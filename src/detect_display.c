#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "detect.h"

unsigned wl_refresh_hz(const WlOutput *o) {
    if (o->refresh_mhz == 0)
        return 0;
    return (o->refresh_mhz + 500) / 1000;
}

double wl_scale_factor(const WlOutput *o) {
    if (o->width == 0 || o->height == 0)
        return 1.0;
    if (o->logical_width > 0 && o->logical_height > 0) {
        double phys = (double)o->width * (double)o->height;
        double log = (double)o->logical_width * (double)o->logical_height;
        if (log > 0.0) {
            double s = sqrt(phys / log);
            if (s > 0.1 && s < 10.0)
                return s;
        }
    }
    if (o->scale > 0)
        return (double)o->scale;
    return 1.0;
}

void detect_format_scale(double scale, char *out, size_t n) {
    char tmp[64];
    snprintf(tmp, sizeof tmp, "%.2f", scale);
    char *dot = strchr(tmp, '.');
    if (dot) {
        char *e = tmp + strlen(tmp);
        while (e > dot + 1 && e[-1] == '0')
            *--e = 0;
        if (e > dot && e[-1] == '.')
            *--e = 0;
    }
    if (!tmp[0])
        snprintf(tmp, sizeof tmp, "1");
    snprintf(out, n, "%s", tmp);
}

static void connector_name(const char *dir_name, char *out, size_t n) {
    const char *rest = dir_name;
    if (!strncmp(rest, "card", 4))
        rest += 4;
    const char *dash = strchr(rest, '-');
    const char *src = dash ? dash + 1 : rest;
    size_t sl = strlen(src);
    if (sl >= n)
        sl = n - 1;
    memcpy(out, src, sl + 1);
}

static void edid_make_model(const uint8_t *edid, size_t len, char *make, size_t make_n,
                            char *model, size_t model_n) {
    make[0] = 0;
    model[0] = 0;
    if (len >= 10) {
        unsigned word = ((unsigned)edid[8] << 8) | edid[9];
        char code[8];
        size_t c = 0;
        unsigned shifts[3] = {10, 5, 0};
        for (int i = 0; i < 3; i++) {
            unsigned v = (word >> shifts[i]) & 31;
            if (v >= 1 && v <= 26 && c + 1 < sizeof code)
                code[c++] = (char)('A' + v - 1);
        }
        code[c] = 0;
        snprintf(make, make_n, "%.7s", code);
    }
    for (int k = 0; k < 4; k++) {
        size_t o = 54 + (size_t)k * 18;
        if (len < o + 18)
            break;
        const uint8_t *d = edid + o;
        if (d[0] == 0 && d[1] == 0 && d[2] == 0 && d[3] == 0xfc) {
            char tmp[16];
            size_t m = 0;
            for (int i = 5; i < 18 && m + 1 < sizeof tmp; i++)
                tmp[m++] = (char)d[i];
            tmp[m] = 0;
            char *s = tmp;
            while (*s == '\n' || *s == '\r' || *s == ' ' || *s == 0)
                s++;
            char *e = s + strlen(s);
            while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == 0))
                *--e = 0;
            if (*s) {
                snprintf(model, model_n, "%s", s);
                break;
            }
        }
    }
}

static unsigned descriptor_refresh(const uint8_t *d, size_t len, unsigned width,
                                   unsigned height) {
    if (len < 12)
        return 0;
    unsigned clock = (unsigned)d[0] | ((unsigned)d[1] << 8);
    if (clock == 0)
        return 0;
    unsigned hact = d[2] | (((unsigned)(d[4] >> 4)) << 8);
    unsigned vact = d[5] | (((unsigned)(d[7] >> 4)) << 8);
    if (hact != width || vact != height)
        return 0;
    unsigned htot = hact + (d[3] | (((unsigned)(d[4] & 0xF)) << 8));
    unsigned vtot = vact + (d[6] | (((unsigned)(d[7] & 0xF)) << 8));
    if (htot == 0 || vtot == 0)
        return 0;
    return (unsigned)((unsigned long long)clock * 10000 / ((unsigned long long)htot * vtot));
}

