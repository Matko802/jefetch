#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "pa.h"

struct PaClient {
    int fd;
    unsigned tag;
};

struct PaRecord {
    int fd;
    unsigned stream;
    uint8_t *pending;
    size_t plen;
    size_t off;
};

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} Wr;

static void wput(Wr *w, const void *p, size_t n) {
    if (w->len + n > w->cap) {
        size_t c = w->cap ? w->cap : 128;
        while (c < w->len + n)
            c *= 2;
        w->data = realloc(w->data, c);
        w->cap = c;
    }
    memcpy(w->data + w->len, p, n);
    w->len += n;
}

static void wtag(Wr *w, uint8_t t) {
    wput(w, &t, 1);
}

static void wu32(Wr *w, uint32_t v) {
    uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8),
                    (uint8_t)v};
    wtag(w, 'L');
    wput(w, b, 4);
}

static void wstr(Wr *w, const char *s) {
    wtag(w, 't');
    wput(w, s, strlen(s) + 1);
}

static void wnull(Wr *w) {
    wtag(w, 'N');
}

static void warb(Wr *w, const uint8_t *d, size_t n) {
    uint8_t b[4] = {(uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8),
                    (uint8_t)n};
    wtag(w, 'x');
    wput(w, b, 4);
    wput(w, d, n);
}

static void wspec(Wr *w, uint8_t fmt, uint8_t ch, uint32_t rate) {
    uint8_t b[4] = {(uint8_t)(rate >> 24), (uint8_t)(rate >> 16), (uint8_t)(rate >> 8),
                    (uint8_t)rate};
    wtag(w, 'a');
    wput(w, &fmt, 1);
    wput(w, &ch, 1);
    wput(w, b, 4);
}

static void wcmap(Wr *w, const uint8_t *m, size_t n) {
    wtag(w, 'm');
    uint8_t c = (uint8_t)n;
    wput(w, &c, 1);
    wput(w, m, n);
}

static void wcvol(Wr *w, uint8_t ch) {
    uint8_t b[4] = {1, 0, 0, 0};
    wtag(w, 'v');
    wput(w, &ch, 1);
    for (unsigned i = 0; i < ch; i++)
        wput(w, b, 4);
}

static void wprop(Wr *w, const char *app) {
    wtag(w, 'P');
    wstr(w, "application.name");
    wu32(w, (uint32_t)strlen(app));
    warb(w, (const uint8_t *)app, strlen(app));
    wstr(w, "application.process.binary");
    wu32(w, (uint32_t)strlen(app));
    warb(w, (const uint8_t *)app, strlen(app));
    wnull(w);
}

