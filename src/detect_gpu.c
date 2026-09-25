#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "detect.h"

static const char *VENDOR_NAMES[][2] = {
    {"0x1002", "AMD/ATI"}, {"0x10de", "NVIDIA"}, {"0x8086", "Intel"},
    {"0x1a03", "ASPEED"}, {"0x106b", "Apple"}, {"0x1022", "AMD"},
    {"0x14e3", "Loongson"}, {"0x13b5", "Arm"}, {"0x19e5", "Huawei"},
    {"0x1518", "Kontron"}, {NULL, NULL}
};

static void vendor_name(const char *id, char *out, size_t n) {
    char l[16];
    size_t i = 0;
    while (id[i] && i + 1 < sizeof l) {
        char c = id[i];
        l[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    l[i] = 0;
    for (int k = 0; VENDOR_NAMES[k][0]; k++) {
        char v[16];
        size_t j = 0;
        while (VENDOR_NAMES[k][0][j] && j + 1 < sizeof v) {
            char c = VENDOR_NAMES[k][0][j];
            v[j++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        v[j] = 0;
        if (!strcmp(l, v)) {
            snprintf(out, n, "%s", VENDOR_NAMES[k][1]);
            return;
        }
    }
    snprintf(out, n, "%s", id);
}

typedef struct {
    char *vendor;
    char *dev;
    char *name;
} PciEntry;

typedef struct {
    PciEntry *items;
    size_t n;
} PciIds;

static void split_ids_line(const char *line, char *id, size_t idn, char *name, size_t namen) {
    (void)idn;
    id[0] = 0;
    name[0] = 0;
    while (*line == ' ' || *line == '\t')
        line++;
    size_t i = 0;
    while (line[i] && i < 4) {
        char c = line[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return;
        i++;
    }
    if (i != 4)
        return;
    for (i = 0; i < 4; i++) {
        char c = line[i];
        id[i] = (char)(c >= 'A' && c <= 'F' ? c + 32 : c);
    }
    id[4] = 0;
    const char *rest = line + 4;
    while (*rest == ' ' || *rest == '\t')
        rest++;
    snprintf(name, namen, "%s", rest);
}

static PciIds parse_pci_ids(const char *text) {
    PciIds ids = {0};
    char vendor[8] = "";
    char *dup = strdup(text);
    char *save = NULL;
    char *line = strtok_r(dup, "\n", &save);
    while (line) {
        size_t l = strlen(line);
        while (l > 0 && (line[l - 1] == '\r'))
            line[--l] = 0;
        if (!line[0] || line[0] == '#') {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        if (line[0] != '\t') {
            char id[8], name[256];
            split_ids_line(line, id, sizeof id, name, sizeof name);
            if (id[0])
                snprintf(vendor, sizeof vendor, "%s", id);
        } else {
            const char *t = line;
            while (*t == '\t')
                t++;
            if (*t == '\t') {
                line = strtok_r(NULL, "\n", &save);
                continue;
            }
            char id[8], name[256];
            split_ids_line(t, id, sizeof id, name, sizeof name);
            if (id[0] && vendor[0]) {
                ids.items = realloc(ids.items, (ids.n + 1) * sizeof(PciEntry));
                ids.items[ids.n].vendor = strdup(vendor);
                ids.items[ids.n].dev = strdup(id);
                ids.items[ids.n].name = strdup(name);
                ids.n++;
            }
        }
        line = strtok_r(NULL, "\n", &save);
    }
    free(dup);
    return ids;
}

static void pci_lookup(PciIds *ids, const char *vendor, const char *device, char *out,
                       size_t n) {
    out[0] = 0;
    char v[16], d[16];
    const char *vv = vendor;
    if (!strncasecmp(vv, "0x", 2))
        vv += 2;
    size_t i = 0;
    while (vv[i] && i + 1 < sizeof v) {
        char c = vv[i];
        v[i++] = (char)(c >= 'A' && c <= 'F' ? c + 32 : c);
    }
    v[i] = 0;
    const char *dd = device;
    if (!strncasecmp(dd, "0x", 2))
        dd += 2;
    i = 0;
    while (dd[i] && i + 1 < sizeof d) {
        char c = dd[i];
        d[i++] = (char)(c >= 'A' && c <= 'F' ? c + 32 : c);
    }
    d[i] = 0;
    for (i = 0; i < ids->n; i++) {
        if (!strcmp(ids->items[i].vendor, v) && !strcmp(ids->items[i].dev, d)) {
            snprintf(out, n, "%s", ids->items[i].name);
            return;
        }
    }
}

static int str_case_eq(const char *a, const char *b);
static void strip_0x(const char *s, char *out, size_t n) {
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    snprintf(out, n, "%s", s);
}
static int str_case_eq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z')
            ca += 32;
        if (cb >= 'A' && cb <= 'Z')
            cb += 32;
        if (ca != cb)
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static void store_version(const char *dir_name, const char *marker, unsigned long long *out,
                          size_t n, int *ok) {
    const char *p = strstr(dir_name, marker);
    *ok = 0;
    if (!p)
        return;
    p += strlen(marker);
    if (!*p)
        return;
    size_t m = 0;
    char *dup = strdup(p);
    char *save = NULL;
    char *tok = strtok_r(dup, ".", &save);
    while (tok && m < n) {
        char *end;
        unsigned long long v = strtoull(tok, &end, 10);
        if (end == tok || *end) {
            free(dup);
            return;
        }
        out[m++] = v;
        tok = strtok_r(NULL, ".", &save);
    }
    free(dup);
    if (m == 0)
        return;
    for (size_t i = m; i < n; i++)
        out[i] = 0;
    *ok = 1;
}

static int version_gt(const unsigned long long *a, const unsigned long long *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i])
            return a[i] > b[i];
    }
    return 0;
}

static void newest_store_file(const char *marker, const char *suffix, char *out, size_t n) {
    out[0] = 0;
    DIR *dp = opendir("/nix/store");
    if (!dp)
        return;
    unsigned long long best[8] = {0};
    int have = 0;
    char best_path[1152] = "";
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        unsigned long long ver[8] = {0};
        int ok = 0;
        store_version(de->d_name, marker, ver, 8, &ok);
        if (!ok)
            continue;
        char path[1152];
        size_t dl = strlen(de->d_name);
        size_t sl = strlen(suffix);
        if (dl + sl + 13 >= sizeof path)
            continue;
        memcpy(path, "/nix/store/", 11);
        memcpy(path + 11, de->d_name, dl);
        path[11 + dl] = '/';
        memcpy(path + 12 + dl, suffix, sl + 1);
        struct stat st;
        if (stat(path, &st) != 0)
            continue;
        if (!have || version_gt(ver, best, 8)) {
            memcpy(best, ver, sizeof best);
            snprintf(best_path, sizeof best_path, "%s", path);
            have = 1;
        }
    }
    closedir(dp);
    if (have)
        snprintf(out, n, "%s", best_path);
}

static PciIds *pci_cache(void) {
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    static PciIds *cached = NULL;
    static int done = 0;
    pthread_mutex_lock(&mu);
    if (done) {
        PciIds *c = cached;
        pthread_mutex_unlock(&mu);
        return c;
    }
    done = 1;
    char paths[7][1152];
    int np = 0;
    char *e = detect_getenv("PCI_IDS_PATH");
    if (e && *e) {
        snprintf(paths[np], sizeof paths[0], "%s", e);
        np++;
    }
    free(e);
    e = detect_getenv("JEFETCH_PCI_IDS");
    if (e && *e && np < 7) {
        snprintf(paths[np], sizeof paths[0], "%s", e);
        np++;
    }
    free(e);
    snprintf(paths[np++], sizeof paths[0], "/usr/share/hwdata/pci.ids");
    snprintf(paths[np++], sizeof paths[0], "/usr/share/misc/pci.ids");
    snprintf(paths[np++], sizeof paths[0], "/etc/pci.ids");
    snprintf(paths[np++], sizeof paths[0], "/run/current-system/sw/share/hwdata/pci.ids");
    if (np < 7) {
        char extra[1152];
        newest_store_file("-hwdata-", "share/hwdata/pci.ids", extra, sizeof extra);
        if (extra[0]) {
            snprintf(paths[np], sizeof paths[0], "%s", extra);
            np++;
        }
    }
    for (int i = 0; i < np; i++) {
        char *text = detect_read_file(paths[i]);
        if (text) {
            cached = malloc(sizeof(PciIds));
            *cached = parse_pci_ids(text);
            free(text);
            break;
        }
    }
    pthread_mutex_unlock(&mu);
    return cached;
}

typedef struct {
    char *did;
    char *rev;
    char *name;
} AmdEntry;

static AmdEntry *parse_amdgpu_ids(const char *text, size_t *n) {
    AmdEntry *out = NULL;
    size_t m = 0;
    *n = 0;
    char *dup = strdup(text);
    char *save = NULL;
    char *line = strtok_r(dup, "\n", &save);
    while (line) {
        while (*line == ' ' || *line == '\t')
            line++;
        char *e = line + strlen(line);
        while (e > line && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
            *--e = 0;
        if (!*line || *line == '#') {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        char *c1 = strchr(line, ',');
        char *c2 = c1 ? strchr(c1 + 1, ',') : NULL;
        if (!c1 || !c2) {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        *c1 = 0;
        *c2 = 0;
        char *did = line;
        char *rev = c1 + 1;
        char *name = c2 + 1;
        while (*did == ' ' || *did == '\t')
            did++;
        while (*rev == ' ' || *rev == '\t')
            rev++;
        while (*name == ' ' || *name == '\t')
            name++;
        char *t;
        t = did + strlen(did);
        while (t > did && (t[-1] == ' ' || t[-1] == '\t'))
            *--t = 0;
        t = rev + strlen(rev);
        while (t > rev && (t[-1] == ' ' || t[-1] == '\t'))
            *--t = 0;
        t = name + strlen(name);
        while (t > name && (t[-1] == ' ' || t[-1] == '\t'))
            *--t = 0;
        if (!*did || !*rev || !*name) {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        out = realloc(out, (m + 1) * sizeof(AmdEntry));
        out[m].did = strdup(did);
        out[m].rev = strdup(rev);
        out[m].name = strdup(name);
        m++;
        line = strtok_r(NULL, "\n", &save);
    }
    free(dup);
    *n = m;
    return out;
}

static AmdEntry *amdgpu_cache(size_t *n) {
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    static AmdEntry *cached = NULL;
    static size_t cached_n = 0;
    static int done = 0;
    pthread_mutex_lock(&mu);
    if (done) {
        *n = cached_n;
        AmdEntry *c = cached;
        pthread_mutex_unlock(&mu);
        return c;
    }
    done = 1;
    char paths[4][1152];
    int np = 0;
    char *e = detect_getenv("AMDGPU_IDS_PATH");
    if (e && *e) {
        snprintf(paths[np], sizeof paths[0], "%s", e);
        np++;
    }
    free(e);
    snprintf(paths[np++], sizeof paths[0], "/run/current-system/sw/share/libdrm/amdgpu.ids");
    snprintf(paths[np++], sizeof paths[0], "/usr/share/libdrm/amdgpu.ids");
    if (np < 4) {
        char extra[1152];
        newest_store_file("-libdrm-", "share/libdrm/amdgpu.ids", extra, sizeof extra);
        if (extra[0]) {
            snprintf(paths[np], sizeof paths[0], "%s", extra);
            np++;
        }
    }
    for (int i = 0; i < np; i++) {
        char *text = detect_read_file(paths[i]);
        if (text) {
            cached = parse_amdgpu_ids(text, &cached_n);
            free(text);
            break;
        }
    }
    *n = cached_n;
    AmdEntry *c = cached;
    pthread_mutex_unlock(&mu);
    return c;
}

static int amdgpu_lookup(const char *vendor, const char *device, const char *revision,
                         char *out, size_t n) {
    if (!str_case_eq(vendor, "0x1002") && !str_case_eq(vendor, "0x1022"))
        return 0;
    if (!device[0] || !revision[0])
        return 0;
    size_t m = 0;
    AmdEntry *table = amdgpu_cache(&m);
    char did[32];
    strip_0x(device, did, sizeof did);
    for (size_t i = 0; i < m; i++) {
        if (str_case_eq(table[i].did, did) && str_case_eq(table[i].rev, revision)) {
            snprintf(out, n, "%s", table[i].name);
            return 1;
        }
    }
    return 0;
}

static void gpu_type(const char *base, const char *vendor, const char *driver, char *out,
                     size_t n) {
    out[0] = 0;
    if (str_case_eq(vendor, "0x1002") && !strcmp(driver, "amdgpu")) {
        char hw[1152];
        snprintf(hw, sizeof hw, "%s/device/hwmon", base);
        DIR *dp = opendir(hw);
        if (dp) {
            struct dirent *de;
            while ((de = readdir(dp)) != NULL) {
                size_t el = strlen(de->d_name);
                char en[65];
                if (el >= sizeof en)
                    continue;
                memcpy(en, de->d_name, el + 1);
                char p[1280];
                snprintf(p, sizeof p, "%s/%s/in1_input", hw, en);
                FILE *f = fopen(p, "r");
                if (f) {
                    fclose(f);
                    closedir(dp);
                    snprintf(out, n, "Integrated");
                    return;
                }
            }
            closedir(dp);
        }
        snprintf(out, n, "Discrete");
        return;
    }
    if (str_case_eq(vendor, "0x8086")) {
        char dev[1152];
        snprintf(dev, sizeof dev, "%s/device", base);
        char link[1024] = "";
        ssize_t l = readlink(dev, link, sizeof link - 1);
        const char *addr = "";
        if (l > 0) {
            link[l] = 0;
            const char *b = strrchr(link, '/');
            addr = b ? b + 1 : link;
        }
        snprintf(out, n, "%s", !strcmp(addr, "0000:00:02.0") ? "Integrated" : "Discrete");
        return;
    }
    if (str_case_eq(vendor, "0x10de")) {
        snprintf(out, n, "Discrete");
        return;
    }
    if (str_case_eq(vendor, "0x13b5") || !strcmp(driver, "panfrost") ||
        !strcmp(driver, "panthor") || !strcmp(driver, "lima") || !strcmp(driver, "v3d") ||
        !strcmp(driver, "vc4") || !strcmp(driver, "etnaviv") ||
        !strcmp(driver, "freedreno") || !strcmp(driver, "adreno") || !strcmp(driver, "msm")) {
        snprintf(out, n, "Integrated");
        return;
    }
}

GpuInfo *detect_gpu(size_t *n) {
    GpuInfo *out = NULL;
    size_t m = 0;
    *n = 0;
    PciIds *ids = pci_cache();
    DIR *dp = opendir("/sys/class/drm");
    if (!dp)
        return NULL;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        const char *name = de->d_name;
        if (strncmp(name, "card", 4) != 0)
            continue;
        const char *after = name + 4;
        if (!*after)
            continue;
        int ok = 1;
        for (const char *p = after; *p; p++) {
            if (*p < '0' || *p > '9') {
                ok = 0;
                break;
            }
        }
        if (!ok)
            continue;
        char base[512];
        snprintf(base, sizeof base, "/sys/class/drm/%s", name);
        char p[576], vendor[32] = "", device[32] = "", revision[32] = "";
        snprintf(p, sizeof p, "%s/device/vendor", base);
        char *t = detect_read_file(p);
        if (t) {
            char *e = t + strlen(t);
            while (e > t && (e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'))
                *--e = 0;
            snprintf(vendor, sizeof vendor, "%s", t);
            free(t);
        }
        if (!vendor[0])
            continue;
        snprintf(p, sizeof p, "%s/device/device", base);
        t = detect_read_file(p);
        if (t) {
            char *e = t + strlen(t);
            while (e > t && (e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'))
                *--e = 0;
            snprintf(device, sizeof device, "%s", t);
            free(t);
        }
        snprintf(p, sizeof p, "%s/device/revision", base);
        t = detect_read_file(p);
        if (t) {
            char *s = t;
            while (*s == ' ' || *s == '\t' || *s == '\n')
                s++;
            char *e = s + strlen(s);
            while (e > s && (e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'))
                *--e = 0;
            if ((s[0] == '0' && (s[1] == 'x' || s[1] == 'X')))
                s += 2;
            char up[32];
            size_t i = 0;
            while (s[i] && i + 1 < sizeof up) {
                char c = s[i];
                up[i++] = (char)(c >= 'a' && c <= 'f' ? c - 32 : c);
            }
            up[i] = 0;
            snprintf(revision, sizeof revision, "%s", up);
            free(t);
        }
        out = realloc(out, (m + 1) * sizeof(GpuInfo));
        GpuInfo *g = &out[m];
        memset(g, 0, sizeof *g);
        snprintf(g->vendor, sizeof g->vendor, "%s", vendor);
        snprintf(g->device_id, sizeof g->device_id, "%s", device);
        vendor_name(vendor, g->vendor_name, sizeof g->vendor_name);
        snprintf(p, sizeof p, "%s/device/uevent", base);
        t = detect_read_file(p);
        if (t) {
            char *save = NULL;
            char *line = strtok_r(t, "\n", &save);
            while (line) {
                if (!strncmp(line, "DRIVER=", 7)) {
                    snprintf(g->driver, sizeof g->driver, "%s", line + 7);
                    break;
                }
                line = strtok_r(NULL, "\n", &save);
            }
            free(t);
        }
        if (!amdgpu_lookup(vendor, device, revision, g->model, sizeof g->model)) {
            if (ids) {
                pci_lookup(ids, vendor, device, g->model, sizeof g->model);
            }
            if (!g->model[0])
                snprintf(g->model, sizeof g->model, "%s (%s)", g->vendor_name, device);
        }
        gpu_type(base, vendor, g->driver, g->dtype, sizeof g->dtype);
        m++;
    }
    closedir(dp);
    *n = m;
    return out;
}

void detect_gpu_free(GpuInfo *g, size_t n) {
    (void)n;
    free(g);
}
