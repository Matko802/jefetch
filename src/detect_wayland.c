#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "detect.h"

#define WL_DISPLAY_ID 1u
#define WL_REGISTRY_ID 2u
#define WL_REGISTRY_GLOBAL 0u
#define WL_OUTPUT_MODE 1u
#define WL_OUTPUT_SCALE 3u
#define WL_OUTPUT_NAME 4u
#define WL_OUTPUT_DESCRIPTION 5u
#define WL_CALLBACK_DONE 0u
#define ZXDG_OUTPUT_LOGICAL_SIZE 1u
#define WL_OUTPUT_MODE_CURRENT 0x1u
#define MAX_MESSAGES 512

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} Msg;

static void m_put(Msg *m, const void *p, size_t n) {
    if (m->len + n > m->cap) {
        size_t c = m->cap ? m->cap : 128;
        while (c < m->len + n)
            c *= 2;
        m->data = realloc(m->data, c);
        m->cap = c;
    }
    memcpy(m->data + m->len, p, n);
    m->len += n;
}

static void put_u32(Msg *m, uint32_t v) {
    uint8_t b[4];
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
    m_put(m, b, 4);
}

static void put_string(Msg *m, const char *s) {
    size_t l = strlen(s);
    put_u32(m, (uint32_t)(l + 1));
    m_put(m, s, l + 1);
    while (m->len % 4 != 0) {
        uint8_t z = 0;
        m_put(m, &z, 1);
    }
}

static void put_header(Msg *m, uint32_t object, uint16_t opcode, size_t body_len) {
    uint32_t size = (uint32_t)(8 + body_len);
    put_u32(m, object);
    put_u32(m, (size << 16) | opcode);
}

typedef struct {
    char name[128];
    char make[128];
    char model[256];
    uint32_t width;
    uint32_t height;
    uint32_t refresh_mhz;
    int have_current;
    int scale;
    int logical_width;
    int logical_height;
} OutputAcc;

typedef struct {
    uint32_t id;
    OutputAcc acc;
} OutEntry;

typedef struct {
    uint32_t name;
    char interface[64];
    uint32_t version;
} Global;

typedef struct {
    uint32_t xdg;
    uint32_t wl;
} XdgMap;

static uint32_t rd_u32(const uint8_t **p, size_t *n) {
    if (*n < 4)
        return 0;
    uint32_t v = (*p)[0] | ((uint32_t)(*p)[1] << 8) | ((uint32_t)(*p)[2] << 16) |
                 ((uint32_t)(*p)[3] << 24);
    *p += 4;
    *n -= 4;
    return v;
}

static int rd_string(const uint8_t **p, size_t *n, char *out, size_t outn) {
    if (*n < 4)
        return 0;
    uint32_t len = (*p)[0] | ((uint32_t)(*p)[1] << 8) | ((uint32_t)(*p)[2] << 16) |
                   ((uint32_t)(*p)[3] << 24);
    *p += 4;
    *n -= 4;
    if (len == 0 || len - 1 > *n)
        return 0;
    size_t copy = len - 1;
    if (copy >= outn)
        copy = outn - 1;
    memcpy(out, *p, copy);
    out[copy] = 0;
    size_t padded = (len + 3) & ~(size_t)3;
    if (padded > *n)
        return 0;
    *p += padded;
    *n -= padded;
    return 1;
}