static void wbool(Wr *w, int b) {
    wtag(w, b ? '1' : '0');
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

static int send_msg(PaClient *c, uint32_t cmd, uint32_t tag, Wr *body, char *err,
                    size_t errn) {
    Wr pay = {0};
    wtag(&pay, 'L');
    uint8_t b[4] = {(uint8_t)(cmd >> 24), (uint8_t)(cmd >> 16), (uint8_t)(cmd >> 8),
                    (uint8_t)cmd};
    wput(&pay, b, 4);
    wtag(&pay, 'L');
    b[0] = (uint8_t)(tag >> 24);
    b[1] = (uint8_t)(tag >> 16);
    b[2] = (uint8_t)(tag >> 8);
    b[3] = (uint8_t)tag;
    wput(&pay, b, 4);
    wput(&pay, body->data, body->len);
    uint8_t hdr[20];
    uint32_t len = (uint32_t)pay.len;
    hdr[0] = (uint8_t)(len >> 24);
    hdr[1] = (uint8_t)(len >> 16);
    hdr[2] = (uint8_t)(len >> 8);
    hdr[3] = (uint8_t)len;
    memset(hdr + 4, 0xFF, 4);
    memset(hdr + 8, 0, 12);
    int ok = write_all(c->fd, hdr, 20) && write_all(c->fd, pay.data, pay.len);
    if (!ok)
        snprintf(err, errn, "pulse write failed");
    free(pay.data);
    return ok;
}

static int read_exact(int fd, uint8_t *o, size_t n, char *err, size_t errn) {
    size_t got = 0;
    while (got < n) {
        ssize_t k = read(fd, o + got, n - got);
        if (k == 0) {
            snprintf(err, errn, "pulse: EOF");
            return 0;
        }
        if (k < 0) {
            if (errno == EINTR)
                continue;
            snprintf(err, errn, "pulse read failed");
            return 0;
        }
        got += (size_t)k;
    }
    return 1;
}

static uint32_t get_u32(const uint8_t **p, size_t *n) {
    if (*n < 5 || (*p)[0] != 'L')
        return 0;
    uint32_t v = ((uint32_t)(*p)[1] << 24) | ((uint32_t)(*p)[2] << 16) |
                 ((uint32_t)(*p)[3] << 8) | (*p)[4];
    *p += 5;
    *n -= 5;
    return v;
}

static int read_packet(PaClient *c, uint32_t *cmd, uint32_t *tag, uint8_t **body,
                       size_t *bodylen, char *err, size_t errn) {
    uint8_t desc[20];
    if (!read_exact(c->fd, desc, 20, err, errn))
        return 0;
    uint32_t len = ((uint32_t)desc[0] << 24) | ((uint32_t)desc[1] << 16) |
                   ((uint32_t)desc[2] << 8) | desc[3];
    uint32_t ch = ((uint32_t)desc[4] << 24) | ((uint32_t)desc[5] << 16) |
                  ((uint32_t)desc[6] << 8) | desc[7];
    if (len == 0 || len > 262144 || ch != 0xFFFFFFFFu) {
        snprintf(err, errn, "pulse: unexpected frame");
        return 0;
    }
    uint8_t *p = malloc(len);
    if (!read_exact(c->fd, p, len, err, errn)) {
        free(p);
        return 0;
    }
    const uint8_t *q = p;
    size_t qn = len;
    *cmd = get_u32(&q, &qn);
    *tag = get_u32(&q, &qn);
    *bodylen = qn;
    *body = malloc(qn ? qn : 1);
    memcpy(*body, q, qn);
    free(p);
    return 1;
}

static int reply_for(PaClient *c, uint32_t want, char *err, size_t errn) {
    for (;;) {
        uint32_t cmd = 0, tag = 0;
        uint8_t *b = NULL;
        size_t bl = 0;
        if (!read_packet(c, &cmd, &tag, &b, &bl, err, errn))
            return 0;
        if (tag != want) {
            free(b);
            continue;
        }
        if (cmd == 2)
            return 1;
        if (cmd == 0) {
            snprintf(err, errn, "pulse: server error");
            free(b);
            return 0;
        }
        free(b);
    }
}

static void load_cookie(uint8_t *cookie) {
    memset(cookie, 0, 256);
    char paths[3][512];
    int n = 0;
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (rt && n < 3)
        snprintf(paths[n++], sizeof paths[0], "%s/pulse/cookie", rt);
    const char *home = getenv("HOME");
    if (home) {
        if (n < 3)
            snprintf(paths[n++], sizeof paths[0], "%s/.config/pulse/cookie", home);
        if (n < 3)
            snprintf(paths[n++], sizeof paths[0], "%s/.pulse-cookie", home);
    }
    for (int i = 0; i < n; i++) {
        FILE *f = fopen(paths[i], "r");
        if (f) {
            fread(cookie, 1, 256, f);
            fclose(f);
            break;
        }
    }
}

PaClient *pa_connect(const char *app, char *err, size_t errn) {
    char cands[6][512];
    int nc = 0;
    const char *srv = getenv("PULSE_SERVER");
    if (srv) {
        char *dup = strdup(srv);
        char *save = NULL;
        char *tok = strtok_r(dup, " ", &save);
        while (tok && nc < 6) {
            while (*tok == ' ' || *tok == '\t')
                tok++;
            if (*tok) {
                char item[112];
                snprintf(item, sizeof item, "%s", tok);
                if (!strncmp(item, "unix:", 5))
                    snprintf(cands[nc++], sizeof cands[0], "%s", item + 5);
                else if (strncmp(item, "tcp:", 4))
                    snprintf(cands[nc++], sizeof cands[0], "%s", item);
            }
            tok = strtok_r(NULL, " ", &save);
        }
        free(dup);
    }
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (rt && nc < 6)
        snprintf(cands[nc++], sizeof cands[0], "%s/pulse/native", rt);
    {
        unsigned uid = (unsigned)getuid();
        if (nc < 6)
            snprintf(cands[nc++], sizeof cands[0], "/run/user/%u/pulse/native", uid);
    }
    char last_err[256];
    snprintf(last_err, sizeof last_err, "no pulse socket");
    for (int i = 0; i < nc; i++) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
            continue;
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof addr);
        addr.sun_family = AF_UNIX;
        size_t cl = strlen(cands[i]);
        if (cl >= sizeof addr.sun_path) {
            close(fd);
            continue;
        }
        memcpy(addr.sun_path, cands[i], cl + 1);
        if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
            close(fd);
            continue;
        }
        struct timeval tv = {1, 500000};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        PaClient *c = calloc(1, sizeof *c);
        c->fd = fd;
        char e2[256];
        Wr body = {0};
        wu32(&body, 35);
        uint8_t cookie[256];
        load_cookie(cookie);
        warb(&body, cookie, 256);
        c->tag++;
        int ok = send_msg(c, 8, c->tag, &body, e2, sizeof e2);
        free(body.data);
        if (ok) {
            uint32_t t = c->tag;
            if (reply_for(c, t, e2, sizeof e2) <= 0)
                ok = 0;
        }
        if (ok) {
            Wr b2 = {0};
            wprop(&b2, app);
            c->tag++;
            ok = send_msg(c, 9, c->tag, &b2, e2, sizeof e2);
            free(b2.data);
            if (ok) {
                if (reply_for(c, c->tag, e2, sizeof e2) <= 0)
                    ok = 0;
            }
        }
        if (ok)
            return c;
        snprintf(last_err, sizeof last_err, "%s", e2);
        close(fd);
        free(c);
    }
    snprintf(err, errn, "%s", last_err);
    return NULL;
}