static void edid_timing(const uint8_t *edid, size_t len, unsigned width, unsigned height,
                        unsigned *refresh, unsigned *size_in) {
    unsigned r = 0;
    unsigned s = 0;
    if (len >= 23) {
        double w = (double)edid[21], h = (double)edid[22];
        if (w > 0.0 && h > 0.0)
            s = (unsigned)(sqrt(w * w + h * h) / 2.54 + 0.5);
    }
    for (int k = 0; k < 4; k++) {
        size_t o = 54 + (size_t)k * 18;
        if (len < o + 12)
            break;
        unsigned v = descriptor_refresh(edid + o, len - o, width, height);
        if (v > r)
            r = v;
    }
    size_t off = 128;
    while (len >= off + 128) {
        const uint8_t *block = edid + off;
        if (block[0] == 0x02 && block[2] >= 4) {
            size_t d = block[2];
            while (d + 18 <= 128) {
                unsigned v = descriptor_refresh(block + d, 128 - d, width, height);
                if (v > r)
                    r = v;
                d += 18;
            }
        }
        off += 128;
    }
    *refresh = r;
    *size_in = s;
}

static void connector_type(const char *connector, char *out, size_t n) {
    char c[64];
    size_t i = 0;
    while (connector[i] && i + 1 < sizeof c) {
        char ch = connector[i];
        c[i++] = (char)(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
    }
    c[i] = 0;
    if (!strncmp(c, "EDP", 3) || !strncmp(c, "LVDS", 4) || !strncmp(c, "DSI", 3)) {
        snprintf(out, n, "Internal");
        return;
    }
    if (!strncmp(c, "DP", 2) || !strncmp(c, "HDMI", 4) || !strncmp(c, "DVI", 3) ||
        !strncmp(c, "VGA", 3) || !strncmp(c, "COMPOSITE", 9) || !strncmp(c, "SVIDEO", 6) ||
        !strncmp(c, "COMPONENT", 9) || !strncmp(c, "TV", 2)) {
        snprintf(out, n, "External");
        return;
    }
    out[0] = 0;
}

static const WlOutput *match_live_output(const WlOutput *live, size_t n, const char *connector,
                                         const uint8_t *edid, size_t elen) {
    size_t i;
    if (n == 0)
        return NULL;
    for (i = 0; i < n; i++) {
        if (live[i].name[0] && !strcmp(live[i].name, connector))
            return &live[i];
    }
    char make[64], model[256];
    edid_make_model(edid, elen, make, sizeof make, model, sizeof model);
    char mkl[64], mdl[256];
    size_t k = 0;
    while (make[k] && k + 1 < sizeof mkl) {
        char c = make[k];
        mkl[k++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    mkl[k] = 0;
    k = 0;
    while (model[k] && k + 1 < sizeof mdl) {
        char c = model[k];
        mdl[k++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    mdl[k] = 0;
    const WlOutput *best = NULL;
    unsigned best_score = 0;
    for (i = 0; i < n; i++) {
        unsigned score = 0;
        char omake[128], omodel[256];
        size_t q = 0;
        while (live[i].make[q] && q + 1 < sizeof omake) {
            char c = live[i].make[q];
            omake[q++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        omake[q] = 0;
        q = 0;
        while (live[i].model[q] && q + 1 < sizeof omodel) {
            char c = live[i].model[q];
            omodel[q++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        omodel[q] = 0;
        if (mkl[0]) {
            if ((strstr(omake, mkl)) || (omake[0] && strstr(mkl, omake)))
                score += 2;
        }
        if (mdl[0]) {
            if ((strstr(omodel, mdl)) || (omodel[0] && strstr(mdl, omodel)))
                score += 3;
        }
        if (score > best_score) {
            best_score = score;
            best = &live[i];
        }
    }
    if (best)
        return best;
    if (n == 1)
        return &live[0];
    return NULL;
}

void detect_display(DisplayInfo **out, size_t *n) {
    DisplayInfo *res = NULL;
    size_t m = 0;
    *out = NULL;
    *n = 0;
    size_t nl = 0;
    WlOutput *live = wl_query_outputs(&nl);
    DIR *dp = opendir("/sys/class/drm");
    if (!dp) {
        wl_outputs_free(live, nl);
        return;
    }
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        const char *dir_name = de->d_name;
        if (strncmp(dir_name, "card", 4) != 0)
            continue;
        if (!strchr(dir_name + 4, '-'))
            continue;
        char path[512];
        snprintf(path, sizeof path, "/sys/class/drm/%s", dir_name);
        char sp[576];
        snprintf(sp, sizeof sp, "%s/status", path);
        char *status = detect_read_file(sp);
        int connected = 0;
        if (status) {
            char *e = status + strlen(status);
            while (e > status && (e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'))
                *--e = 0;
            char l[32];
            size_t i = 0;
            while (status[i] && i + 1 < sizeof l) {
                char c = status[i];
                l[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
            }
            l[i] = 0;
            connected = !strcmp(l, "connected");
            free(status);
        }
        if (!connected)
            continue;
        snprintf(sp, sizeof sp, "%s/modes", path);
        char *modes = detect_read_file(sp);
        if (!modes)
            continue;
        char *save = NULL;
        char *line = strtok_r(modes, "\n", &save);
        while (line && !*line)
            line = strtok_r(NULL, "\n", &save);
        unsigned width = 0, height = 0;
        if (line) {
            char *x = strchr(line, 'x');
            if (x) {
                *x = 0;
                width = (unsigned)strtoul(line, NULL, 10);
                height = (unsigned)strtoul(x + 1, NULL, 10);
            }
        }
        free(modes);
        if (width == 0 || height == 0)
            continue;
        char connector[128];
        connector_name(dir_name, connector, sizeof connector);
        snprintf(sp, sizeof sp, "%s/edid", path);
        static const uint8_t empty_edid[1] = {0};
        FILE *f = fopen(sp, "rb");
        uint8_t *edid = NULL;
        size_t elen = 0;
        if (f) {
            size_t cap = 512;
            edid = malloc(cap);
            size_t k;
            while ((k = fread(edid + elen, 1, cap - elen, f)) > 0) {
                elen += k;
                if (elen >= 65536)
                    break;
                if (elen == cap) {
                    cap *= 2;
                    edid = realloc(edid, cap);
                }
            }
            fclose(f);
            if (elen == 0) {
                free(edid);
                edid = NULL;
            }
        }
        unsigned refresh = 0;
        char model[256] = "";
        double scale = 1.0;
        const WlOutput *wl = match_live_output(live, nl, connector, (edid ? edid : empty_edid),
                                               elen);
        if (wl) {
            if (wl->width > 0 && wl->height > 0) {
                width = wl->width;
                height = wl->height;
            }
            refresh = wl_refresh_hz(wl);
            if (wl->model[0])
                snprintf(model, sizeof model, "%s", wl->model);
            else if (wl->make[0])
                snprintf(model, sizeof model, "%s", wl->make);
            scale = wl_scale_factor(wl);
        }
        if (!model[0]) {
            char make[64], em[256];
            edid_make_model((edid ? edid : empty_edid), elen, make, sizeof make, em,
                            sizeof em);
            if (em[0])
                snprintf(model, sizeof model, "%s", em);
            else if (make[0])
                snprintf(model, sizeof model, "%s", make);
        }
        if (!model[0])
            snprintf(model, sizeof model, "%s", connector);
        unsigned er = 0, size_in = 0;
        edid_timing((edid ? edid : empty_edid), elen, width, height, &er, &size_in);
        if (refresh == 0)
            refresh = er;
        free(edid);
        res = realloc(res, (m + 1) * sizeof(DisplayInfo));
        res[m].width = width;
        res[m].height = height;
        res[m].refresh_rate = refresh;
        res[m].size_in = size_in;
        connector_type(connector, res[m].dtype, sizeof res[m].dtype);
        snprintf(res[m].name, sizeof res[m].name, "%.*s",
                 (int)(sizeof res[m].name - 1), dir_name);
        snprintf(res[m].model, sizeof res[m].model, "%s", model);
        res[m].scale = scale;
        m++;
    }
    closedir(dp);
    wl_outputs_free(live, nl);
    *out = res;
    *n = m;
}

void detect_display_free(DisplayInfo *d, size_t n) {
    (void)n;
    free(d);
}
