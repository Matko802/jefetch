#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "json.h"
#include "pa.h"
#include "sharkvis_sync.h"

#define SV_DEFAULT_BEAT_DEPTH 0.6f
#define SV_MAX_BEAT_DEPTH 0.9f
#define BEAT_RATE 8000
#define BEAT_WINDOW 256

SharkvisMode sv_mode_parse(const char *v) {
    while (*v == ' ' || *v == '\t')
        v++;
    char l[32];
    size_t i = 0;
    while (v[i] && i + 1 < sizeof l) {
        char c = v[i];
        l[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    l[i] = 0;
    char *e = l + strlen(l);
    while (e > l && (e[-1] == ' ' || e[-1] == '\t'))
        *--e = 0;
    if (!strcmp(l, "on") || !strcmp(l, "true") || !strcmp(l, "1") ||
        !strcmp(l, "yes") || !strcmp(l, "enable") || !strcmp(l, "enabled"))
        return SVM_ON;
    if (!strcmp(l, "off") || !strcmp(l, "false") || !strcmp(l, "0") ||
        !strcmp(l, "no") || !strcmp(l, "disable") || !strcmp(l, "disabled"))
        return SVM_OFF;
    if (!strcmp(l, "auto"))
        return SVM_AUTO;
    return SVM_AUTO;
}

int sv_mode_enabled(SharkvisMode m, int running) {
    if (m == SVM_OFF)
        return 0;
    return running;
}

float beat_speed_mult(float beat, float depth) {
    if (depth < 0.0f)
        depth = 0.0f;
    if (depth > SV_MAX_BEAT_DEPTH)
        depth = SV_MAX_BEAT_DEPTH;
    if (beat < 0.0f)
        beat = 0.0f;
    if (beat > 1.0f)
        beat = 1.0f;
    float v = 1.0f - depth * beat;
    if (v < 0.1f)
        v = 0.1f;
    if (v > 1.0f)
        v = 1.0f;
    return v;
}

void live_frame_free_contents(LiveFrame *f) {
    for (size_t i = 0; i < f->nglyphs; i++)
        free(f->glyphs[i]);
    free(f->glyphs);
    f->glyphs = NULL;
    f->nglyphs = 0;
}

Rgb sv_lerp_rgb(Rgb lo, Rgb hi, float t) {
    Rgb o;
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    o.r = (uint8_t)(lo.r + (hi.r - lo.r) * t + 0.5f);
    o.g = (uint8_t)(lo.g + (hi.g - lo.g) * t + 0.5f);
    o.b = (uint8_t)(lo.b + (hi.b - lo.b) * t + 0.5f);
    return o;
}

static int cmdline_has_raw(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    char buf[1024];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    size_t i = 0;
    while (i < n) {
        size_t s = i;
        while (i < n && buf[i])
            i++;
        if (i - s == 5 && !memcmp(buf + s, "--raw", 5))
            return 1;
        i++;
    }
    return 0;
}

int sv_is_running(void) {
    const char *v = getenv("JEFETCH_SHARKVIS_RUNNING");
    if (v) {
        char l[16];
        size_t i = 0;
        while (v[i] && i + 1 < sizeof l) {
            char c = v[i];
            l[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        l[i] = 0;
        if (!strcmp(l, "1") || !strcmp(l, "true") || !strcmp(l, "yes") || !strcmp(l, "on"))
            return 1;
        if (!strcmp(l, "0") || !strcmp(l, "false") || !strcmp(l, "no") || !strcmp(l, "off"))
            return 0;
    }
    DIR *dp = opendir("/proc");
    if (!dp)
        return 0;
    struct dirent *de;
    int found = 0;
    while ((de = readdir(dp)) != NULL && !found) {
        size_t el = strlen(de->d_name);
        if (el == 0 || el >= 16)
            continue;
        char pid[16];
        memcpy(pid, de->d_name, el + 1);
        size_t i = 0;
        int ok = pid[0] != 0;
        while (pid[i]) {
            if (pid[i] < '0' || pid[i] > '9') {
                ok = 0;
                break;
            }
            i++;
        }
        if (!ok)
            continue;
        char path[64];
        snprintf(path, sizeof path, "/proc/%s/comm", pid);
        char *comm = NULL;
        FILE *f = fopen(path, "r");
        if (f) {
            char tmp[64];
            if (fgets(tmp, sizeof tmp, f)) {
                char *e = tmp + strlen(tmp);
                while (e > tmp && (e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'))
                    *--e = 0;
                comm = strdup(tmp);
            }
            fclose(f);
        }
        if (!comm)
            continue;
        int match = !strcmp(comm, "sharkvis");
        free(comm);
        if (match) {
            snprintf(path, sizeof path, "/proc/%s/cmdline", pid);
            if (!cmdline_has_raw(path))
                found = 1;
        }
    }
    closedir(dp);
    return found;
}

char **sv_config_paths(size_t *n) {
    char **out = NULL;
    size_t m = 0;
    *n = 0;
    const char *e = getenv("JEFETCH_SHARKVIS_CONFIG");
    if (e && *e) {
        const char *s = e;
        while (*s == ' ' || *s == '\t')
            s++;
        if (*s) {
            out = malloc(sizeof(char *));
            out[m++] = strdup(e);
            *n = m;
            return out;
        }
    }
    e = getenv("SHARKVIS_CONFIG");
    if (e && *e) {
        const char *s = e;
        while (*s == ' ' || *s == '\t')
            s++;
        if (*s) {
            out = realloc(out, (m + 1) * sizeof(char *));
            out[m++] = strdup(e);
        }
    }
    e = getenv("HOME");
    if (e) {
        char p[1152];
        /* JSONC first: sharkvis prefers config.jsonc for new files. */
        snprintf(p, sizeof p, "%s/.config/sharkvis/config.jsonc", e);
        out = realloc(out, (m + 1) * sizeof(char *));
        out[m++] = strdup(p);
        snprintf(p, sizeof p, "%s/.config/sharkvis/config.toml", e);
        out = realloc(out, (m + 1) * sizeof(char *));
        out[m++] = strdup(p);
    }
    out = realloc(out, (m + 1) * sizeof(char *));
    out[m++] = strdup("./config.jsonc");
    out = realloc(out, (m + 1) * sizeof(char *));
    out[m++] = strdup("./config.toml");
    *n = m;
    return out;
}

void sv_free_paths(char **p, size_t n) {
    for (size_t i = 0; i < n; i++)
        free(p[i]);
    free(p);
}

void sv_free_strs(char **p, size_t n) {
    for (size_t i = 0; i < n; i++)
        free(p[i]);
    free(p);
}

static char *read_file_all(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    size_t k;
    while ((k = fread(buf + len, 1, cap - len - 1, f)) > 0) {
        len += k;
        if (len + 1 >= cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
    }
    fclose(f);
    buf[len] = 0;
    return buf;
}

int sv_parse_color(const char *s, Rgb *out) {
    while (*s == ' ' || *s == '\t')
        s++;
    size_t l = strlen(s);
    while (l > 0 && (s[l - 1] == ' ' || s[l - 1] == '\t'))
        l--;
    if (l == 0)
        return 0;
    char t[64];
    if (l >= sizeof t)
        return 0;
    memcpy(t, s, l);
    t[l] = 0;
    if (strchr(t, ',')) {
        char *save = NULL;
        char *a = strtok_r(t, ",", &save);
        char *b = strtok_r(NULL, ",", &save);
        char *c = strtok_r(NULL, ",", &save);
        char *d = strtok_r(NULL, ",", &save);
        if (!a || !b || !c || d)
            return 0;
        char *e1, *e2, *e3;
        long r = strtol(a, &e1, 10);
        long g = strtol(b, &e2, 10);
        long bl = strtol(c, &e3, 10);
        if (*e1 || *e2 || *e3 || r < 0 || r > 255 || g < 0 || g > 255 || bl < 0 || bl > 255)
            return 0;
        out->r = (uint8_t)r;
        out->g = (uint8_t)g;
        out->b = (uint8_t)bl;
        return 1;
    }
    const char *hex = t[0] == '#' ? t + 1 : t;
    if (strlen(hex) == 6) {
        int ok = 1;
        for (int i = 0; i < 6; i++) {
            char c = hex[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                ok = 0;
                break;
            }
        }
        if (ok) {
            unsigned long v = strtoul(hex, NULL, 16);
            out->r = (uint8_t)((v >> 16) & 0xff);
            out->g = (uint8_t)((v >> 8) & 0xff);
            out->b = (uint8_t)(v & 0xff);
            return 1;
        }
    }
    char low[64];
    size_t i = 0;
    while (t[i] && i + 1 < sizeof low) {
        char c = t[i];
        low[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    low[i] = 0;
    if (!strcmp(low, "white")) {
        *out = (Rgb){255, 255, 255};
        return 1;
    }
    if (!strcmp(low, "red")) {
        *out = (Rgb){255, 0, 0};
        return 1;
    }
    if (!strcmp(low, "green")) {
        *out = (Rgb){0, 255, 0};
        return 1;
    }
    if (!strcmp(low, "blue")) {
        *out = (Rgb){0, 0, 255};
        return 1;
    }
    if (!strcmp(low, "yellow")) {
        *out = (Rgb){255, 255, 0};
        return 1;
    }
    if (!strcmp(low, "magenta") || !strcmp(low, "purple")) {
        *out = (Rgb){255, 0, 255};
        return 1;
    }
    if (!strcmp(low, "cyan")) {
        *out = (Rgb){0, 255, 255};
        return 1;
    }
    if (!strcmp(low, "orange")) {
        *out = (Rgb){255, 136, 0};
        return 1;
    }
    if (!strcmp(low, "lime")) {
        *out = (Rgb){136, 255, 0};
        return 1;
    }
    if (!strcmp(low, "teal")) {
        *out = (Rgb){0, 255, 136};
        return 1;
    }
    if (!strcmp(low, "pink")) {
        *out = (Rgb){255, 0, 136};
        return 1;
    }
    if (!strcmp(low, "gray") || !strcmp(low, "grey")) {
        *out = (Rgb){136, 136, 136};
        return 1;
    }
    if (!strcmp(low, "black")) {
        *out = (Rgb){0, 0, 0};
        return 1;
    }
    return 0;
}

static int text_is_jsonc(const char *text) {
    const unsigned char *p = (const unsigned char *)text;
    if (p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF)
        p += 3;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    return *p == '{';
}

/* JSONC branch: same schema sharkvis writes
 * ({"color": {"gradient_low": ..., "gradient_high": ...}}). */
static int parse_cfg_gradients_jsonc(const char *text, Rgb *lo, Rgb *hi) {
    char err[256];
    JsonValue *root = json_parse(text, err, sizeof err);
    if (!root)
        return 0;
    int have_lo = 0, have_hi = 0;
    const JsonValue *color = json_get(root, "color");
    if (color && color->type == JV_OBJ) {
        const char *slo = json_str(json_get(color, "gradient_low"));
        const char *shi = json_str(json_get(color, "gradient_high"));
        if (slo && sv_parse_color(slo, lo))
            have_lo = 1;
        if (shi && sv_parse_color(shi, hi))
            have_hi = 1;
    }
    json_free(root);
    if (have_lo && have_hi)
        return 1;
    if (have_lo && !have_hi) {
        *hi = *lo;
        return 1;
    }
    if (!have_lo && have_hi) {
        *lo = *hi;
        return 1;
    }
    return 0;
}

static int parse_cfg_gradients(const char *text, Rgb *lo, Rgb *hi) {
    if (text_is_jsonc(text))
        return parse_cfg_gradients_jsonc(text, lo, hi);
    int have_lo = 0, have_hi = 0;
    char section[64] = "general";
    char *dup = strdup(text);
    char *save = NULL;
    char *line = strtok_r(dup, "\n", &save);
    while (line) {
        char *t = line;
        while (*t == ' ' || *t == '\t')
            t++;
        char *e = t + strlen(t);
        while (e > t && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
            *--e = 0;
        if (!*t || *t == ';' || *t == '#') {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        if (*t == '[') {
            char *end = strchr(t, ']');
            if (end)
                *end = 0;
            char *nm = t + 1;
            while (*nm == ' ' || *nm == '\t')
                nm++;
            size_t i = 0;
            while (nm[i] && i + 1 < sizeof section) {
                char c = nm[i];
                section[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
            }
            section[i] = 0;
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        char *eq = strchr(t, '=');
        if (!eq) {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        *eq = 0;
        char *k = t;
        char *ke = k + strlen(k);
        while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t'))
            *--ke = 0;
        char kl[64];
        size_t i = 0;
        while (k[i] && i + 1 < sizeof kl) {
            char c = k[i];
            kl[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        kl[i] = 0;
        char *v = eq + 1;
        while (*v == ' ' || *v == '\t')
            v++;
        char *semi = strchr(v, ';');
        if (semi)
            *semi = 0;
        e = v + strlen(v);
        while (e > v && (e[-1] == ' ' || e[-1] == '\t'))
            *--e = 0;
        if (!strcmp(section, "color")) {
            if (!strcmp(kl, "gradient_low")) {
                if (sv_parse_color(v, lo))
                    have_lo = 1;
            } else if (!strcmp(kl, "gradient_high")) {
                if (sv_parse_color(v, hi))
                    have_hi = 1;
            }
        }
        line = strtok_r(NULL, "\n", &save);
    }
    free(dup);
    if (have_lo && have_hi)
        return 1;
    if (have_lo && !have_hi) {
        *hi = *lo;
        return 1;
    }
    if (!have_lo && have_hi) {
        *lo = *hi;
        return 1;
    }
    return 0;
}

int sv_gradient_colors(Rgb *lo, Rgb *hi) {
    size_t n = 0;
    char **paths = sv_config_paths(&n);
    int found = 0;
    for (size_t i = 0; i < n && !found; i++) {
        char *text = read_file_all(paths[i]);
        if (!text)
            continue;
        if (parse_cfg_gradients(text, lo, hi))
            found = 1;
        free(text);
    }
    sv_free_paths(paths, n);
    return found;
}

/* Split a chars string into one malloc'd glyph per codepoint.
 * Returns NULL (with *n == 0) for blank/empty ramps. */
static char **ramp_from_chars(const char *v, size_t *n) {
    size_t m = 0;
    const char *p = v;
    size_t vrem = strlen(v);
    int allblank = 1;
    char **ramp = NULL;
    while (vrem > 0 && *p) {
        size_t kl2;
        unsigned char c = (unsigned char)*p;
        if (c < 0x80)
            kl2 = 1;
        else if ((c & 0xE0) == 0xC0)
            kl2 = 2;
        else if ((c & 0xF0) == 0xE0)
            kl2 = 3;
        else
            kl2 = 4;
        /* Truncated multibyte at end: stop instead of over-reading. */
        if (kl2 > vrem)
            break;
        int valid = 1;
        for (size_t k = 1; k < kl2; k++) {
            if ((p[k] & 0xC0) != 0x80) {
                valid = 0;
                break;
            }
        }
        if (!valid) {
            p += 1;
            vrem -= 1;
            continue;
        }
        char *s = malloc(kl2 + 1);
        if (!s)
            break;
        memcpy(s, p, kl2);
        s[kl2] = 0;
        if (strcmp(s, " ") && strcmp(s, "\t"))
            allblank = 0;
        char **nr = realloc(ramp, (m + 1) * sizeof(char *));
        if (!nr) {
            free(s);
            break;
        }
        ramp = nr;
        ramp[m++] = s;
        p += kl2;
        vrem -= kl2;
        if (m >= 64)
            break;
    }
    if (m > 0 && !allblank) {
        *n = m;
        return ramp;
    }
    for (size_t k2 = 0; k2 < m; k2++)
        free(ramp[k2]);
    free(ramp);
    *n = 0;
    return NULL;
}

/* JSONC branch: {"visualizer": {"chars": "..."}}. */
static char **glyph_ramp_jsonc(const char *text, size_t *n) {
    char err[256];
    JsonValue *root = json_parse(text, err, sizeof err);
    if (!root)
        return NULL;
    char **out = NULL;
    const JsonValue *vis = json_get(root, "visualizer");
    if (vis && vis->type == JV_OBJ) {
        const char *v = json_str(json_get(vis, "chars"));
        if (v)
            out = ramp_from_chars(v, n);
    }
    json_free(root);
    return out;
}

char **sv_glyph_ramp(size_t *n) {
    size_t np = 0;
    char **paths = sv_config_paths(&np);
    char **out = NULL;
    *n = 0;
    for (size_t i = 0; i < np && !*n; i++) {
        char *text = read_file_all(paths[i]);
        if (!text)
            continue;
        if (text_is_jsonc(text)) {
            char **r = glyph_ramp_jsonc(text, n);
            if (r)
                out = r;
            free(text);
            continue;
        }
        char section[64] = "general";
        char *dup = strdup(text);
        char *save = NULL;
        char *line = strtok_r(dup, "\n", &save);
        while (line) {
            char *t = line;
            while (*t == ' ' || *t == '\t')
                t++;
            char *e = t + strlen(t);
            while (e > t && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
                *--e = 0;
            if (!*t || *t == ';' || *t == '#') {
                line = strtok_r(NULL, "\n", &save);
                continue;
            }
            if (*t == '[') {
                char *end = strchr(t, ']');
                if (end)
                    *end = 0;
                char *nm = t + 1;
                while (*nm == ' ' || *nm == '\t')
                    nm++;
                size_t k = 0;
                while (nm[k] && k + 1 < sizeof section) {
                    char c = nm[k];
                    section[k++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
                }
                section[k] = 0;
                line = strtok_r(NULL, "\n", &save);
                continue;
            }
            if (strcmp(section, "visualizer")) {
                line = strtok_r(NULL, "\n", &save);
                continue;
            }
            char *eq = strchr(t, '=');
            if (!eq) {
                line = strtok_r(NULL, "\n", &save);
                continue;
            }
            *eq = 0;
            char *k = t;
            char *ke = k + strlen(k);
            while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t'))
                *--ke = 0;
            char kl[32];
            size_t q = 0;
            while (k[q] && q + 1 < sizeof kl) {
                char c = k[q];
                kl[q++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
            }
            kl[q] = 0;
            if (strcmp(kl, "chars")) {
                line = strtok_r(NULL, "\n", &save);
                continue;
            }
            char *v = eq + 1;
            while (*v == ' ' || *v == '\t')
                v++;
            size_t m = 0;
            char **ramp = ramp_from_chars(v, &m);
            if (ramp) {
                out = ramp;
                *n = m;
            }
            break;
        }
        free(dup);
        free(text);
    }
    sv_free_paths(paths, np);
    return out;
}

static int parse_level(const char *v, float *out) {
    char *end;
    float f = strtof(v, &end);
    if (end == v)
        return 0;
    if (f != f)
        return 0;
    if (f > 1.0f)
        f = f / 100.0f;
    if (f < 0.0f)
        f = 0.0f;
    if (f > 1.0f)
        f = 1.0f;
    *out = f;
    return 1;
}

static int parse_beat(const char *v, float *out) {
    char l[16];
    size_t i = 0;
    while (v[i] && i + 1 < sizeof l) {
        char c = v[i];
        l[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    l[i] = 0;
    if (!strcmp(l, "true") || !strcmp(l, "yes") || !strcmp(l, "on") || !strcmp(l, "hit")) {
        *out = 1.0f;
        return 1;
    }
    if (!strcmp(l, "false") || !strcmp(l, "no") || !strcmp(l, "off")) {
        *out = 0.0f;
        return 1;
    }
    return parse_level(v, out);
}

void sv_parse_state_text(const char *text, LiveState *out) {
    memset(out, 0, sizeof *out);
    size_t cap = 32, n = 0;
    char **parts = malloc(cap * sizeof(char *));
    char *dup = strdup(text);
    for (char *p = dup; *p; p++) {
        if (*p == ';' || *p == '\n' || *p == '\r')
            *p = ' ';
    }
    char *save = NULL;
    char *chunk = strtok_r(dup, " \t", &save);
    while (chunk) {
        char *c2 = strchr(chunk, ',');
        while (c2) {
            *c2 = 0;
            if (*chunk) {
                if (n >= cap) {
                    cap *= 2;
                    parts = realloc(parts, cap * sizeof(char *));
                }
                parts[n++] = chunk;
            }
            chunk = c2 + 1;
        }
        if (*chunk) {
            if (n >= cap) {
                cap *= 2;
                parts = realloc(parts, cap * sizeof(char *));
            }
            parts[n++] = chunk;
        }
        chunk = strtok_r(NULL, " \t", &save);
    }
    size_t tc = n;
    char **toks = malloc((tc + 1) * sizeof(char *));
    size_t tn = 0;
    for (size_t i = 0; i < n; i++) {
        char *p = parts[i];
        char *sep = strchr(p, '=');
        if (!sep)
            sep = strchr(p, ':');
        if (sep) {
            *sep = 0;
            char *k = p;
            char *ke = k + strlen(k);
            while (ke > k && (*ke == 0 || *(ke - 1) == ' ' || *(ke - 1) == '\t'))
                ke--;
            char kl[32];
            size_t q = 0;
            while (k[q] && q + 1 < sizeof kl) {
                char c = k[q];
                kl[q++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
            }
            kl[q] = 0;
            char *v = sep + 1;
            while (*v == ' ' || *v == '\t')
                v++;
            int is_color = !strcmp(kl, "color") || !strcmp(kl, "colour") || !strcmp(kl, "rgb");
            int bare = v[0] >= '0' && v[0] <= '9' && !strchr(v, '=') && !strchr(v, ':');
            if (is_color && bare && i + 2 < n) {
                int b1 = parts[i + 1][0] >= '0' && parts[i + 1][0] <= '9';
                int b2 = parts[i + 2][0] >= '0' && parts[i + 2][0] <= '9';
                if (b1 && b2) {
                    char tmp[64];
                    snprintf(tmp, sizeof tmp, "%s=%s,%s,%s", kl, v, parts[i + 1],
                             parts[i + 2]);
                    toks[tn++] = strdup(tmp);
                    i += 2;
                    continue;
                }
            }
            char tmp[128];
            snprintf(tmp, sizeof tmp, "%s=%s", kl, v);
            toks[tn++] = strdup(tmp);
        } else {
            char kl[32];
            size_t q = 0;
            while (p[q] && q + 1 < sizeof kl) {
                char c = p[q];
                kl[q++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
            }
            kl[q] = 0;
            int is_color = !strcmp(kl, "color") || !strcmp(kl, "colour") || !strcmp(kl, "rgb");
            if (is_color && i + 3 < n) {
                int ok = 1;
                for (int k2 = 1; k2 <= 3; k2++) {
                    char *t = parts[i + k2];
                    int j = 0;
                    while (t[j] >= '0' && t[j] <= '9')
                        j++;
                    if (t[j] || j == 0)
                        ok = 0;
                }
                if (ok) {
                    char tmp[64];
                    snprintf(tmp, sizeof tmp, "%s=%s,%s,%s", kl, parts[i + 1], parts[i + 2],
                             parts[i + 3]);
                    toks[tn++] = strdup(tmp);
                    i += 3;
                    continue;
                }
            }
            toks[tn++] = strdup(p);
        }
    }
    for (size_t i = 0; i < tn; i++) {
        char *eq = strchr(toks[i], '=');
        if (!eq)
            continue;
        *eq = 0;
        char *v = eq + 1;
        while (*v == ' ' || *v == '\t')
            v++;
        if (!*v)
            continue;
        float f;
        Rgb c;
        if ((!strcmp(toks[i], "color") || !strcmp(toks[i], "colour") ||
             !strcmp(toks[i], "rgb")) &&
            sv_parse_color(v, &c)) {
            out->has_color = 1;
            out->color = c;
        } else if ((!strcmp(toks[i], "color_low") || !strcmp(toks[i], "colour_low") ||
                    !strcmp(toks[i], "glow")) &&
                   sv_parse_color(v, &c)) {
            out->has_glow = 1;
            out->glow = c;
        } else if ((!strcmp(toks[i], "color_high") || !strcmp(toks[i], "colour_high") ||
                    !strcmp(toks[i], "ghigh")) &&
                   sv_parse_color(v, &c)) {
            out->has_ghigh = 1;
            out->ghigh = c;
        } else if ((!strcmp(toks[i], "energy") || !strcmp(toks[i], "level") ||
                    !strcmp(toks[i], "volume") || !strcmp(toks[i], "rms")) &&
                   parse_level(v, &f)) {
            out->has_energy = 1;
            out->energy = f;
        } else if ((!strcmp(toks[i], "bass") || !strcmp(toks[i], "low")) &&
                   parse_level(v, &f)) {
            out->has_bass = 1;
            out->bass = f;
        } else if (!strcmp(toks[i], "left") && parse_level(v, &f)) {
            out->has_left = 1;
            out->left = f;
        } else if (!strcmp(toks[i], "right") && parse_level(v, &f)) {
            out->has_right = 1;
            out->right = f;
        } else if ((!strcmp(toks[i], "beat") || !strcmp(toks[i], "kick") ||
                    !strcmp(toks[i], "onset")) &&
                   parse_beat(v, &f)) {
            out->has_beat = 1;
            out->beat = f;
        } else if ((!strcmp(toks[i], "started") || !strcmp(toks[i], "session") ||
                    !strcmp(toks[i], "session_started"))) {
            char *end;
            unsigned long long ms = strtoull(v, &end, 10);
            if (end != v) {
                out->has_started = 1;
                out->started = ms;
            }
        }
    }
    for (size_t i = 0; i < tn; i++)
        free(toks[i]);
    free(toks);
    free(parts);
    free(dup);
}

static unsigned long long stale_after_ms(void) {
    const char *v = getenv("JEFETCH_SHARKVIS_STALE_MS");
    if (v) {
        char *end;
        unsigned long long ms = strtoull(v, &end, 10);
        if (end != v)
            return ms > 0 ? ms : 1;
    }
    return 1000;
}

static int file_fresh(const char *path, unsigned long long stale_ms, uint64_t *mtime_ms,
                      size_t *size) {
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    long long age = (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000 -
                    ((long long)st.st_mtime * 1000 + st.st_mtim.tv_nsec / 1000000);
    if (age < 0 || (unsigned long long)age > stale_ms)
        return 0;
    *mtime_ms = (uint64_t)st.st_mtime * 1000 + (uint64_t)st.st_mtim.tv_nsec / 1000000;
    *size = (size_t)st.st_size;
    return 1;
}

static void collect_state_paths(char ***out, size_t *n) {
    *out = NULL;
    *n = 0;
    const char *e = getenv("JEFETCH_SHARKVIS_STATE");
    if (e) {
        const char *s = e;
        while (*s == ' ' || *s == '\t')
            s++;
        if (*s) {
            *out = malloc(sizeof(char *));
            (*out)[(*n)++] = strdup(e);
            return;
        }
    }
    char dirs[2][1024];
    int nd = 0;
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (rt && *rt) {
        size_t l = strlen(rt);
        while (l > 0 && rt[l - 1] == '/')
            l--;
        snprintf(dirs[nd], sizeof dirs[0], "%.*s/sharkvis", (int)l, rt);
        nd++;
    }
    unsigned uid = (unsigned)getuid();
    char rundir[256];
    snprintf(rundir, sizeof rundir, "/run/user/%u/sharkvis", uid);
    int dup = 0;
    for (int i = 0; i < nd; i++) {
        if (!strcmp(dirs[i], rundir))
            dup = 1;
    }
    if (!dup && nd < 2) {
        snprintf(dirs[nd], sizeof dirs[0], "%s", rundir);
        nd++;
    }
    for (int i = 0; i < nd; i++) {
        char p[1152];
        snprintf(p, sizeof p, "%.1023s/state", dirs[i]);
        *out = realloc(*out, (*n + 1) * sizeof(char *));
        (*out)[(*n)++] = strdup(p);
        DIR *dp = opendir(dirs[i]);
        if (dp) {
            char *names[256];
            size_t nn = 0;
            struct dirent *de;
            while ((de = readdir(dp)) != NULL && nn < 256) {
                size_t l = strlen(de->d_name);
                if (strncmp(de->d_name, "state-", 6) != 0)
                    continue;
                if (l > 4 && !strcmp(de->d_name + l - 4, ".tmp"))
                    continue;
                names[nn++] = strdup(de->d_name);
            }
            closedir(dp);
            for (size_t a = 0; a < nn; a++) {
                for (size_t b = a + 1; b < nn; b++) {
                    if (strcmp(names[a], names[b]) > 0) {
                        char *t = names[a];
                        names[a] = names[b];
                        names[b] = t;
                    }
                }
            }
            for (size_t k = 0; k < nn; k++) {
                size_t dl = strlen(dirs[i]);
                while (dl > 0 && dirs[i][dl - 1] == '/')
                    dl--;
                char fp[1280];
                snprintf(fp, sizeof fp, "%.*s/%s", (int)dl, dirs[i], names[k]);
                *out = realloc(*out, (*n + 1) * sizeof(char *));
                (*out)[(*n)++] = strdup(fp);
                free(names[k]);
            }
        }
    }
    const char *tmpd = getenv("TMPDIR");
    if (tmpd && *tmpd) {
        size_t l = strlen(tmpd);
        while (l > 0 && tmpd[l - 1] == '/')
            l--;
        char p[1152];
        snprintf(p, sizeof p, "%.*s/sharkvis-%u.state", (int)l, tmpd, uid);
        *out = realloc(*out, (*n + 1) * sizeof(char *));
        (*out)[(*n)++] = strdup(p);
    }
    {
        char p[128];
        snprintf(p, sizeof p, "/tmp/sharkvis-%u.state", uid);
        *out = realloc(*out, (*n + 1) * sizeof(char *));
        (*out)[(*n)++] = strdup(p);
    }
    *out = realloc(*out, (*n + 1) * sizeof(char *));
    (*out)[(*n)++] = strdup("/tmp/sharkvis.state");
}

int sv_read_live_state(LiveState *out) {
    char **paths = NULL;
    size_t n = 0;
    collect_state_paths(&paths, &n);
    unsigned long long stale = stale_after_ms();
    int found = 0;
    LiveState best;
    memset(&best, 0, sizeof best);
    unsigned long long best_started = 0;
    for (size_t i = 0; i < n; i++) {
        uint64_t mtime = 0;
        size_t size = 0;
        if (!file_fresh(paths[i], stale, &mtime, &size))
            continue;
        char *text = read_file_all(paths[i]);
        if (!text)
            continue;
        char *t = text;
        while (*t == ' ' || *t == '\t' || *t == '\n' || *t == '\r')
            t++;
        if (!*t) {
            free(text);
            continue;
        }
        LiveState st;
        sv_parse_state_text(text, &st);
        free(text);
        int live = st.has_color || st.has_energy || st.has_beat || st.has_glow ||
                   st.has_ghigh || st.has_bass || st.has_left || st.has_right;
        if (!live)
            continue;
        unsigned long long started = st.has_started ? st.started : 0;
        if (!found || started > best_started) {
            best = st;
            best_started = started;
            found = 1;
        }
    }
    for (size_t i = 0; i < n; i++)
        free(paths[i]);
    free(paths);
    if (found)
        *out = best;
    return found;
}

typedef struct {
    float avg;
    float peak;
    float prev;
    float beat;
    float now;
    float onsets[8];
    size_t n;
    float period;
    float next;
    float misses;
    float cand;
    unsigned cstr;
} BeatTracker;

static float estimate_period(const float *times, size_t n) {
    float iois[8];
    size_t m = 0;
    for (size_t i = 0; i + 1 < n && m < 8; i++) {
        float d = times[i + 1] - times[i];
        if (d > 0.05f)
            iois[m++] = d;
    }
    if (m < 3)
        return -1.0f;
    for (size_t i = 1; i < m; i++) {
        size_t j = i;
        while (j > 0 && iois[j] < iois[j - 1]) {
            float t = iois[j];
            iois[j] = iois[j - 1];
            iois[j - 1] = t;
            j--;
        }
    }
    float p = iois[m / 2];
    if (p < 0.2f)
        p = 0.2f;
    if (p > 1.5f)
        p = 1.5f;
    while (p > 0.65f)
        p /= 2.0f;
    while (p < 0.30f)
        p *= 2.0f;
    float bpm = roundf(60.0f / p);
    if (bpm < 60.0f)
        bpm = 60.0f;
    if (bpm > 200.0f)
        bpm = 200.0f;
    return 60.0f / bpm;
}

static float tracker_step(BeatTracker *t, float energy, float dt) {
    if (dt < 0.001f)
        dt = 0.001f;
    if (dt > 1.0f)
        dt = 1.0f;
    t->now += dt;
    float e = energy;
    if (e < 0.0f)
        e = 0.0f;
    if (e > 1.0f)
        e = 1.0f;
    t->avg += (e - t->avg) * (1.0f - expf(-dt * 1.5f));
    float pk = t->peak * expf(-dt * 0.8f);
    t->peak = e > pk ? e : pk;
    float range = t->peak - t->avg;
    if (range < 0.05f)
        range = 0.05f;
    float strength = (e - t->avg) / range;
    if (strength < 0.0f)
        strength = 0.0f;
    if (strength > 1.0f)
        strength = 1.0f;
    if (strength > 0.55f && e > 0.05f && e > t->prev) {
        t->prev = e;
        float tt = t->now;
        if (t->n < 8) {
            t->onsets[t->n++] = tt;
        } else {
            memmove(t->onsets, t->onsets + 1, 7 * sizeof(float));
            t->onsets[7] = tt;
        }
        if (t->n >= 5) {
            if (t->period > 0.0f) {
                float window = 0.12f * t->period;
                float d = t->next - tt;
                if (d < 0)
                    d = -d;
                if (d <= window) {
                    t->next = tt + t->period;
                    t->misses = 0.0f;
                }
            }
            float p = estimate_period(t->onsets, t->n);
            if (p > 0.0f) {
                float denom = t->cand > 1e-6f ? t->cand : 1e-6f;
                float rel = (p - t->cand) / denom;
                if (rel < 0)
                    rel = -rel;
                if (rel <= 0.12f)
                    t->cstr++;
                else {
                    t->cand = p;
                    t->cstr = 1;
                }
                if (t->cstr >= 2 && (t->period <= 0.0f || t->misses >= 3.0f)) {
                    t->period = t->cand;
                    t->next = tt + t->cand;
                    t->misses = 0.0f;
                }
            }
        }
        t->beat = 1.0f;
    } else {
        t->prev = e;
        if (t->period > 0.0f) {
            float window = 0.12f * t->period;
            if (t->now >= t->next - window && strength > 0.25f && e > 0.05f) {
                t->beat = 1.0f;
                t->misses = 0.0f;
                t->next += t->period;
            } else {
                t->beat *= expf(-dt * 5.0f);
                if (t->now > t->next + window) {
                    t->misses += 1.0f;
                    t->next += t->period;
                    if (t->misses >= 4.0f)
                        t->period = 0.0f;
                }
            }
        } else {
            t->beat *= expf(-dt * 5.0f);
        }
    }
    if (t->beat < 0.0f)
        t->beat = 0.0f;
    if (t->beat > 1.0f)
        t->beat = 1.0f;
    return t->beat;
}

typedef struct {
    float energy;
    float beat;
    uint64_t updated_ms;
    int dead;
    int stop;
    pthread_mutex_t mu;
    pthread_t thread;
    int has_thread;
} BeatMonitor;

static void *beat_thread(void *arg) {
    BeatMonitor *m = arg;
    char err[256];
    PaClient *client = pa_connect("jefetch-beat", err, sizeof err);
    if (!client) {
        pthread_mutex_lock(&m->mu);
        m->dead = 1;
        pthread_mutex_unlock(&m->mu);
        return NULL;
    }
    char dev[512];
    if (!pa_default_monitor(client, dev, sizeof dev, err, sizeof err)) {
        pthread_mutex_lock(&m->mu);
        m->dead = 1;
        pthread_mutex_unlock(&m->mu);
        pa_client_free(client);
        return NULL;
    }
    PaRecord *rec = pa_record(client, dev, BEAT_RATE, 1, err, sizeof err);
    if (!rec) {
        pthread_mutex_lock(&m->mu);
        m->dead = 1;
        pthread_mutex_unlock(&m->mu);
        return NULL;
    }
    uint8_t raw[BEAT_WINDOW * 2];
    float energy = 0.0f;
    BeatTracker tracker;
    memset(&tracker, 0, sizeof tracker);
    float window_dt = (float)BEAT_WINDOW / (float)BEAT_RATE;
    volatile int *stop = (volatile int *)&m->stop;
    for (;;) {
        pthread_mutex_lock(&m->mu);
        int st = m->stop;
        pthread_mutex_unlock(&m->mu);
        if (st)
            break;
        size_t got = 0;
        while (got < sizeof raw) {
            pthread_mutex_lock(&m->mu);
            st = m->stop;
            pthread_mutex_unlock(&m->mu);
            if (st)
                break;
            long k = pa_read_chunk(rec, raw + got, sizeof raw - got, stop, err, sizeof err);
            if (k <= 0) {
                if (k < 0) {
                    pthread_mutex_lock(&m->mu);
                    m->dead = 1;
                    pthread_mutex_unlock(&m->mu);
                    pa_record_free(rec);
                    return NULL;
                }
                break;
            }
            got += (size_t)k;
        }
        if (got < sizeof raw) {
            pthread_mutex_lock(&m->mu);
            st = m->stop;
            pthread_mutex_unlock(&m->mu);
            if (st)
                break;
            continue;
        }
        float sum = 0.0f;
        for (int i = 0; i < BEAT_WINDOW; i++) {
            int16_t v = (int16_t)((unsigned)raw[2 * i] | ((unsigned)raw[2 * i + 1] << 8));
            float f = (float)v / 32768.0f;
            sum += f * f;
        }
        float rms = sqrtf(sum / (float)BEAT_WINDOW);
        float target = rms * 4.0f;
        if (target < 0.0f)
            target = 0.0f;
        if (target > 1.0f)
            target = 1.0f;
        energy += (target - energy) * 0.4f;
        float beat = tracker_step(&tracker, energy, window_dt);
        if (beat < 0.0f)
            beat = 0.0f;
        if (beat > 1.0f)
            beat = 1.0f;
        pthread_mutex_lock(&m->mu);
        m->energy = energy;
        m->beat = beat;
        m->updated_ms = jf_now_ms();
        pthread_mutex_unlock(&m->mu);
    }
    pa_record_free(rec);
    return NULL;
}

static BeatMonitor *beat_monitor_start(void) {
    BeatMonitor *m = calloc(1, sizeof *m);
    if (!m)
        return NULL;
    pthread_mutex_init(&m->mu, NULL);
    m->updated_ms = jf_now_ms();
    if (pthread_create(&m->thread, NULL, beat_thread, m) != 0) {
        pthread_mutex_destroy(&m->mu);
        free(m);
        return NULL;
    }
    m->has_thread = 1;
    return m;
}

static void beat_monitor_free(BeatMonitor *m) {
    if (!m)
        return;
    pthread_mutex_lock(&m->mu);
    m->stop = 1;
    pthread_mutex_unlock(&m->mu);
    if (m->has_thread)
        pthread_join(m->thread, NULL);
    pthread_mutex_destroy(&m->mu);
    free(m);
}

static int beat_monitor_sample(BeatMonitor *m, float *e, float *b) {
    pthread_mutex_lock(&m->mu);
    int dead = m->dead;
    uint64_t upd = m->updated_ms;
    float en = m->energy, bt = m->beat;
    pthread_mutex_unlock(&m->mu);
    if (dead)
        return 0;
    if (jf_now_ms() - upd > 1500)
        return 0;
    *e = en;
    *b = bt;
    return 1;
}

struct Sync {
    int running;
    int have_running_at;
    uint64_t running_at;
    int has_gradients;
    Rgb grad_lo;
    Rgb grad_hi;
    char **glyphs;
    size_t nglyphs;
    int have_visual_at;
    uint64_t visual_at;
    BeatMonitor *monitor;
    LiveFrame last;
    int have_last_ok;
    uint64_t last_ok;
    int have_state_mem;
    char *mem_path;
    uint64_t mem_mtime;
    size_t mem_size;
    LiveState mem_state;
    double term_phase;
    uint64_t term_at;
    int term_init;
};

Sync *sync_new(void) {
    Sync *s = calloc(1, sizeof *s);
    return s;
}

void sync_free(Sync *s) {
    if (!s)
        return;
    beat_monitor_free(s->monitor);
    live_frame_free_contents(&s->last);
    sv_free_strs(s->glyphs, s->nglyphs);
    free(s->mem_path);
    free(s);
}

static void frame_free_glyphs(LiveFrame *f) {
    live_frame_free_contents(f);
}

static void frame_copy_glyphs(LiveFrame *dst, char **glyphs, size_t n) {
    frame_free_glyphs(dst);
    if (n == 0 || !glyphs)
        return;
    if (n > 64)
        n = 64;
    char **ng = malloc(n * sizeof(char *));
    if (!ng)
        return;
    size_t k = 0;
    for (; k < n; k++) {
        if (!glyphs[k]) {
            ng[k] = strdup(" ");
            if (!ng[k])
                break;
            continue;
        }
        ng[k] = strdup(glyphs[k]);
        if (!ng[k])
            break;
    }
    if (k != n) {
        for (size_t j = 0; j < k; j++)
            free(ng[j]);
        free(ng);
        return;
    }
    dst->glyphs = ng;
    dst->nglyphs = n;
}

LiveFrame sync_poll(Sync *s, SharkvisMode mode, float beat_depth, int live_colors) {
    LiveFrame inactive;
    memset(&inactive, 0, sizeof inactive);
    if (mode == SVM_OFF) {
        beat_monitor_free(s->monitor);
        s->monitor = NULL;
        frame_free_glyphs(&s->last);
        memset(&s->last, 0, sizeof s->last);
        LiveFrame out;
        memset(&out, 0, sizeof out);
        /* Like Rust's LiveFrame::inactive(): no beat info means full speed,
         * otherwise the base animation never advances (spin_phase += 0). */
        out.speed_mult = 1.0f;
        return out;
    }
    uint64_t now = jf_now_ms();
    if (!s->have_running_at || now - s->running_at >= 500) {
        s->running = sv_is_running();
        s->running_at = now;
        s->have_running_at = 1;
    }
    if (!sv_mode_enabled(mode, s->running)) {
        beat_monitor_free(s->monitor);
        s->monitor = NULL;
        frame_free_glyphs(&s->last);
        memset(&s->last, 0, sizeof s->last);
        LiveFrame out;
        memset(&out, 0, sizeof out);
        /* Like Rust's LiveFrame::inactive(): no beat info means full speed. */
        out.speed_mult = 1.0f;
        return out;
    }
    if (!s->have_visual_at || now - s->visual_at >= 250) {
        Rgb lo, hi;
        if (sv_gradient_colors(&lo, &hi)) {
            /* Only latch when actually changed; otherwise tint caches churn. */
            if (!s->has_gradients || s->grad_lo.r != lo.r || s->grad_lo.g != lo.g ||
                s->grad_lo.b != lo.b || s->grad_hi.r != hi.r || s->grad_hi.g != hi.g ||
                s->grad_hi.b != hi.b) {
                s->has_gradients = 1;
                s->grad_lo = lo;
                s->grad_hi = hi;
            }
        } else {
            s->has_gradients = 0;
        }
        size_t ng = 0;
        char **g = sv_glyph_ramp(&ng);
        sv_free_strs(s->glyphs, s->nglyphs);
        s->glyphs = g;
        s->nglyphs = ng;
        s->visual_at = now;
        s->have_visual_at = 1;
    }
    int has_energy = 0, has_beat = 0;
    float energy = 0, beat = 0;
    Rgb color = {0, 0, 0};
    int has_color = 0;
    int has_live_grad = 0;
    Rgb live_lo = {0, 0, 0}, live_hi = {0, 0, 0};
    int has_bass = 0, has_left = 0, has_right = 0;
    float bass = 0, left = 0, right = 0;
    LiveState live;
    int have_state = sv_read_live_state(&live);
    if (have_state) {
        free(s->mem_path);
        s->mem_path = NULL;
        s->have_state_mem = 1;
        s->mem_state = live;
        if (live.has_energy) {
            has_energy = 1;
            energy = live.energy;
        }
        if (live.has_beat) {
            has_beat = 1;
            beat = live.beat;
        }
        if (live.has_color) {
            has_color = 1;
            color = live.color;
        }
        if (live.has_bass) {
            has_bass = 1;
            bass = live.bass;
        }
        if (live.has_left) {
            has_left = 1;
            left = live.left;
        }
        if (live.has_right) {
            has_right = 1;
            right = live.right;
        }
        if (live.has_glow && live.has_ghigh) {
            has_live_grad = 1;
            live_lo = live.glow;
            live_hi = live.ghigh;
        }
    }
    if (have_state) {
        s->have_last_ok = 1;
        s->last_ok = now;
    } else if (s->last.active) {
        if (s->have_last_ok && now - s->last_ok < 750) {
            /* Deep copy: the caller takes ownership and frees its copy,
             * so it must never alias s->last.glyphs (use-after-free). */
            LiveFrame stale = s->last;
            stale.glyphs = NULL;
            stale.nglyphs = 0;
            if (s->last.nglyphs && s->last.glyphs) {
                stale.glyphs = malloc(s->last.nglyphs * sizeof(char *));
                if (stale.glyphs) {
                    size_t k = 0;
                    for (; k < s->last.nglyphs; k++) {
                        stale.glyphs[k] = strdup(s->last.glyphs[k]);
                        if (!stale.glyphs[k])
                            break;
                    }
                    stale.nglyphs = k;
                    if (k != s->last.nglyphs) {
                        for (size_t j = 0; j < k; j++)
                            free(stale.glyphs[j]);
                        free(stale.glyphs);
                        stale.glyphs = NULL;
                        stale.nglyphs = 0;
                    }
                }
            }
            return stale;
        }
    }
    if (!has_energy || !has_beat) {
        if (!s->monitor)
            s->monitor = beat_monitor_start();
        if (s->monitor) {
            float e, b;
            if (beat_monitor_sample(s->monitor, &e, &b)) {
                if (!has_energy) {
                    has_energy = 1;
                    energy = e;
                }
                if (!has_beat) {
                    has_beat = 1;
                    beat = b;
                }
            }
        }
    }
    if (energy < 0.0f)
        energy = 0.0f;
    if (energy > 1.0f)
        energy = 1.0f;
    if (beat < 0.0f)
        beat = 0.0f;
    if (beat > 1.0f)
        beat = 1.0f;
    LiveFrame frame;
    memset(&frame, 0, sizeof frame);
    frame.active = 1;
    if (live_colors) {
        /* Fresh state wins over everything. Sticky fallback to s->last is
         * only allowed when we have NO fresh state (transient gap); when
         * have_state==1 a missing grad/color means "no live color", not
         * "keep showing the previous logo's color".
         * Priority: live grad > fresh flat > config grad > last (gap only). */
        if (has_live_grad) {
            frame.has_grad = 1;
            frame.glo = live_lo;
            frame.ghi = live_hi;
        } else if (have_state && has_color) {
            frame.has_flat = 1;
            frame.flat = color;
        } else if (s->has_gradients) {
            frame.has_grad = 1;
            frame.glo = s->grad_lo;
            frame.ghi = s->grad_hi;
        } else if (!have_state) {
            if (s->last.has_grad) {
                frame.has_grad = 1;
                frame.glo = s->last.glo;
                frame.ghi = s->last.ghi;
            } else if (s->last.has_flat) {
                frame.has_flat = 1;
                frame.flat = s->last.flat;
            }
        }
    }
    frame_copy_glyphs(&frame, s->glyphs, s->nglyphs);
    frame.energy = energy;
    frame.beat = beat;
    frame.bass = has_bass ? bass : energy;
    if (frame.bass < 0.0f)
        frame.bass = 0.0f;
    if (frame.bass > 1.0f)
        frame.bass = 1.0f;
    frame.left = has_left ? left : energy;
    if (frame.left < 0.0f)
        frame.left = 0.0f;
    if (frame.left > 1.0f)
        frame.left = 1.0f;
    frame.right = has_right ? right : energy;
    if (frame.right < 0.0f)
        frame.right = 0.0f;
    if (frame.right > 1.0f)
        frame.right = 1.0f;
    frame.speed_mult = beat_speed_mult(beat, beat_depth);
    frame_free_glyphs(&s->last);
    s->last = frame;
    LiveFrame ret;
    memset(&ret, 0, sizeof ret);
    ret.active = s->last.active;
    ret.has_grad = s->last.has_grad;
    ret.glo = s->last.glo;
    ret.ghi = s->last.ghi;
    ret.has_flat = s->last.has_flat;
    ret.flat = s->last.flat;
    ret.energy = s->last.energy;
    ret.beat = s->last.beat;
    ret.bass = s->last.bass;
    ret.left = s->last.left;
    ret.right = s->last.right;
    ret.speed_mult = s->last.speed_mult;
    ret.has_term_pal = s->last.has_term_pal;
    if (ret.has_term_pal)
        memcpy(ret.term_pal, s->last.term_pal, sizeof ret.term_pal);
    if (s->last.nglyphs && s->last.glyphs) {
        ret.glyphs = malloc(s->last.nglyphs * sizeof(char *));
        if (ret.glyphs) {
            size_t k = 0;
            for (; k < s->last.nglyphs; k++) {
                if (!s->last.glyphs[k]) {
                    ret.glyphs[k] = NULL;
                    break;
                }
                ret.glyphs[k] = strdup(s->last.glyphs[k]);
                if (!ret.glyphs[k])
                    break;
            }
            if (k == s->last.nglyphs) {
                ret.nglyphs = s->last.nglyphs;
            } else {
                for (size_t j = 0; j < k; j++)
                    free(ret.glyphs[j]);
                free(ret.glyphs);
                ret.glyphs = NULL;
                ret.nglyphs = 0;
            }
        }
    }
    return ret;
}

const LiveFrame *sync_last(const Sync *s) {
    return &s->last;
}

int sv_is_live_color_name(const char *s) {
    while (*s == ' ' || *s == '\t')
        s++;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t'))
        n--;
    if (n != 8)
        return 0;
    static const char *t = "sharkvis";
    for (size_t i = 0; i < 8; i++) {
        char a = s[i];
        if (a >= 'A' && a <= 'Z')
            a += 32;
        if (a != t[i])
            return 0;
    }
    return 1;
}

int sv_grad_for_row(const LiveFrame *f, size_t idx, size_t total, Rgb *out) {
    if (!f->active)
        return 0;
    if (f->has_flat) {
        *out = f->flat;
        return 1;
    }
    if (f->has_grad) {
        float t;
        if (total > 1) {
            size_t i = idx < total - 1 ? idx : total - 1;
            t = (float)(total - 1 - i) / (float)(total - 1);
        } else {
            t = 0.5f;
        }
        *out = sv_lerp_rgb(f->glo, f->ghi, t);
        return 1;
    }
    return 0;
}

int sv_has_display_color(const LiveFrame *f) {
    return f->active && (f->has_flat || f->has_grad);
}

void sv_rgb_ansi_start(Rgb c, char *out, size_t n) {
    snprintf(out, n, "\x1b[38;2;%u;%u;%um", c.r, c.g, c.b);
}

/* Real terminal palette via OSC 4 query. No hardcoded colors: the 16 colors
 * come from the terminal itself (\e]4;i;? -> rgb:R/G/B). Queried once per
 * process; 0 when the terminal cannot be asked (no tty, dumb, silent). */
static int term_env_ok(void) {
    const char *t = getenv("TERM");
    if (!t || !*t)
        return 0;
    char l[64];
    size_t i = 0;
    while (t[i] && i + 1 < sizeof l) {
        char c = t[i];
        l[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    l[i] = 0;
    return strcmp(l, "dumb") != 0;
}

static int hexdig(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* One X11 color component (1-4 hex digits) scaled to 8 bit. */
static int parse_comp(const char *s, size_t len, unsigned *out) {
    if (len < 1 || len > 4)
        return 0;
    unsigned v = 0, max = 0;
    for (size_t i = 0; i < len; i++) {
        int d = hexdig(s[i]);
        if (d < 0)
            return 0;
        v = v * 16 + (unsigned)d;
    }
    max = len == 1 ? 15 : len == 2 ? 255 : len == 3 ? 4095 : 65535;
    *out = (v * 255 + max / 2) / max;
    return 1;
}

static int parse_osc4_spec(const char *s, Rgb *out) {
    if (!strncmp(s, "rgb:", 4)) {
        const char *p = s + 4;
        unsigned c[3];
        for (int k = 0; k < 3; k++) {
            const char *e = p;
            while (*e && *e != '/')
                e++;
            if (!parse_comp(p, (size_t)(e - p), &c[k]))
                return 0;
            p = *e ? e + 1 : e;
        }
        out->r = (uint8_t)c[0];
        out->g = (uint8_t)c[1];
        out->b = (uint8_t)c[2];
        return 1;
    }
    if (s[0] == '#') {
        size_t l = strlen(s + 1);
        if (l != 3 && l != 6)
            return 0;
        unsigned c[3];
        size_t w = l / 3;
        for (int k = 0; k < 3; k++) {
            if (!parse_comp(s + 1 + (size_t)k * w, w, &c[k]))
                return 0;
        }
        out->r = (uint8_t)c[0];
        out->g = (uint8_t)c[1];
        out->b = (uint8_t)c[2];
        return 1;
    }
    return 0;
}

/* Scan accumulated reply bytes for OSC 4 responses. */
static int osc4_parse(const uint8_t *buf, size_t len, Rgb pal[16], int have[16]) {
    int found = 0;
    for (size_t i = 0; i + 5 < len; i++) {
        if (buf[i] != 0x1b || buf[i + 1] != ']')
            continue;
        size_t j = i + 2;
        if (j + 1 >= len || buf[j] != '4' || buf[j + 1] != ';')
            continue;
        j += 2;
        unsigned idx = 0;
        size_t digits = 0;
        while (j < len && buf[j] >= '0' && buf[j] <= '9') {
            idx = idx * 10 + (unsigned)(buf[j] - '0');
            digits++;
            j++;
        }
        if (!digits || idx > 15)
            continue;
        if (j >= len || (buf[j] != ';' && buf[j] != ':'))
            continue;
        j++;
        size_t s = j;
        while (j < len && buf[j] != '\a' &&
               !(buf[j] == 0x1b && j + 1 < len && buf[j + 1] == '\\'))
            j++;
        if (j >= len)
            break; /* unterminated at buffer end; more data may complete it */
        char spec[64];
        size_t sl = j - s;
        if (sl == 0 || sl >= sizeof spec)
            continue;
        memcpy(spec, buf + s, sl);
        spec[sl] = 0;
        Rgb c;
        if (!have[idx] && parse_osc4_spec(spec, &c)) {
            pal[idx] = c;
            have[idx] = 1;
            found++;
        }
    }
    return found;
}

#define OSC4_MAX_READS 6

static int osc4_query(Rgb pal[16]) {
    if (!term_env_ok())
        return 0;
    int fd = open("/dev/tty", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return 0;
    struct termios orig, raw;
    if (tcgetattr(fd, &orig) != 0) {
        close(fd);
        return 0;
    }
    raw = orig;
    raw.c_lflag &= (unsigned)(~(ICANON | ECHO));
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(fd, TCSANOW, &raw) != 0) {
        close(fd);
        return 0;
    }
    char req[16 * 16];
    size_t rl = 0;
    for (int i = 0; i < 16; i++)
        rl += (size_t)snprintf(req + rl, sizeof req - rl, "\x1b]4;%d;?\x1b\\", i);
    size_t wr = 0;
    while (wr < rl) {
        ssize_t k = write(fd, req + wr, rl - wr);
        if (k <= 0)
            break;
        wr += (size_t)k;
    }
    uint8_t buf[4096];
    size_t bl = 0;
    int have[16] = {0};
    int found = 0;
    int empty = 0;
    for (int i = 0; i < OSC4_MAX_READS && found < 16 && bl < sizeof buf - 64; i++) {
        ssize_t k = read(fd, buf + bl, sizeof buf - bl - 1);
        if (k < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (k == 0) {
            /* Timeout with no bytes yet; replies may still be on the way. */
            if (++empty >= 3)
                break;
            continue;
        }
        empty = 0;
        bl += (size_t)k;
        osc4_parse(buf, bl, pal, have);
        found = 0;
        for (int q = 0; q < 16; q++)
            found += have[q];
    }
    /* Swallow stragglers so late replies never spill into shell input. */
    read(fd, buf, 1);
    tcsetattr(fd, TCSANOW, &orig);
    close(fd);
    return found == 16;
}

static Rgb term_cache[16];
static int term_cache_state = 0; /* 0 unknown, 1 ok, -1 failed */
static uint64_t term_cache_at = 0;

#define TERM_REQUERY_MS 10000

int sv_term_palette(Rgb out[16]) {
    uint64_t now = jf_now_ms();
    if (!term_cache_state || now - term_cache_at > TERM_REQUERY_MS) {
        /* Re-query periodically: picks up terminal theme switches and
         * recovers from an early failed query instead of freezing. */
        term_cache_state = osc4_query(term_cache) ? 1 : -1;
        term_cache_at = now;
    }
    if (term_cache_state < 0)
        return 0;
    memcpy(out, term_cache, sizeof term_cache);
    return 1;
}

/* Escape for a live color: exact palette hits emit the terminal index
 * itself (identical to what sharkvis renders), blends stay truecolor. */
void sv_live_esc(const Rgb *pal, Rgb c, char *out, size_t n) {
    if (pal) {
        for (int i = 0; i < 16; i++) {
            if (pal[i].r == c.r && pal[i].g == c.g && pal[i].b == c.b) {
                if (i < 8)
                    snprintf(out, n, "\x1b[%dm", 30 + i);
                else
                    snprintf(out, n, "\x1b[%dm", 90 + (i - 8));
                return;
            }
        }
    }
    snprintf(out, n, "\x1b[38;2;%u;%u;%um", c.r, c.g, c.b);
}

/* Smoothly interpolated palette position: full RGB lerp between the two
 * adjacent palette entries, so the flow never bands. */
static Rgb term_at(const Rgb pal[16], double pos) {
    double f = floor(pos);
    double frac = pos - f;
    long i = (long)f % 16;
    if (i < 0)
        i += 16;
    long j = (i + 1) % 16;
    if (frac < 0.0)
        frac = 0.0;
    if (frac > 1.0)
        frac = 1.0;
    Rgb o;
    o.r = (uint8_t)(pal[i].r + (pal[j].r - pal[i].r) * frac + 0.5);
    o.g = (uint8_t)(pal[i].g + (pal[j].g - pal[i].g) * frac + 0.5);
    o.b = (uint8_t)(pal[i].b + (pal[j].b - pal[i].b) * frac + 0.5);
    return o;
}

#define TERM_FLOW_SPAN 4.0
#define TERM_FLOW_PERIOD_MS 6400.0

void sv_term_flow(Sync *s, float energy, const Rgb pal[16], Rgb *lo, Rgb *hi) {
    uint64_t now = jf_now_ms();
    if (!s->term_init) {
        s->term_phase = 0.0;
        s->term_at = now;
        s->term_init = 1;
    } else {
        double dt = (double)(now - s->term_at);
        if (dt < 0.0)
            dt = 0.0;
        if (dt > 1000.0)
            dt = 1000.0;
        double rate = 16.0 / TERM_FLOW_PERIOD_MS;
        double boost = 1.0 + 2.0 * (energy < 0.0f ? 0.0 : energy > 1.0f ? 1.0 : energy);
        s->term_phase += dt * rate * boost;
        if (s->term_phase >= 4096.0)
            s->term_phase = fmod(s->term_phase, 16.0);
        s->term_at = now;
    }
    *lo = term_at(pal, s->term_phase);
    *hi = term_at(pal, s->term_phase + TERM_FLOW_SPAN);
}

void sv_swap_placeholders(const char *s, int has_live, Rgb live,
                            const Rgb *term_pal, char *out, size_t n) {
    const char *ph = "\x1b[38;2;1;2;3m";
    size_t phlen = strlen(ph);
    size_t pos = 0;
    const char *p = s;
    char esc[32] = "";
    if (has_live)
        sv_live_esc(term_pal, live, esc, sizeof esc);
    while (*p && pos + 1 < n) {
        if (!strncmp(p, ph, phlen)) {
            if (has_live) {
                size_t l = strlen(esc);
                if (pos + l >= n)
                    break;
                memcpy(out + pos, esc, l);
                pos += l;
            }
            p += phlen;
        } else {
            out[pos++] = *p++;
        }
    }
    out[pos] = 0;
}