void pa_client_free(PaClient *c) {
    if (!c)
        return;
    close(c->fd);
    free(c);
}

int pa_default_monitor(PaClient *c, char *out, size_t outn, char *err, size_t errn) {
    Wr body = {0};
    wu32(&body, 0xFFFFFFFFu);
    wnull(&body);
    c->tag++;
    if (!send_msg(c, 21, c->tag, &body, err, errn)) {
        free(body.data);
        return 0;
    }
    free(body.data);
    uint8_t *rb = NULL;
    size_t rbl = 0;
    {
        uint32_t cmd = 0, tag = 0;
        if (!read_packet(c, &cmd, &tag, &rb, &rbl, err, errn))
            return 0;
        if (tag != c->tag || cmd != 2) {
            free(rb);
            snprintf(err, errn, "pulse: bad reply");
            return 0;
        }
    }
    const uint8_t *p = rb;
    size_t n = rbl;
    get_u32(&p, &n);
    char tmp[4][512];
    int ti = 0;
    for (int i = 0; i < 2; i++) {
        if (n < 1)
            break;
        if (p[0] == 'N') {
            p++;
            n--;
        } else if (p[0] == 't') {
            p++;
            n--;
            size_t k = 0;
            while (k < n && p[k])
                k++;
            if (k >= n)
                break;
            size_t cp = k < sizeof tmp[0] - 1 ? k : sizeof tmp[0] - 1;
            memcpy(tmp[ti], p, cp);
            tmp[ti][cp] = 0;
            ti++;
            p += k + 1;
            n -= k + 1;
        } else {
            break;
        }
    }
    if (n < 1 + 6) {
        free(rb);
        return 0;
    }
    p++;
    n--;
    p += 6;
    n -= 6;
    if (n < 1) {
        free(rb);
        return 0;
    }
    {
        unsigned char mc = *p;
        p++;
        n--;
        if (mc > n) {
            free(rb);
            return 0;
        }
        p += mc;
        n -= mc;
    }
    if (n < 5) {
        free(rb);
        return 0;
    }
    p += 5;
    n -= 5;
    if (n >= 4 && p[0] == 'v') {
        p++;
        n--;
        if (n < 1) {
            free(rb);
            return 0;
        }
        unsigned char cc = *p;
        p++;
        n--;
        if (n < (size_t)cc * 4) {
            free(rb);
            return 0;
        }
        p += (size_t)cc * 4;
        n -= (size_t)cc * 4;
    }
    if (n < 1) {
        free(rb);
        return 0;
    }
    p++;
    n--;
    if (n < 5) {
        free(rb);
        return 0;
    }
    p += 5;
    n -= 5;
    int ok = 0;
    if (n >= 1) {
        if (p[0] == 'N') {
            p++;
            n--;
        } else if (p[0] == 't') {
            p++;
            n--;
            size_t k = 0;
            while (k < n && p[k])
                k++;
            if (k < n) {
                char mon[512];
                size_t cp = k < sizeof mon - 1 ? k : sizeof mon - 1;
                memcpy(mon, p, cp);
                mon[cp] = 0;
                if (mon[0]) {
                    snprintf(out, outn, "%s", mon);
                    ok = 1;
                }
            }
        }
    }
    free(rb);
    return ok;
}