static void apply_output_event(OutputAcc *acc, uint16_t opcode, const uint8_t *body,
                               size_t blen) {
    const uint8_t *p = body;
    size_t n = blen;
    if (opcode == 0) {
        for (int i = 0; i < 5; i++)
            rd_u32(&p, &n);
        char tmp[256];
        if (rd_string(&p, &n, tmp, sizeof tmp))
            snprintf(acc->make, sizeof acc->make, "%.*s",
                     (int)(sizeof acc->make - 1), tmp);
        if (rd_string(&p, &n, tmp, sizeof tmp))
            snprintf(acc->model, sizeof acc->model, "%.*s",
                     (int)(sizeof acc->model - 1), tmp);
        rd_u32(&p, &n);
    } else if (opcode == WL_OUTPUT_MODE) {
        uint32_t flags = rd_u32(&p, &n);
        uint32_t w = rd_u32(&p, &n);
        uint32_t h = rd_u32(&p, &n);
        uint32_t hz = rd_u32(&p, &n);
        if (flags & WL_OUTPUT_MODE_CURRENT) {
            acc->width = w;
            acc->height = h;
            acc->refresh_mhz = hz;
            acc->have_current = 1;
        }
    } else if (opcode == WL_OUTPUT_SCALE) {
        if (n >= 4) {
            int f = (int)rd_u32(&p, &n);
            if (f > 0)
                acc->scale = f;
        }
    } else if (opcode == WL_OUTPUT_NAME) {
        char tmp[128];
        if (rd_string(&p, &n, tmp, sizeof tmp))
            snprintf(acc->name, sizeof acc->name, "%.*s",
                     (int)(sizeof acc->name - 1), tmp);
    } else if (opcode == WL_OUTPUT_DESCRIPTION) {
        char tmp[8];
        rd_string(&p, &n, tmp, sizeof tmp);
    }
}

static OutputAcc *find_out(OutEntry *outs, size_t nouts, uint32_t id) {
    for (size_t i = 0; i < nouts; i++) {
        if (outs[i].id == id)
            return &outs[i].acc;
    }
    return NULL;
}

static size_t parse_messages(const uint8_t *data, size_t len, OutEntry **outs, size_t *nouts,
                             XdgMap **xmap, size_t *nxmap, Global **globals, size_t *nglob,
                             int *sync_done, uint32_t sync_id) {
    size_t off = 0;
    size_t count = 0;
    while (off + 8 <= len && count < MAX_MESSAGES) {
        count++;
        uint32_t object = data[off] | ((uint32_t)data[off + 1] << 8) |
                          ((uint32_t)data[off + 2] << 16) | ((uint32_t)data[off + 3] << 24);
        uint32_t size_op = data[off + 4] | ((uint32_t)data[off + 5] << 8) |
                           ((uint32_t)data[off + 6] << 16) | ((uint32_t)data[off + 7] << 24);
        size_t size = size_op >> 16;
        uint16_t opcode = (uint16_t)(size_op & 0xffff);
        if (size < 8 || off + size > len)
            break;
        const uint8_t *body = data + off + 8;
        size_t blen = size - 8;
        if (object == WL_REGISTRY_ID && opcode == WL_REGISTRY_GLOBAL) {
            const uint8_t *p = body;
            size_t bn = blen;
            uint32_t name = rd_u32(&p, &bn);
            char iface[64] = "";
            rd_string(&p, &bn, iface, sizeof iface);
            uint32_t ver = rd_u32(&p, &bn);
            if (iface[0]) {
                *globals = realloc(*globals, (*nglob + 1) * sizeof(Global));
                (*globals)[*nglob].name = name;
                snprintf((*globals)[*nglob].interface, sizeof((*globals)[*nglob].interface),
                         "%s", iface);
                (*globals)[*nglob].version = ver;
                (*nglob)++;
            }
        } else if (object == sync_id && opcode == WL_CALLBACK_DONE) {
            *sync_done = 1;
        } else {
            OutputAcc *acc = find_out(*outs, *nouts, object);
            if (acc) {
                apply_output_event(acc, opcode, body, blen);
            } else {
                uint32_t wl_id = 0;
                int found = 0;
                for (size_t i = 0; i < *nxmap; i++) {
                    if ((*xmap)[i].xdg == object) {
                        wl_id = (*xmap)[i].wl;
                        found = 1;
                        break;
                    }
                }
                if (found && opcode == ZXDG_OUTPUT_LOGICAL_SIZE && blen >= 8) {
                    const uint8_t *p = body;
                    size_t bn = blen;
                    int w = (int)rd_u32(&p, &bn);
                    int h = (int)rd_u32(&p, &bn);
                    if (w > 0 && h > 0) {
                        OutputAcc *a2 = find_out(*outs, *nouts, wl_id);
                        if (a2) {
                            a2->logical_width = w;
                            a2->logical_height = h;
                        }
                    }
                }
            }
        }
        off += size;
    }
    return off;
}