PaRecord *pa_record(PaClient *c, const char *device, unsigned rate, unsigned channels,
                    char *err, size_t errn) {
    uint8_t map[2];
    size_t mapn;
    if (channels >= 2) {
        map[0] = 1;
        map[1] = 2;
        mapn = 2;
    } else {
        map[0] = 0;
        mapn = 1;
    }
    Wr body = {0};
    wspec(&body, 3, (uint8_t)channels, rate);
    wcmap(&body, map, mapn);
    wu32(&body, 0xFFFFFFFFu);
    wstr(&body, device);
    wu32(&body, 0xFFFFFFFFu);
    wbool(&body, 0);
    wu32(&body, 320);
    for (int i = 0; i < 11; i++)
        wbool(&body, 0);
    wtag(&body, 'P');
    wstr(&body, "application.name");
    wu32(&body, 12);
    warb(&body, (const uint8_t *)"jefetch-beat", 12);
    wstr(&body, "application.process.binary");
    wu32(&body, 12);
    warb(&body, (const uint8_t *)"jefetch-beat", 12);
    wnull(&body);
    wu32(&body, 0xFFFFFFFFu);
    wbool(&body, 0);
    wbool(&body, 0);
    wbool(&body, 0);
    {
        uint8_t z = 0;
        wtag(&body, 'B');
        wput(&body, &z, 1);
    }
    wcvol(&body, (uint8_t)channels);
    for (int i = 0; i < 5; i++)
        wbool(&body, 0);
    c->tag++;
    if (!send_msg(c, 5, c->tag, &body, err, errn)) {
        free(body.data);
        return NULL;
    }
    free(body.data);
    uint8_t *rb = NULL;
    size_t rbl = 0;
    uint32_t stream = 0;
    {
        uint32_t cmd = 0, tag = 0;
        uint8_t *b = NULL;
        size_t bl = 0;
        if (!read_packet(c, &cmd, &tag, &b, &bl, err, errn))
            return NULL;
        if (tag != c->tag || cmd != 2) {
            free(b);
            snprintf(err, errn, "pulse: bad record reply");
            return NULL;
        }
        const uint8_t *p = b;
        size_t qn = bl;
        stream = get_u32(&p, &qn);
        free(b);
        (void)rb;
        (void)rbl;
    }
    PaRecord *rec = calloc(1, sizeof *rec);
    rec->fd = c->fd;
    rec->stream = stream;
    free(c);
    return rec;
}

void pa_record_free(PaRecord *r) {
    if (!r)
        return;
    close(r->fd);
    free(r->pending);
    free(r);
}

long pa_read_chunk(PaRecord *r, uint8_t *out, size_t cap, const volatile int *stop,
                   char *err, size_t errn) {
    if (r->off < r->plen) {
        size_t avail = r->plen - r->off;
        size_t n = avail < cap ? avail : cap;
        memcpy(out, r->pending + r->off, n);
        r->off += n;
        return (long)n;
    }
    free(r->pending);
    r->pending = NULL;
    r->plen = 0;
    r->off = 0;
    for (;;) {
        if (stop && *stop)
            return 0;
        uint8_t desc[20];
        if (!read_exact(r->fd, desc, 20, err, errn))
            return -1;
        uint32_t len = ((uint32_t)desc[0] << 24) | ((uint32_t)desc[1] << 16) |
                       ((uint32_t)desc[2] << 8) | desc[3];
        uint32_t ch = ((uint32_t)desc[4] << 24) | ((uint32_t)desc[5] << 16) |
                      ((uint32_t)desc[6] << 8) | desc[7];
        if (len == 0 || len > 262144) {
            snprintf(err, errn, "pulse: bogus frame");
            return -1;
        }
        uint8_t *p = malloc(len);
        if (!read_exact(r->fd, p, len, err, errn)) {
            free(p);
            return -1;
        }
        if (ch == 0xFFFFFFFFu) {
            const uint8_t *q = p;
            size_t qn = len;
            uint32_t cmd = get_u32(&q, &qn);
            get_u32(&q, &qn);
            free(p);
            if (cmd == 0) {
                snprintf(err, errn, "pulse: server error");
                return -1;
            }
            if (cmd == 65) {
                snprintf(err, errn, "pulse: stream killed");
                return -1;
            }
            continue;
        }
        if (ch == r->stream) {
            size_t n = len < cap ? len : cap;
            memcpy(out, p, n);
            if (len > cap) {
                r->pending = malloc(len - cap);
                memcpy(r->pending, p + cap, len - cap);
                r->plen = len - cap;
            }
            free(p);
            return (long)n;
        }
        free(p);
    }
}