static int write_all(int fd, const uint8_t *p, size_t n) {
    while (n > 0) {
        ssize_t k = write(fd, p, n);
        if (k < 0) {
            if (errno == EINTR)
                continue;
            return 0;
        }
        p += k;
        n -= (size_t)k;
    }
    return 1;
}

static void socket_path(char *out, size_t n) {
    out[0] = 0;
    const char *display = getenv("WAYLAND_DISPLAY");
    if (!display || !*display)
        return;
    if (strchr(display, '/')) {
        snprintf(out, n, "%s", display);
        return;
    }
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (rt && *rt) {
        size_t l = strlen(rt);
        while (l > 0 && rt[l - 1] == '/')
            l--;
        snprintf(out, n, "%.*s/%s", (int)l, rt, display);
        return;
    }
    unsigned uid = (unsigned)getuid();
    char candidate[256];
    snprintf(candidate, sizeof candidate, "/run/user/%u/%s", uid, display);
    struct stat st;
    if (stat(candidate, &st) == 0)
        snprintf(out, n, "%s", candidate);
}

WlOutput *wl_query_outputs(size_t *n) {
    char path[512];
    WlOutput *result = NULL;
    size_t nres = 0;
    *n = 0;
    socket_path(path, sizeof path);
    if (!path[0])
        return NULL;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return NULL;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    size_t pl = strlen(path);
    if (pl >= sizeof addr.sun_path) {
        close(fd);
        return NULL;
    }
    memcpy(addr.sun_path, path, pl + 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        close(fd);
        return NULL;
    }
    struct timeval tv = {1, 500000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    Msg reg = {0};
    {
        Msg body = {0};
        put_u32(&body, WL_REGISTRY_ID);
        put_header(&reg, WL_DISPLAY_ID, 1, body.len);
        m_put(&reg, body.data, body.len);
        free(body.data);
    }
    if (!write_all(fd, reg.data, reg.len)) {
        free(reg.data);
        close(fd);
        return NULL;
    }
    free(reg.data);
    uint8_t *data = NULL;
    size_t dlen = 0, dcap = 0;
    Global *globals = NULL;
    size_t nglob = 0, glob_cap = 0;
    OutEntry *outs = NULL;
    size_t nouts = 0, outs_cap = 0;
    XdgMap *xmap = NULL;
    size_t nxmap = 0, xmap_cap = 0;
    uint32_t next_id = 3;
    int bound = 0;
    uint32_t sync_id = 0;
    int sync_done = 0;
    size_t consumed = 0;
    uint8_t tmp[65536];
    (void)glob_cap;
    (void)outs_cap;
    (void)xmap_cap;
    for (;;) {
        ssize_t k = read(fd, tmp, sizeof tmp);
        if (k <= 0)
            break;
        if (dlen + (size_t)k > dcap) {
            size_t c = dcap ? dcap : 65536;
            while (c < dlen + (size_t)k)
                c *= 2;
            data = realloc(data, c);
            dcap = c;
        }
        memcpy(data + dlen, tmp, (size_t)k);
        dlen += (size_t)k;
        if (!bound) {
            size_t nfresh = nglob;
            consumed += parse_messages(data + consumed, dlen - consumed, &outs, &nouts,
                                       &xmap, &nxmap, &globals, &nglob, &sync_done,
                                       sync_id);
            Msg out = {0};
            uint32_t *wl_ids = NULL;
            size_t nwl = 0;
            for (size_t i = nfresh; i < nglob; i++) {
                if (!strcmp(globals[i].interface, "wl_output")) {
                    uint32_t id = next_id++;
                    outs = realloc(outs, (nouts + 1) * sizeof(OutEntry));
                    outs[nouts].id = id;
                    memset(&outs[nouts].acc, 0, sizeof(OutputAcc));
                    nouts++;
                    wl_ids = realloc(wl_ids, (nwl + 1) * sizeof(uint32_t));
                    wl_ids[nwl++] = id;
                    Msg body = {0};
                    put_u32(&body, globals[i].name);
                    put_string(&body, globals[i].interface);
                    uint32_t ver = globals[i].version < 4 ? globals[i].version : 4;
                    put_u32(&body, ver);
                    put_u32(&body, id);
                    put_header(&out, WL_REGISTRY_ID, 0, body.len);
                    m_put(&out, body.data, body.len);
                    free(body.data);
                }
            }
            uint32_t mgr = 0;
            for (size_t i = nfresh; i < nglob; i++) {
                if (!strcmp(globals[i].interface, "zxdg_output_manager_v1")) {
                    uint32_t id = next_id++;
                    Msg body = {0};
                    put_u32(&body, globals[i].name);
                    put_string(&body, globals[i].interface);
                    uint32_t ver = globals[i].version < 3 ? globals[i].version : 3;
                    put_u32(&body, ver);
                    put_u32(&body, id);
                    put_header(&out, WL_REGISTRY_ID, 0, body.len);
                    m_put(&out, body.data, body.len);
                    free(body.data);
                    mgr = id;
                    break;
                }
            }
            if (mgr) {
                for (size_t i = 0; i < nwl; i++) {
                    uint32_t xid = next_id++;
                    Msg body = {0};
                    put_u32(&body, xid);
                    put_u32(&body, wl_ids[i]);
                    put_header(&out, mgr, 1, body.len);
                    m_put(&out, body.data, body.len);
                    free(body.data);
                    xmap = realloc(xmap, (nxmap + 1) * sizeof(XdgMap));
                    xmap[nxmap].xdg = xid;
                    xmap[nxmap].wl = wl_ids[i];
                    nxmap++;
                }
            }
            free(wl_ids);
            if (out.len == 0) {
                free(out.data);
                break;
            }
            sync_id = next_id++;
            {
                Msg body = {0};
                put_u32(&body, sync_id);
                put_header(&out, WL_DISPLAY_ID, 0, body.len);
                m_put(&out, body.data, body.len);
                free(body.data);
            }
            if (!write_all(fd, out.data, out.len)) {
                free(out.data);
                break;
            }
            free(out.data);
            bound = 1;
            continue;
        }
        consumed += parse_messages(data + consumed, dlen - consumed, &outs, &nouts, &xmap,
                                   &nxmap, &globals, &nglob, &sync_done, sync_id);
        if (sync_done)
            break;
        if (dlen > (size_t)1 << 20)
            break;
    }
    close(fd);
    for (size_t i = 0; i < nouts; i++) {
        if (!outs[i].acc.have_current)
            continue;
        result = realloc(result, (nres + 1) * sizeof(WlOutput));
        snprintf(result[nres].name, sizeof result[nres].name, "%s", outs[i].acc.name);
        snprintf(result[nres].make, sizeof result[nres].make, "%s", outs[i].acc.make);
        snprintf(result[nres].model, sizeof result[nres].model, "%s", outs[i].acc.model);
        result[nres].width = outs[i].acc.width;
        result[nres].height = outs[i].acc.height;
        result[nres].refresh_mhz = outs[i].acc.refresh_mhz;
        result[nres].scale = outs[i].acc.scale;
        result[nres].logical_width = outs[i].acc.logical_width;
        result[nres].logical_height = outs[i].acc.logical_height;
        nres++;
    }
    for (size_t i = 0; i < nres; i++) {
        for (size_t j = i + 1; j < nres; j++) {
            if (strcmp(result[i].name, result[j].name) > 0) {
                WlOutput t = result[i];
                result[i] = result[j];
                result[j] = t;
            }
        }
    }
    free(data);
    free(globals);
    free(outs);
    free(xmap);
    *n = nres;
    return result;
}

void wl_outputs_free(WlOutput *o, size_t n) {
    (void)n;
    free(o);
}
