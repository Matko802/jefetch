#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "anim.h"
#include "common.h"

#define ANIM_K2 5.5f
#define ANIM_WIDTH 60
#define ANIM_GAP 2
#define ANIM_MAX_POINTS 400000
#define ANIM_BASE_FPS 12.0f

static const char *DEFAULT_SHADING[] = {"\xe2\x96\x91", "\xe2\x96\x92", "\xe2\x96\x93",
                                        "\xe2\x96\x88"};
static const char *ASCII_SHADING[] = {".", ":", "*", "#"};

void anim_default_shading(char ***out, size_t *n) {
    const char **ramp = jf_utf8_supported() ? DEFAULT_SHADING : ASCII_SHADING;
    *out = malloc(4 * sizeof(char *));
    for (int i = 0; i < 4; i++)
        (*out)[i] = strdup(ramp[i]);
    *n = 4;
}

void anim_config_default(AnimConfig *c) {
    memset(c, 0, sizeof *c);
    c->spin_y = 1;
    c->speed = 2.0f;
    c->speed_x = 1.0f;
    c->speed_y = 1.0f;
    c->speed_z = 1.0f;
    c->size = 2.0f;
    c->depth = 2.0f;
    c->depth_user_set = 1;
    c->light_x = -0.4082f;
    c->light_y = 0.8165f;
    c->light_z = -0.4082f;
    anim_default_shading(&c->shading, &c->nshading);
    c->sharkvis = SVM_OFF;
    c->beat_depth = 0.6f;
    c->grow = 0.12f;
}

void anim_config_free(AnimConfig *c) {
    for (size_t i = 0; i < c->nshading; i++)
        free(c->shading[i]);
    free(c->shading);
    c->shading = NULL;
    c->nshading = 0;
}

static int is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_';
}

static int find_key(const char *s, const char *key) {
    size_t sl = strlen(s), kl = strlen(key);
    size_t from = 0;
    while (from + kl <= sl) {
        const char *rel = strstr(s + from, key);
        if (!rel)
            return -1;
        size_t pos = (size_t)(rel - s);
        int prev_ok = pos == 0 || !is_word_char(s[pos - 1]);
        size_t after = pos + kl;
        int next_ok = after >= sl || !is_word_char(s[after]);
        if (prev_ok && next_ok)
            return (int)pos;
        from = pos + 1;
    }
    return -1;
}

static int has_word(const char *s, const char *word) {
    return find_key(s, word) >= 0;
}

static int extract_word(const char *low, const char *raw, const char *key, int stop_comma,
                        char *out, size_t n) {
    int p = find_key(low, key);
    if (p < 0)
        return 0;
    size_t kl = strlen(key);
    size_t pos = (size_t)p + kl;
    size_t ll = strlen(low);
    size_t off = 0;
    while (pos + off < ll) {
        char c = low[pos + off];
        if (c == '=' || c == ':' || c == ' ' || c == '\t' || c == ',')
            off++;
        else
            break;
    }
    size_t rl = strlen(raw);
    if (pos + off >= rl)
        return 0;
    const char *rr = raw + pos + off;
    const char *lr = low + pos + off;
    if (*rr == '"' || *rr == '\'') {
        char q = *rr;
        const char *end = strchr(rr + 1, q);
        if (!end)
            return 0;
        size_t l = (size_t)(end - (rr + 1));
        if (l >= n)
            l = n - 1;
        memcpy(out, rr + 1, l);
        out[l] = 0;
        return 1;
    }
    size_t end = 0;
    size_t i = 0;
    while (lr[i]) {
        char c = lr[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || (stop_comma && c == ','))
            break;
        end = i + 1;
        i++;
    }
    if (end == 0)
        return 0;
    if (end >= n)
        end = n - 1;
    memcpy(out, rr, end);
    out[end] = 0;
    return 1;
}

static int extract_number(const char *s, const char *key, float *out) {
    int p = find_key(s, key);
    if (p < 0)
        return 0;
    const char *rest = s + p + strlen(key);
    while (*rest && !((*rest >= '0' && *rest <= '9') || *rest == '-' || *rest == '.'))
        rest++;
    char *end;
    float v = strtof(rest, &end);
    if (end == rest)
        return 0;
    const char *q = rest;
    int e2 = 0;
    while (q < (const char *)end) {
        if (*q == '.' || *q == '-' || (*q >= '0' && *q <= '9'))
            e2++;
        else
            break;
        q++;
    }
    if (e2 == 0)
        return 0;
    *out = v;
    return 1;
}

static void blank_option_spans(const char *s, const char *const *keys, size_t nkeys,
                               char *out, size_t n) {
    snprintf(out, n, "%s", s);
    size_t len = strlen(out);
    for (size_t ki = 0; ki < nkeys; ki++) {
        const char *key = keys[ki];
        size_t kl = strlen(key);
        if (kl == 0)
            continue;
        size_t i = 0;
        while (i + kl <= len) {
            if (memcmp(out + i, key, kl) != 0) {
                i++;
                continue;
            }
            int prev_ok = i == 0 || !is_word_char(out[i - 1]);
            size_t after = i + kl;
            int next_ok = after >= len || !is_word_char(out[after]);
            if (!prev_ok || !next_ok) {
                i++;
                continue;
            }
            for (size_t j = i; j < after; j++)
                out[j] = ' ';
            size_t j = after;
            while (j < len &&
                   (out[j] == '=' || out[j] == ':' || out[j] == ' ' || out[j] == '\t' ||
                    out[j] == ',')) {
                out[j] = ' ';
                j++;
            }
            if (j < len && (out[j] == '"' || out[j] == '\'')) {
                char q = out[j];
                out[j] = ' ';
                j++;
                while (j < len && out[j] != q) {
                    out[j] = ' ';
                    j++;
                }
                if (j < len) {
                    out[j] = ' ';
                    j++;
                }
            } else {
                while (j < len && out[j] != ' ' && out[j] != '\t' && out[j] != '\n' &&
                       out[j] != '\r') {
                    out[j] = ' ';
                    j++;
                }
            }
            i = j;
        }
    }
}

static float char_weight_utf8(const char *ch) {
    if (!ch || !*ch)
        return 0.0f;
    const unsigned char *b = (const unsigned char *)ch;
    if (b[0] < 0x80) {
        switch (b[0]) {
        case 'M':
            return 1.00f;
        case 'N':
            return 0.88f;
        case 'm':
            return 0.76f;
        case 'd':
            return 0.66f;
        case 'h':
        case 'b':
            return 0.56f;
        case 'y':
            return 0.46f;
        case 'o':
        case 'n':
            return 0.38f;
        case 's':
            return 0.30f;
        case '+':
            return 0.22f;
        case ':':
            return 0.18f;
        case '=':
            return 0.22f;
        case '-':
            return 0.14f;
        case '`':
            return 0.08f;
        case '.':
            return 0.10f;
        case '/':
            return 0.12f;
        case '\'':
            return 0.06f;
        case ' ':
            return 0.0f;
        default:
            break;
        }
        if (b[0] >= 'A' && b[0] <= 'Z')
            return 0.80f;
        if (b[0] >= 'a' && b[0] <= 'z')
            return 0.50f;
        if (b[0] >= '0' && b[0] <= '9')
            return 0.40f;
        return 0.15f;
    }
    if (b[0] == 0xE2 && b[1] == 0x96) {
        switch (b[2]) {
        case 0x88:
            return 1.00f;
        case 0x93:
            return 0.75f;
        case 0x92:
            return 0.50f;
        case 0x91:
            return 0.25f;
        case 0x80:
        case 0x84:
        case 0x8C:
        case 0x90:
            return 0.50f;
        case 0x82:
            return 0.55f;
        case 0x81:
            return 0.30f;
        default:
            return 0.50f;
        }
    }
    if (b[0] == 0xE2 && (b[1] == 0x94 || b[1] == 0x95))
        return 0.20f;
    return 0.30f;
}

static size_t u8len(unsigned char c) {
    if (c < 0x80)
        return 1;
    if ((c & 0xE0) == 0xC0)
        return 2;
    if ((c & 0xF0) == 0xE0)
        return 3;
    return 4;
}

static int parse_style_value(const char *v, int *flat) {
    char t[128];
    size_t i = 0;
    while (v[i] && i + 1 < sizeof t) {
        char c = v[i];
        t[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    t[i] = 0;
    char *s = t;
    while (*s == ' ' || *s == '\t')
        s++;
    if (strstr(s, "flat") || !strcmp(s, "2d") || strstr(s, "plain")) {
        *flat = 1;
        return 1;
    }
    if (strstr(s, "3d") || strstr(s, "three") || strstr(s, "depth")) {
        *flat = 0;
        return 1;
    }
    return 0;
}

static int parse_light_value(const char *v, float *x, float *y, float *z) {
    char t[128];
    size_t i = 0, j = 0;
    while (v[i] && j + 1 < sizeof t) {
        char c = v[i++];
        if (c == ' ' || c == '_' || c == '-')
            continue;
        t[j++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    t[j] = 0;
    if (!strcmp(t, "topleft")) {
        *x = 0.41f;
        *y = 0.82f;
        *z = -0.41f;
        return 1;
    }
    if (!strcmp(t, "topright")) {
        *x = -0.41f;
        *y = 0.82f;
        *z = -0.41f;
        return 1;
    }
    if (!strcmp(t, "top")) {
        *x = 0.0f;
        *y = 0.89f;
        *z = -0.45f;
        return 1;
    }
    if (!strcmp(t, "left")) {
        *x = 0.82f;
        *y = 0.41f;
        *z = -0.41f;
        return 1;
    }
    if (!strcmp(t, "right")) {
        *x = -0.82f;
        *y = 0.41f;
        *z = -0.41f;
        return 1;
    }
    if (!strcmp(t, "front")) {
        *x = 0.0f;
        *y = 0.0f;
        *z = -1.0f;
        return 1;
    }
    if (!strcmp(t, "bottomleft")) {
        *x = 0.41f;
        *y = -0.82f;
        *z = -0.41f;
        return 1;
    }
    if (!strcmp(t, "bottomright")) {
        *x = -0.41f;
        *y = -0.82f;
        *z = -0.41f;
        return 1;
    }
    char *c1 = strchr(v, ',');
    char *c2 = c1 ? strchr(c1 + 1, ',') : NULL;
    if (!c1 || !c2)
        return 0;
    char a[64], b[64], cc[64];
    size_t l = (size_t)(c1 - v);
    if (l >= sizeof a)
        return 0;
    memcpy(a, v, l);
    a[l] = 0;
    l = (size_t)(c2 - c1 - 1);
    if (l >= sizeof b)
        return 0;
    memcpy(b, c1 + 1, l);
    b[l] = 0;
    snprintf(cc, sizeof cc, "%s", c2 + 1);
    char *e1, *e2, *e3;
    float fx = strtof(a, &e1);
    float fy = strtof(b, &e2);
    float fz = strtof(cc, &e3);
    if (e1 == a || e2 == b || e3 == cc)
        return 0;
    *x = fx;
    *y = fy;
    *z = fz;
    return 1;
}

static int is_sharkvis_chars_value(const char *v) {
    while (*v == ' ' || *v == '\t')
        v++;
    size_t n = strlen(v);
    while (n > 0 && (v[n - 1] == ' ' || v[n - 1] == '\t'))
        n--;
    if (n != 8)
        return 0;
    static const char *t = "sharkvis";
    for (size_t i = 0; i < 8; i++) {
        char a = v[i];
        if (a >= 'A' && a <= 'Z')
            a += 32;
        if (a != t[i])
            return 0;
    }
    return 1;
}

static void apply_chars_value(AnimConfig *c, const char *v) {
    while (*v == ' ' || *v == '\t')
        v++;
    if (!*v)
        return;
    char l[512];
    size_t i = 0;
    while (v[i] && i + 1 < sizeof l) {
        char ch = v[i];
        l[i++] = (char)(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
    }
    l[i] = 0;
    if (!strcmp(l, "sharkvis")) {
        c->original_glyphs = 0;
        for (size_t k = 0; k < c->nshading; k++)
            free(c->shading[k]);
        free(c->shading);
        anim_default_shading(&c->shading, &c->nshading);
        return;
    }
    if (!strcmp(l, "ascii") || !strcmp(l, "original") || !strcmp(l, "keep") ||
        !strcmp(l, "logo") || !strcmp(l, "same") || strstr(l, "original") ||
        strstr(l, "ascii")) {
        c->original_glyphs = 1;
        return;
    }
    c->original_glyphs = 0;
    for (size_t k = 0; k < c->nshading; k++)
        free(c->shading[k]);
    free(c->shading);
    anim_default_shading(&c->shading, &c->nshading);
}

void anim_apply_style_chars(AnimConfig *c, const char *style, const char *chars) {
    if (style) {
        int f = 0;
        if (parse_style_value(style, &f))
            c->flat = f;
    }
    if (chars) {
        apply_chars_value(c, chars);
        c->shading_explicit = !is_sharkvis_chars_value(chars);
        c->chars_set = 1;
    }
}

float anim_auto_fps(const AnimConfig *c) {
    float ex = c->speed * c->speed_x;
    float ey = c->speed * c->speed_y;
    float ez = c->speed * c->speed_z;
    if (ex < 0)
        ex = -ex;
    if (ey < 0)
        ey = -ey;
    if (ez < 0)
        ez = -ez;
    float eff = ex;
    if (ey > eff)
        eff = ey;
    if (ez > eff)
        eff = ez;
    float v = 30.0f * (eff / 2.0f > 1.0f ? eff / 2.0f : 1.0f);
    if (v < 30.0f)
        v = 30.0f;
    if (v > 120.0f)
        v = 120.0f;
    return v;
}

unsigned long anim_frame_interval_us(const AnimConfig *c) {
    return (unsigned long)(1000000.0f / anim_auto_fps(c));
}

int anim_animation_color(const char *s, char *out, size_t n) {
    char v[512];
    if (!s || !extract_word(s, s, "color", 1, v, sizeof v))
        return 0;
    char *t = v;
    while (*t == ' ' || *t == '\t')
        t++;
    if (!*t)
        return 0;
    snprintf(out, n, "%s", t);
    return 1;
}

void anim_config_from_str(AnimConfig *c, const char *s) {
    static const char *OPTION_KEYS[] = {
        "speed_x", "speed_y", "speed_z", "speed", "size", "depth", "height", "style",
        "mode", "characters", "chars", "glyphs", "glyph", "shading", "symbols",
        "symbol", "ramp", "color", "textcolor", "light", "sharkvis", "nosharkvis",
        "no-sharkvis", "beat", "grow", "boom", "return"
    };
    if (!s)
        return;
    char low[2048];
    size_t i = 0;
    while (s[i] && i + 1 < sizeof low) {
        char ch = s[i];
        low[i++] = (char)(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
    }
    low[i] = 0;
    char raw[2048];
    snprintf(raw, sizeof raw, "%s", s);
    int have_flat = 0, flat = 0;
    char v[512];
    const char *style_keys[] = {"style", "mode"};
    for (int k = 0; k < 2; k++) {
        if (extract_word(low, raw, style_keys[k], 1, v, sizeof v)) {
            int f = 0;
            if (parse_style_value(v, &f)) {
                flat = f;
                have_flat = 1;
            }
        }
    }
    if (!have_flat) {
        if (has_word(low, "flat")) {
            flat = 1;
            have_flat = 1;
        } else if (has_word(low, "3d")) {
            flat = 0;
            have_flat = 1;
        }
    }
    if (have_flat)
        c->flat = flat;
    if (extract_word(low, raw, "color", 1, v, sizeof v)) {
        char *t = v;
        while (*t == ' ' || *t == '\t')
            t++;
        char *e = t + strlen(t);
        while (e > t && (e[-1] == ' ' || e[-1] == '\t'))
            *--e = 0;
        char l[64];
        size_t q = 0;
        while (t[q] && q + 1 < sizeof l) {
            char ch = t[q];
            l[q++] = (char)(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
        }
        l[q] = 0;
        if (!strcmp(l, "sharkvis"))
            c->live_colors = 1;
        else if (!strcmp(l, "terminal"))
            c->live_term_colors = 1;
    }
    if (extract_word(low, raw, "textcolor", 1, v, sizeof v)) {
        char *t = v;
        while (*t == ' ' || *t == '\t')
            t++;
        char l[64];
        size_t q = 0;
        while (t[q] && q + 1 < sizeof l) {
            char ch = t[q];
            l[q++] = (char)(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
        }
        l[q] = 0;
        char *e = l + strlen(l);
        while (e > l && (e[-1] == ' ' || e[-1] == '\t'))
            *--e = 0;
        if (!strcmp(l, "sharkvis") || !strcmp(l, "terminal"))
            c->text_live_colors = 1;
    }
    const char *char_keys[] = {"characters", "chars", "glyphs", "glyph", "shading",
                               "symbols", "symbol", "ramp"};
    int have_chars = 0;
    char chars_v[512] = "";
    for (int k = 0; k < 8; k++) {
        if (extract_word(low, raw, char_keys[k], 0, v, sizeof v)) {
            snprintf(chars_v, sizeof chars_v, "%s", v);
            have_chars = 1;
        }
    }
    if (extract_word(low, raw, "light", 0, v, sizeof v)) {
        float x, y, z;
        if (parse_light_value(v, &x, &y, &z)) {
            c->light_x = x;
            c->light_y = y;
            c->light_z = z;
        }
    }
    if (have_chars) {
        apply_chars_value(c, chars_v);
        c->shading_explicit = !is_sharkvis_chars_value(chars_v);
        c->chars_set = 1;
    } else if (has_word(low, "blocks") || has_word(low, "block")) {
        c->original_glyphs = 0;
        for (size_t k = 0; k < c->nshading; k++)
            free(c->shading[k]);
        free(c->shading);
        anim_default_shading(&c->shading, &c->nshading);
        c->shading_explicit = 1;
    }
    if (has_word(low, "no-sharkvis") || has_word(low, "nosharkvis")) {
        c->sharkvis = SVM_OFF;
        c->sharkvis_set = 1;
    } else if (extract_word(low, raw, "sharkvis", 1, v, sizeof v)) {
        char *t = v;
        while (*t == ' ' || *t == '\t')
            t++;
        SharkvisMode m = sv_mode_parse(t);
        if (!*t) {
            c->sharkvis = SVM_AUTO;
        } else {
            char l[32];
            snprintf(l, sizeof l, "%.*s", (int)(sizeof l - 1), t);
            for (char *p = l; *p; p++) {
                if (*p >= 'A' && *p <= 'Z')
                    *p += 32;
            }
            if (!strcmp(l, "on") || !strcmp(l, "true") || !strcmp(l, "1") ||
                !strcmp(l, "yes") || !strcmp(l, "enable") || !strcmp(l, "enabled") ||
                !strcmp(l, "off") || !strcmp(l, "false") || !strcmp(l, "0") ||
                !strcmp(l, "no") || !strcmp(l, "disable") || !strcmp(l, "disabled") ||
                !strcmp(l, "auto"))
                c->sharkvis = m;
            else {
                c->sharkvis = SVM_AUTO;
            }
        }
        c->sharkvis_set = 1;
    } else if (has_word(low, "sharkvis")) {
        c->sharkvis = SVM_AUTO;
        c->sharkvis_set = 1;
    }
    char axis_src[2048];
    blank_option_spans(low, OPTION_KEYS, 27, axis_src, sizeof axis_src);
    int has_x = strchr(axis_src, 'x') != NULL;
    int has_y = strchr(axis_src, 'y') != NULL;
    int has_z = strchr(axis_src, 'z') != NULL;
    if (strstr(low, "spin") || has_x || has_y || has_z || strstr(low, "rotate")) {
        if (has_x || has_y || has_z) {
            c->spin_x = has_x;
            c->spin_y = has_y;
            c->spin_z = has_z;
        }
    }
    float f = 0;
    if (extract_number(low, "speed_x", &f)) {
        c->speed_x = f;
        c->speed_set = 1;
    }
    if (extract_number(low, "speed_y", &f)) {
        c->speed_y = f;
        c->speed_set = 1;
    }
    if (extract_number(low, "speed_z", &f)) {
        c->speed_z = f;
        c->speed_set = 1;
    }
    if (extract_number(low, "speed", &f)) {
        c->speed = f;
        c->speed_set = 1;
    }
    if (extract_number(low, "beat", &f)) {
        if (f < 0.0f)
            f = 0.0f;
        if (f > 0.9f)
            f = 0.9f;
        c->beat_depth = f;
    }
    if (extract_number(low, "grow", &f)) {
        if (f < 0.0f)
            f = 0.0f;
        if (f > 0.3f)
            f = 0.3f;
        c->grow = f;
    }
    if (extract_number(low, "boom", &f)) {
        if (f < 0.0f)
            f = 0.0f;
        if (f > 1.0f)
            f = 1.0f;
        c->has_boom = 1;
        c->boom = f;
    }
    if (extract_number(low, "return", &f)) {
        if (f < 0.0f)
            f = 0.0f;
        c->has_return_secs = 1;
        c->return_secs = f;
    }
    if (extract_number(low, "size", &f))
        c->size = f;
    if (extract_number(low, "depth", &f)) {
        c->depth = f;
        c->depth_user_set = 1;
    }
    if (extract_number(low, "height", &f))
        c->height = (int)f;
}

typedef struct {
    float x;
    float y;
    float z;
    float nx;
    float ny;
    float nz;
    int color;
    char glyph[8];
} Point;

static void fg_payload(const char *seq, char *out, size_t n) {
    out[0] = 0;
    const char *t = seq;
    if (!strncmp(t, "\x1b[", 2))
        t += 2;
    size_t l = strlen(t);
    char *tmp = malloc(l + 1);
    memcpy(tmp, t, l + 1);
    if (l > 0 && tmp[l - 1] == 'm')
        tmp[l - 1] = 0;
    char *parts[64];
    size_t np = 0;
    char *p = tmp;
    for (;;) {
        char *semi = strchr(p, ';');
        if (semi) {
            *semi = 0;
            parts[np++] = p;
            p = semi + 1;
        } else {
            parts[np++] = p;
            break;
        }
        if (np >= 64)
            break;
    }
    char *cur = NULL;
    size_t i = 0;
    while (i < np) {
        const char *q = parts[i];
        if (!*q) {
            free(cur);
            cur = strdup("");
            i++;
        } else if (!strcmp(q, "38")) {
            if (i + 2 < np && !strcmp(parts[i + 1], "5") && parts[i + 2][0]) {
                free(cur);
                cur = malloc(strlen(parts[i + 2]) + 8);
                snprintf(cur, strlen(parts[i + 2]) + 8, "38;5;%s", parts[i + 2]);
                i += 3;
            } else if (i + 4 < np && !strcmp(parts[i + 1], "2") && parts[i + 2][0] &&
                       parts[i + 3][0] && parts[i + 4][0]) {
                free(cur);
                cur = malloc(strlen(parts[i + 2]) + strlen(parts[i + 3]) +
                             strlen(parts[i + 4]) + 16);
                snprintf(cur, strlen(parts[i + 2]) + strlen(parts[i + 3]) +
                                 strlen(parts[i + 4]) + 16,
                         "38;2;%s;%s;%s", parts[i + 2], parts[i + 3], parts[i + 4]);
                i += 5;
            } else {
                free(cur);
                cur = strdup("");
                i++;
            }
        } else {
            char *end;
            long num = strtol(q, &end, 10);
            if (end != q && !*end) {
                if ((num >= 30 && num <= 37) || (num >= 90 && num <= 97)) {
                    free(cur);
                    cur = malloc(8);
                    snprintf(cur, 8, "%ld", num);
                } else if (num == 0 || num == 39) {
                    free(cur);
                    cur = strdup("");
                }
            }
            i++;
        }
    }
    free(tmp);
    if (cur) {
        snprintf(out, n, "%s", cur);
        free(cur);
    }
}

static int pal_pos(char **palette, size_t n, const char *p) {
    for (size_t i = 0; i < n; i++) {
        if (!strcmp(palette[i], p))
            return (int)i;
    }
    return -1;
}

typedef struct {
    char *glyph;
    char *color;
} Cell;

typedef struct {
    Cell *cells;
    size_t n;
} CellRow;

static void parse_cells(const ResolvedLogo *logo, CellRow **rows_out, size_t *nrows,
                        int *has_ansi, size_t *max_cols) {
    CellRow *rows = NULL;
    size_t nr = 0;
    int ansi = 0;
    size_t mc = 0;
    for (size_t li = 0; li < logo->nlines; li++) {
        const char *s = logo->lines[li];
        size_t n = strlen(s);
        Cell *row = NULL;
        size_t rn = 0;
        char cur[64] = "";
        size_t i = 0;
        while (i < n) {
            unsigned char b = (unsigned char)s[i];
            if (b == 0x1b && i + 1 < n && s[i + 1] == '[') {
                size_t j = i + 2;
                while (j < n && !((s[j] >= 'A' && s[j] <= 'Z') || (s[j] >= 'a' && s[j] <= 'z')))
                    j++;
                if (j < n && s[j] == 'm') {
                    char seq[128];
                    size_t sl = j - (i + 2);
                    if (sl >= sizeof seq)
                        sl = sizeof seq - 1;
                    memcpy(seq, s + i + 2, sl);
                    seq[sl] = 0;
                    char p[64];
                    fg_payload(seq, p, sizeof p);
                    if (p[0])
                        ansi = 1;
                    snprintf(cur, sizeof cur, "%s", p);
                    i = j + 1;
                    continue;
                }
                i = j + 1;
                continue;
            }
            size_t al = u8len(b);
            if (i + al > n)
                al = n - i;
            int valid = 1;
            for (size_t k = 1; k < al; k++) {
                if (i + k >= n || (s[i + k] & 0xC0) != 0x80) {
                    valid = 0;
                    break;
                }
            }
            if (!valid)
                al = 1;
            row = realloc(row, (rn + 1) * sizeof(Cell));
            row[rn].glyph = malloc(al + 1);
            memcpy(row[rn].glyph, s + i, al);
            row[rn].glyph[al] = 0;
            row[rn].color = strdup(cur);
            rn++;
            i += al;
        }
        while (rn > 0) {
            char *g = row[rn - 1].glyph;
            while (*g == ' ' || *g == '\t')
                g++;
            if (*g)
                break;
            free(row[rn - 1].glyph);
            free(row[rn - 1].color);
            rn--;
        }
        if (rn > mc)
            mc = rn;
        rows = realloc(rows, (nr + 1) * sizeof(CellRow));
        rows[nr].cells = row;
        rows[nr].n = rn;
        nr++;
    }
    if (!ansi) {
        for (size_t i = 0; i < logo->ncolors; i++) {
            if (logo->colors[i][0]) {
                char p[64];
                fg_payload(logo->colors[i], p, sizeof p);
                if (p[0]) {
                    ansi = 1;
                    break;
                }
            }
        }
    }
    if (ansi) {
        for (size_t r = 0; r < nr; r++) {
            if (r >= logo->ncolors)
                break;
            if (!logo->colors[r][0])
                continue;
            char p[64];
            fg_payload(logo->colors[r], p, sizeof p);
            if (!p[0])
                continue;
            for (size_t c = 0; c < rows[r].n; c++) {
                if (!rows[r].cells[c].color[0]) {
                    free(rows[r].cells[c].color);
                    rows[r].cells[c].color = strdup(p);
                }
            }
        }
    }
    *rows_out = rows;
    *nrows = nr;
    *has_ansi = ansi;
    *max_cols = mc;
}

static void free_cells(CellRow *rows, size_t n) {
    for (size_t i = 0; i < n; i++) {
        for (size_t j = 0; j < rows[i].n; j++) {
            free(rows[i].cells[j].glyph);
            free(rows[i].cells[j].color);
        }
        free(rows[i].cells);
    }
    free(rows);
}

static float hash_unit(unsigned x) {
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return (float)x / 4294967295.0f;
}

struct LogoCloud {
    Point *points;
    size_t npoints;
    char **palette_ansi;
    size_t npalette;
    int has_ansi;
    size_t sub_rows;
    size_t sub_cols;
    float *buf_z;
    float *buf_lum;
    int *buf_col;
    char (*buf_glyph)[8];
    size_t buf_w;
    size_t buf_h;
    char **tint_rows;
    size_t ntint;
    int has_tint_key;
    int tint_has_grad;
    unsigned char tint_lo[3];
    unsigned char tint_hi[3];
    size_t tint_h;
    size_t tint_ymin;
    size_t tint_ymax;
    int tint_has_term_pal;
    unsigned char tint_term_pal[16 * 3];
};

static void build_points(CellRow *cells, size_t nrows, int has_ansi, const AnimConfig *c,
                         size_t rows, size_t cols, Point **pts_out, size_t *npts,
                         char ***pal_out, size_t *npal) {
    char **palette = NULL;
    size_t npl = 0;
    if (!has_ansi) {
        palette = malloc(sizeof(char *));
        palette[0] = strdup("39");
        npl = 1;
    } else {
        for (size_t r = 0; r < nrows; r++) {
            for (size_t k = 0; k < cells[r].n; k++) {
                const char *p = cells[r].cells[k].color;
                if (p[0] && pal_pos(palette, npl, p) < 0) {
                    palette = realloc(palette, (npl + 1) * sizeof(char *));
                    palette[npl++] = strdup(p);
                }
            }
        }
    }
    if (rows == 0 || cols == 0) {
        *pts_out = NULL;
        *npts = 0;
        *pal_out = palette;
        *npal = npl;
        return;
    }
    float cx = ((float)cols - 1.0f) * 0.5f;
    float cy = ((float)rows - 1.0f) * 0.5f;
    float *hmap = calloc(rows * cols, sizeof(float));
    for (size_t r = 0; r < rows; r++) {
        for (size_t cc = 0; cc < cols; cc++) {
            if (r < nrows && cc < cells[r].n)
                hmap[r * cols + cc] = char_weight_utf8(cells[r].cells[cc].glyph);
        }
    }
    float effective_depth = c->depth;
    if (!c->depth_user_set) {
        double sum = 0, sum2 = 0;
        size_t n = 0;
        for (size_t r = 0; r < rows; r++) {
            for (size_t cc = 0; cc < cols; cc++) {
                float h = hmap[r * cols + cc];
                if (h > 0.0f) {
                    sum += h;
                    sum2 += h * h;
                    n++;
                }
            }
        }
        if (n > 0) {
            double mean = sum / n;
            double var = sum2 / n - mean * mean;
            double std = var > 0.0 ? sqrt(var) : 0.0;
            if (std < 0.25) {
                double boost = 1.0 + 2.0 * (0.25 - std) / 0.25;
                effective_depth *= (float)boost;
            }
        }
    }
    float zmax = 0.18f * effective_depth;
    float *gnx = calloc(rows * cols, sizeof(float));
    float *gny = calloc(rows * cols, sizeof(float));
    float *gnz = calloc(rows * cols, sizeof(float));
    for (size_t r = 0; r < rows; r++) {
        for (size_t cc = 0; cc < cols; cc++) {
            if (hmap[r * cols + cc] <= 0.0f) {
                gnz[r * cols + cc] = 1.0f;
                continue;
            }
            float dhdx, dhdy;
            if (cc > 0 && cc + 1 < cols)
                dhdx = (hmap[r * cols + cc + 1] - hmap[r * cols + cc - 1]) * 0.5f;
            else if (cc == 0 && cols > 1)
                dhdx = hmap[r * cols + cc + 1] - hmap[r * cols + cc];
            else if (cols > 1)
                dhdx = hmap[r * cols + cc] - hmap[r * cols + cc - 1];
            else
                dhdx = 0.0f;
            if (r > 0 && r + 1 < rows)
                dhdy = (hmap[(r + 1) * cols + cc] - hmap[(r - 1) * cols + cc]) * 0.5f;
            else if (r == 0 && rows > 1)
                dhdy = hmap[(r + 1) * cols + cc] - hmap[r * cols + cc];
            else if (rows > 1)
                dhdy = hmap[r * cols + cc] - hmap[(r - 1) * cols + cc];
            else
                dhdy = 0.0f;
            float nx = -dhdx / 0.07f;
            float ny = dhdy / 0.14f;
            float l = sqrtf(nx * nx + ny * ny + 1.0f);
            if (l > 1e-6f) {
                gnx[r * cols + cc] = nx / l;
                gny[r * cols + cc] = ny / l;
                gnz[r * cols + cc] = 1.0f / l;
            } else {
                gnz[r * cols + cc] = 1.0f;
            }
        }
    }
    float sz = c->size;
    if (!(sz == sz) || sz < 0.1f)
        sz = 0.1f;
    if (sz > 4.0f)
        sz = 4.0f;
    int z_layers = (int)(6.0f * sz);
    if (z_layers < 6)
        z_layers = 6;
    if (z_layers > 24)
        z_layers = 24;
    size_t sbr = c->original_glyphs ? 1 : 2;
    size_t subdiv;
    if (c->original_glyphs) {
        subdiv = 1;
    } else {
        subdiv = (size_t)(sz * (float)sbr);
    }
    if (subdiv < 1)
        subdiv = 1;
    if (subdiv > 4)
        subdiv = 4;
    Point *points = NULL;
    size_t npoints = 0, pcap = 0;
    for (size_t row = 0; row < rows; row++) {
        for (size_t col = 0; col < cols; col++) {
            float h = hmap[row * cols + col];
            if (h <= 0.0f)
                continue;
            const char *gstr = (row < nrows && col < cells[row].n) ? cells[row].cells[col].glyph : " ";
            for (size_t sr = 0; sr < subdiv; sr++) {
                for (size_t sc = 0; sc < subdiv; sc++) {
                    float frow = (float)row + (float)sr / (float)subdiv;
                    float fcol = (float)col + (float)sc / (float)subdiv;
                    float ih;
                    if (sr == 0 && sc == 0) {
                        ih = h;
                    } else {
                        float fr = (float)sr / (float)subdiv;
                        float fc = (float)sc / (float)subdiv;
                        size_t nr = row + 1;
                        if (nr >= rows)
                            nr = rows - 1;
                        size_t nc = col + 1;
                        if (nc >= cols)
                            nc = cols - 1;
                        float h00 = hmap[row * cols + col];
                        float h10 = hmap[nr * cols + col];
                        float h01 = hmap[row * cols + nc];
                        float h11 = hmap[nr * cols + nc];
                        float v = h00 * (1 - fr) * (1 - fc) + h10 * fr * (1 - fc) +
                                  h01 * (1 - fr) * fc + h11 * fr * fc;
                        if (v <= 0.0f)
                            continue;
                        ih = v;
                    }
                    if (ih <= 0.0f)
                        continue;
                    float ox = (fcol - cx) * 0.07f;
                    float oy = (cy - frow) * 0.14f;
                    float zr = ih * zmax;
                    if (c->flat) {
                        if (npoints >= ANIM_MAX_POINTS)
                            break;
                        int col_val;
                        if (has_ansi) {
                            const char *p2 = (row < nrows && col < cells[row].n)
                                                 ? cells[row].cells[col].color
                                                 : "";
                            col_val = p2[0] ? pal_pos(palette, npl, p2) : -1;
                        } else {
                            col_val = 0;
                        }
                        if (npoints >= pcap) {
                            pcap = pcap ? pcap * 2 : 1024;
                            points = realloc(points, pcap * sizeof(Point));
                        }
                        Point *pt = &points[npoints++];
                        pt->x = ox;
                        pt->y = oy;
                        pt->z = 0.0f;
                        pt->nx = 0.0f;
                        pt->ny = 0.0f;
                        pt->nz = 1.0f;
                        pt->color = col_val;
                        size_t gl = strlen(gstr);
                        if (gl > 7)
                            gl = 7;
                        memcpy(pt->glyph, gstr, gl);
                        pt->glyph[gl] = 0;
                        continue;
                    }
                    int is_edge = 0;
                    for (int dr = -1; dr <= 1 && !is_edge; dr++) {
                        for (int dc = -1; dc <= 1; dc++) {
                            if (dr == 0 && dc == 0)
                                continue;
                            long nr = (long)row + dr;
                            long nc = (long)col + dc;
                            float nh = 0.0f;
                            if (nr >= 0 && nr < (long)rows && nc >= 0 && nc < (long)cols)
                                nh = hmap[(size_t)nr * cols + (size_t)nc];
                            if (nh <= 0.0f) {
                                is_edge = 1;
                                break;
                            }
                        }
                    }
                    int layers = (is_edge || ih < 0.15f) ? 2 : z_layers;
                    if (layers < 2)
                        continue;
                    for (int k = 0; k < layers; k++) {
                        if (npoints >= ANIM_MAX_POINTS)
                            break;
                        float pz;
                        if (k == 0) {
                            pz = -zr;
                        } else if (k == layers - 1) {
                            pz = zr;
                        } else {
                            unsigned seed = (unsigned)row * 73856093u ^
                                            (unsigned)col * 19349663u ^ (unsigned)sr * 83492791u ^
                                            (unsigned)sc * 2971215073u ^ (unsigned)k * 91138233u;
                            uint32_t zbits;
                            memcpy(&zbits, &zr, 4);
                            seed ^= zbits;
                            pz = (hash_unit(seed) - 0.5f) * 2.0f * zr;
                        }
                        int col_val;
                        if (has_ansi) {
                            const char *p2 = (row < nrows && col < cells[row].n)
                                                 ? cells[row].cells[col].color
                                                 : "";
                            col_val = p2[0] ? pal_pos(palette, npl, p2) : -1;
                        } else {
                            col_val = 0;
                        }
                        float nx, ny, nz;
                        if (k == 0) {
                            nx = gnx[row * cols + col];
                            ny = gny[row * cols + col];
                            nz = -gnz[row * cols + col];
                        } else if (k == layers - 1) {
                            nx = gnx[row * cols + col];
                            ny = gny[row * cols + col];
                            nz = gnz[row * cols + col];
                        } else {
                            float ex = 0.0f, ey = 0.0f;
                            for (int dr = -1; dr <= 1; dr++) {
                                for (int dc = -1; dc <= 1; dc++) {
                                    if (dr == 0 && dc == 0)
                                        continue;
                                    long nr = (long)row + dr;
                                    long nc = (long)col + dc;
                                    float nh = 0.0f;
                                    if (nr >= 0 && nr < (long)rows && nc >= 0 &&
                                        nc < (long)cols)
                                        nh = hmap[(size_t)nr * cols + (size_t)nc];
                                    if (nh < h) {
                                        ex += (float)dc;
                                        ey += -(float)dr;
                                    }
                                }
                            }
                            float el = sqrtf(ex * ex + ey * ey);
                            if (el > 1e-6f) {
                                ex /= el;
                                ey /= el;
                            }
                            float tn = (float)k / (float)(layers - 1) * 2.0f - 1.0f;
                            float side = sqrtf(1.0f - tn * tn);
                            if (side < 0.0f)
                                side = 0.0f;
                            nx = ex * side;
                            ny = ey * side;
                            nz = tn;
                        }
                        if (npoints >= pcap) {
                            pcap = pcap ? pcap * 2 : 1024;
                            points = realloc(points, pcap * sizeof(Point));
                        }
                        Point *pt = &points[npoints++];
                        pt->x = ox;
                        pt->y = oy;
                        pt->z = pz;
                        pt->nx = nx;
                        pt->ny = ny;
                        pt->nz = nz;
                        pt->color = col_val;
                        size_t gl = strlen(gstr);
                        if (gl > 7)
                            gl = 7;
                        memcpy(pt->glyph, gstr, gl);
                        pt->glyph[gl] = 0;
                    }
                }
            }
        }
    }
    free(hmap);
    free(gnx);
    free(gny);
    free(gnz);
    *pts_out = points;
    *npts = npoints;
    *pal_out = palette;
    *npal = npl;
}

LogoCloud *anim_build_cloud(const ResolvedLogo *logo, const AnimConfig *config) {
    if (logo->nlines == 0)
        return NULL;
    CellRow *rows = NULL;
    size_t nrows = 0;
    int has_ansi = 0;
    size_t max_cols = 0;
    parse_cells(logo, &rows, &nrows, &has_ansi, &max_cols);
    if (nrows == 0 || max_cols == 0) {
        free_cells(rows, nrows);
        return NULL;
    }
    Point *points = NULL;
    size_t npoints = 0;
    char **palette = NULL;
    size_t npalette = 0;
    build_points(rows, nrows, has_ansi, config, nrows, max_cols, &points, &npoints,
                 &palette, &npalette);
    free_cells(rows, nrows);
    if (npoints == 0) {
        for (size_t i = 0; i < npalette; i++)
            free(palette[i]);
        free(palette);
        free(points);
        return NULL;
    }
    size_t sr = config->original_glyphs ? 1 : 2;
    size_t sc = sr;
    LogoCloud *c = calloc(1, sizeof *c);
    c->points = points;
    c->npoints = npoints;
    c->has_ansi = has_ansi;
    c->sub_rows = sr;
    c->sub_cols = sc;
    c->palette_ansi = malloc((npalette ? npalette : 1) * sizeof(char *));
    c->npalette = npalette;
    for (size_t i = 0; i < npalette; i++) {
        char tmp[128];
        snprintf(tmp, sizeof tmp, "\x1b[1;%sm", palette[i]);
        c->palette_ansi[i] = strdup(tmp);
        free(palette[i]);
    }
    free(palette);
    return c;
}

void anim_cloud_free(LogoCloud *c) {
    size_t i;
    if (!c)
        return;
    free(c->points);
    for (i = 0; i < c->npalette; i++)
        free(c->palette_ansi[i]);
    free(c->palette_ansi);
    free(c->buf_z);
    free(c->buf_lum);
    free(c->buf_col);
    free(c->buf_glyph);
    for (i = 0; i < c->ntint; i++)
        free(c->tint_rows[i]);
    free(c->tint_rows);
    free(c);
}

void anim_stereo_spin(float left, float right, float *yaw, float *pitch) {
    float l = left < 0 ? 0 : (left > 1 ? 1 : left);
    float r = right < 0 ? 0 : (right > 1 ? 1 : right);
    float sum = l + r;
    if (sum < 1e-3f) {
        *yaw = 0.0f;
        *pitch = 0.0f;
        return;
    }
    float bal = (r - l) / sum;
    float mag = sum * 0.5f;
    if (mag < 0.0f)
        mag = 0.0f;
    if (mag > 1.0f)
        mag = 1.0f;
    if (mag < 0.04f) {
        *yaw = 0.0f;
        *pitch = 0.0f;
        return;
    }
    float ab = bal < 0 ? -bal : bal;
    *yaw = bal * mag * 0.20f;
    *pitch = (1.0f - ab) * mag * 0.14f;
}

double anim_ease_to_root(double phase, float dt) {
    double tau = 6.283185307179586;
    double target = round(phase / tau) * tau;
    double diff = target - phase;
    double step = 3.141592653589793 * (dt > 0 ? dt : 0);
    if (diff < 0 ? -diff <= step : diff <= step)
        return target;
    return phase + (diff > 0 ? step : -step);
}

void render_fx_none(RenderFx *fx) {
    memset(fx, 0, sizeof *fx);
    fx->scale = 1.0f;
}

void render_fx_free(RenderFx *fx) {
    for (size_t i = 0; i < fx->nshading; i++)
        free(fx->shading[i]);
    free(fx->shading);
    fx->shading = NULL;
    fx->nshading = 0;
}

int render_fx_is_none(const RenderFx *fx) {
    float d = fx->scale - 1.0f;
    if (d < 0)
        d = -d;
    if (fx->has_grad || fx->has_shading || d >= 1e-6f)
        return 0;
    for (int i = 0; i < 3; i++) {
        float a = fx->audio[i] < 0 ? -fx->audio[i] : fx->audio[i];
        if (a >= 1e-6f)
            return 0;
    }
    return 1;
}

void anim_resolved_free(ResolvedLogo *r) {
    resolved_logo_free(r);
}

static void push_color_str(JfBuf *line, char **palette_ansi, size_t npal, int has_ansi,
                           int c, int *prev_color, const char *tint_esc) {
    if (tint_esc) {
        if (*prev_color != -100) {
            if (*prev_color != -2 && *prev_color != -1)
                jf_buf_put(line, "\x1b[0m");
            jf_buf_put(line, tint_esc);
            *prev_color = -100;
        }
        return;
    }
    if (c != *prev_color) {
        if (*prev_color != -2 && *prev_color != -1)
            jf_buf_put(line, "\x1b[0m");
        if (c >= 0) {
            if ((size_t)c < npal)
                jf_buf_put(line, palette_ansi[c]);
        } else if (has_ansi) {
            jf_buf_put(line, "\x1b[0m");
        } else {
            jf_buf_put(line, "\x1b[1;35m");
        }
        *prev_color = c;
    }
}

static ResolvedLogo *render_cloud_with_fx(LogoCloud *cloud, double frame,
                                               const AnimConfig *config,
                                               size_t render_height,
                                               size_t info_line_count,
                                               const RenderFx *fx) {
    size_t sub_rows = cloud->sub_rows;
    size_t sub_cols = cloud->sub_cols;
    int has_ansi = cloud->has_ansi;
    size_t rh = render_height > 1 ? render_height : 1;
    size_t logo_height = rh < (size_t)(60 * 3 / 5) ? rh : (size_t)(60 * 3 / 5);
    if (logo_height < 1)
        logo_height = 1;
    float zoom = 1.0f;
    if (fx->scale == fx->scale && fx->scale > 0.0f) {
        zoom = fx->scale;
        if (zoom < 0.5f)
            zoom = 0.5f;
        if (zoom > 2.0f)
            zoom = 2.0f;
    }
    float k1 = 37.0f * (float)logo_height / 36.0f * zoom;
    float half_aw = 60.0f * 0.5f;
    size_t w = 60;
    size_t h = rh;
    size_t sw = w * sub_cols;
    size_t sh = h * sub_rows;
    if (sw == 0 || sh == 0 || sw > 4096 || sh > 4096)
        return NULL;
    if (cloud->buf_w != sw || cloud->buf_h != sh) {
        float *nz = calloc(sh * sw, sizeof(float));
        float *nl = calloc(sh * sw, sizeof(float));
        int *nc = calloc(sh * sw, sizeof(int));
        char(*ng)[8] = calloc(sh * sw, sizeof(*cloud->buf_glyph));
        if (!nz || !nl || !nc || !ng) {
            free(nz);
            free(nl);
            free(nc);
            free(ng);
            return NULL;
        }
        free(cloud->buf_z);
        free(cloud->buf_lum);
        free(cloud->buf_col);
        free(cloud->buf_glyph);
        cloud->buf_z = nz;
        cloud->buf_lum = nl;
        cloud->buf_col = nc;
        cloud->buf_glyph = ng;
        cloud->buf_w = sw;
        cloud->buf_h = sh;
        for (size_t i = 0; i < sh * sw; i++)
            cloud->buf_glyph[i][0] = ' ';
    } else {
        if (!cloud->buf_z || !cloud->buf_lum || !cloud->buf_col || !cloud->buf_glyph)
            return NULL;
        for (size_t i = 0; i < sh * sw; i++) {
            cloud->buf_z[i] = 0.0f;
            cloud->buf_lum[i] = 0.0f;
            cloud->buf_col[i] = 0;
            cloud->buf_glyph[i][0] = ' ';
            cloud->buf_glyph[i][1] = 0;
        }
    }
    float *zbuf = cloud->buf_z;
    float *lumbuf = cloud->buf_lum;
    int *colorbuf = cloud->buf_col;
    char (*glyphbuf)[8] = cloud->buf_glyph;
    float fps = anim_auto_fps(config);
    float mul = (float)frame * 12.0f / fps;
    float ax = config->spin_x ? mul * 0.04f * config->speed * config->speed_x + fx->audio[0] : 0.0f;
    float ay = config->spin_y ? mul * 0.06f * config->speed * config->speed_y + fx->audio[1] : 0.0f;
    float az = config->spin_z ? mul * 0.05f * config->speed * config->speed_z + fx->audio[2] : 0.0f;
    float ca = cosf(ax), sa = sinf(ax);
    float cb = cosf(ay), sb = sinf(ay);
    float cc = cosf(az), sc = sinf(az);
    float lx = config->light_x, ly = config->light_y, lz = config->light_z;
    float hx0 = lx, hy0 = ly, hz0 = lz - 1.0f;
    float hl0 = sqrtf(hx0 * hx0 + hy0 * hy0 + hz0 * hz0);
    float hlx, hly, hlz;
    if (hl0 > 1e-6f) {
        hlx = hx0 / hl0;
        hly = hy0 / hl0;
        hlz = hz0 / hl0;
    } else {
        hlx = 0.0f;
        hly = 0.0f;
        hlz = -1.0f;
    }
    float y_center;
    if (info_line_count > 0 && info_line_count + 2 <= rh)
        y_center = 1.0f + (float)info_line_count * 0.5f;
    else
        y_center = (float)h * 0.5f;
    float k1x2 = k1 * 2.0f;
    size_t ink_top = h, ink_bot = 0;
    for (size_t pi = 0; pi < cloud->npoints; pi++) {
        const Point *pt = &cloud->points[pi];
        float y1 = pt->y * ca - pt->z * sa;
        float z1 = pt->y * sa + pt->z * ca;
        float x2 = pt->x * cb + z1 * sb;
        float z2 = -pt->x * sb + z1 * cb;
        float y2 = y1;
        float ny1 = pt->ny * ca - pt->nz * sa;
        float nz1 = pt->ny * sa + pt->nz * ca;
        float nx2 = pt->nx * cb + nz1 * sb;
        float nz2 = -pt->nx * sb + nz1 * cb;
        float ny2 = ny1;
        float x3 = x2 * cc - y2 * sc;
        float y3 = x2 * sc + y2 * cc;
        float z3 = z2;
        float nx3 = nx2 * cc - ny2 * sc;
        float ny3 = nx2 * sc + ny2 * cc;
        float nz3 = nz2;
        float zc = z3 + 5.5f;
        if (zc < 0.1f)
            continue;
        float ooz = 1.0f / zc;
        int xs = (int)((half_aw + k1x2 * x3 * ooz) * (float)sub_cols);
        int ys = (int)((y_center - k1 * y3 * ooz) * (float)sub_rows);
        if (xs < 0 || xs >= (int)sw || ys < 0 || ys >= (int)sh)
            continue;
        size_t idx = (size_t)ys * sw + (size_t)xs;
        if (ooz > zbuf[idx]) {
            float diff = nx3 * lx + ny3 * ly + nz3 * lz;
            if (diff < 0.0f)
                diff = 0.0f;
            float sd = nx3 * hlx + ny3 * hly + nz3 * hlz;
            if (sd < 0.0f)
                sd = 0.0f;
            float spec = sd * sd;
            spec = spec * spec;
            spec = spec * spec;
            float lum = 0.08f + 0.62f * diff + 0.30f * spec;
            if (lum > 1.0f)
                lum = 1.0f;
            zbuf[idx] = ooz;
            lumbuf[idx] = lum;
            colorbuf[idx] = pt->color;
            memcpy(glyphbuf[idx], pt->glyph, 8);
            size_t yr = (size_t)ys / sub_rows;
            if (yr < ink_top)
                ink_top = yr;
            if (yr > ink_bot)
                ink_bot = yr;
        }
    }
    const char **shading;
    size_t scount;
    if (fx->has_shading && fx->nshading > 0) {
        shading = (const char **)fx->shading;
        scount = fx->nshading;
    } else {
        shading = (const char **)config->shading;
        scount = config->nshading;
    }
    if (scount < 1)
        scount = 1;
    size_t smax = scount - 1;
    size_t total_sub = sub_rows * sub_cols;
    int tint_now = fx->has_grad ? 1 : 0;
    int tint_pal_changed = 0;
    if (tint_now) {
        if (!!cloud->tint_has_term_pal != !!fx->has_term_pal)
            tint_pal_changed = 1;
        else if (fx->has_term_pal) {
            for (int pi = 0; pi < 16 && !tint_pal_changed; pi++) {
                if (cloud->tint_term_pal[pi * 3] != (unsigned char)fx->term_pal[pi].r ||
                    cloud->tint_term_pal[pi * 3 + 1] != (unsigned char)fx->term_pal[pi].g ||
                    cloud->tint_term_pal[pi * 3 + 2] != (unsigned char)fx->term_pal[pi].b)
                    tint_pal_changed = 1;
            }
        }
    }
    if (cloud->has_tint_key != (tint_now ? 1 : -1) || tint_pal_changed ||
        (tint_now && (cloud->tint_h != h || cloud->tint_ymin != ink_top ||
                      cloud->tint_ymax != ink_bot ||
                      memcmp(cloud->tint_lo, fx->grad_lo, 3) != 0 ||
                      memcmp(cloud->tint_hi, fx->grad_hi, 3) != 0))) {
        for (size_t i = 0; i < cloud->ntint; i++)
            free(cloud->tint_rows[i]);
        free(cloud->tint_rows);
        cloud->tint_rows = NULL;
        cloud->ntint = 0;
        if (tint_now) {
            if (h == 0 || h > 512) {
                cloud->has_tint_key = tint_now ? 1 : -1;
                cloud->tint_ymin = ink_top;
                cloud->tint_ymax = ink_bot;
            } else {
                char **rows_new = malloc(h * sizeof(char *));
                if (!rows_new) {
                    cloud->has_tint_key = -1;
                } else {
                    size_t built = 0;
                    int fail = 0;
                    for (size_t y = 0; y < h; y++) {
                        /* Full gradient across the visible ink, like
                         * sharkvis bars: endpoints land on the logo's
                         * own top/bottom rows so neither color sits far
                         * away in empty space. Flat (lo==hi) falls out
                         * of the lerp naturally. */
                        float t;
                        if (ink_bot > ink_top) {
                            size_t yc = y < ink_top ? ink_top : (y > ink_bot ? ink_bot : y);
                            t = (float)(ink_bot - yc) / (float)(ink_bot - ink_top);
                        } else {
                            t = 0.5f;
                        }
                        Rgb lo8 = {(uint8_t)(fx->grad_lo[0] + 0.5f),
                                   (uint8_t)(fx->grad_lo[1] + 0.5f),
                                   (uint8_t)(fx->grad_lo[2] + 0.5f)};
                        Rgb hi8 = {(uint8_t)(fx->grad_hi[0] + 0.5f),
                                   (uint8_t)(fx->grad_hi[1] + 0.5f),
                                   (uint8_t)(fx->grad_hi[2] + 0.5f)};
                        Rgb tc = sv_lerp_rgb(lo8, hi8, t);
                        char tmp[32];
                        sv_live_esc(fx->has_term_pal ? fx->term_pal : NULL, tc, tmp,
                                    sizeof tmp);
                        size_t l = strlen(tmp);
                        rows_new[y] = malloc(l + 1);
                        if (!rows_new[y]) {
                            fail = 1;
                            break;
                        }
                        memcpy(rows_new[y], tmp, l + 1);
                        built++;
                    }
                    if (fail) {
                        for (size_t k = 0; k < built; k++)
                            free(rows_new[k]);
                        free(rows_new);
                        cloud->has_tint_key = -1;
                    } else {
                        cloud->tint_rows = rows_new;
                        cloud->ntint = h;
                        memcpy(cloud->tint_lo, fx->grad_lo, 3);
                        memcpy(cloud->tint_hi, fx->grad_hi, 3);
                        cloud->tint_h = h;
                        cloud->tint_ymin = ink_top;
                        cloud->tint_ymax = ink_bot;
                        cloud->tint_has_term_pal = fx->has_term_pal ? 1 : 0;
                        if (fx->has_term_pal) {
                            for (int pi = 0; pi < 16; pi++) {
                                cloud->tint_term_pal[pi * 3] = fx->term_pal[pi].r;
                                cloud->tint_term_pal[pi * 3 + 1] = fx->term_pal[pi].g;
                                cloud->tint_term_pal[pi * 3 + 2] = fx->term_pal[pi].b;
                            }
                        }
                        cloud->has_tint_key = 1;
                    }
                }
            }
        } else {
            cloud->has_tint_key = -1;
        }
    }
    ResolvedLogo *res = calloc(1, sizeof *res);
    if (!res)
        return NULL;
    res->lines = malloc(h * sizeof(char *));
    res->colors = calloc(1, sizeof(char *));
    if (!res->lines || !res->colors) {
        free(res->lines);
        free(res->colors);
        free(res);
        return NULL;
    }
    res->colors[0] = strdup("");
    if (!res->colors[0]) {
        free(res->lines);
        free(res->colors);
        free(res);
        return NULL;
    }
    res->ncolors = 1;
    for (size_t row = 0; row < h; row++) {
        JfBuf line;
        memset(&line, 0, sizeof line);
        int prev_color = -2;
        for (size_t col = 0; col < w; col++) {
            if (config->original_glyphs) {
                size_t idx = row * sw + col;
                if (zbuf[idx] <= 0.0f) {
                    if (prev_color != -2 && prev_color != -1) {
                        jf_buf_put(&line, "\x1b[0m");
                        prev_color = -1;
                    }
                    jf_buf_putc(&line, ' ');
                    continue;
                }
                const char *tint = (tint_now && row < cloud->ntint) ? cloud->tint_rows[row] : NULL;
                push_color_str(&line, cloud->palette_ansi, cloud->npalette, has_ansi,
                               colorbuf[idx], &prev_color, tint);
                jf_buf_put(&line, glyphbuf[idx]);
                continue;
            }
            size_t n = 0;
            float lsum = 0.0f;
            int vc[4] = {-2, -2, -2, -2};
            size_t vn[4] = {0, 0, 0, 0};
            float vz[4] = {0, 0, 0, 0};
            size_t vk = 0;
            for (size_t sr = 0; sr < sub_rows; sr++) {
                for (size_t sc2 = 0; sc2 < sub_cols; sc2++) {
                    size_t idx = (row * sub_rows + sr) * sw + (col * sub_cols + sc2);
                    float z = zbuf[idx];
                    if (z > 0.0f) {
                        lsum += lumbuf[idx];
                        n++;
                        int cc2 = colorbuf[idx];
                        size_t fi = vk;
                        for (size_t k = 0; k < vk; k++) {
                            if (vc[k] == cc2) {
                                fi = k;
                                break;
                            }
                        }
                        if (fi == vk && vk < 4) {
                            vc[vk] = cc2;
                            vk++;
                        }
                        if (fi < 4) {
                            vn[fi]++;
                            if (z > vz[fi])
                                vz[fi] = z;
                        }
                    }
                }
            }
            int best_c = 0;
            size_t vote_n = 0;
            float vote_z = -1.0f;
            for (size_t k = 0; k < vk; k++) {
                if (vn[k] > vote_n || (vn[k] == vote_n && vz[k] > vote_z)) {
                    vote_n = vn[k];
                    vote_z = vz[k];
                    best_c = vc[k];
                }
            }
            if (n == 0) {
                if (prev_color != -2 && prev_color != -1) {
                    jf_buf_put(&line, "\x1b[0m");
                    prev_color = -1;
                }
                jf_buf_putc(&line, ' ');
                continue;
            }
            float coverage = (float)n / (float)total_sub;
            float ink = lsum / (float)n * coverage;
            size_t ci = (size_t)(ink * (float)smax + 0.5f);
            if (ci > smax)
                ci = smax;
            const char *tint = (tint_now && row < cloud->ntint) ? cloud->tint_rows[row] : NULL;
            push_color_str(&line, cloud->palette_ansi, cloud->npalette, has_ansi,
                           best_c, &prev_color, tint);
            jf_buf_put(&line, shading[ci]);
        }
        if (prev_color != -2 && prev_color != -1)
            jf_buf_put(&line, "\x1b[0m");
        res->lines[res->nlines++] = line.data ? line.data : strdup("");
    }
    res->width = w;
    res->padding_right = 2;
    return res;
}

ResolvedLogo *anim_render_cloud(LogoCloud *cloud, double frame,
                                const AnimConfig *config, size_t render_height,
                                size_t info_line_count) {
    RenderFx fx;
    render_fx_none(&fx);
    return render_cloud_with_fx(cloud, frame, config, render_height, info_line_count,
                                &fx);
}

ResolvedLogo *anim_render_cloud_with_fx(LogoCloud *cloud, double frame,
                                        const AnimConfig *config,
                                        size_t render_height,
                                        size_t info_line_count,
                                        const RenderFx *fx) {
    return render_cloud_with_fx(cloud, frame, config, render_height, info_line_count, fx);
}

ResolvedLogo *anim_render_frame(const ResolvedLogo *logo, double frame,
                                const AnimConfig *config, size_t render_height,
                                size_t info_line_count) {
    RenderFx fx;
    render_fx_none(&fx);
    return anim_render_frame_with_fx(logo, frame, config, render_height, info_line_count,
                                     &fx);
}

ResolvedLogo *anim_render_frame_with_tint(const ResolvedLogo *logo, double frame,
                                          const AnimConfig *config,
                                          size_t render_height,
                                          size_t info_line_count,
                                          const unsigned char tint[3]) {
    RenderFx fx;
    render_fx_none(&fx);
    fx.has_grad = 1;
    fx.grad_lo[0] = tint[0];
    fx.grad_lo[1] = tint[1];
    fx.grad_lo[2] = tint[2];
    fx.grad_hi[0] = tint[0];
    fx.grad_hi[1] = tint[1];
    fx.grad_hi[2] = tint[2];
    return anim_render_frame_with_fx(logo, frame, config, render_height, info_line_count,
                                     &fx);
}

ResolvedLogo *anim_render_frame_with_fx(const ResolvedLogo *logo, double frame,
                                        const AnimConfig *config,
                                        size_t render_height,
                                        size_t info_line_count,
                                        const RenderFx *fx) {
    LogoCloud *cloud = NULL;
    if (logo) {
        cloud = anim_build_cloud(logo, config);
        if (!cloud) {
            ResolvedLogo *r = calloc(1, sizeof *r);
            if (logo->nlines) {
                r->lines = malloc(logo->nlines * sizeof(char *));
                for (size_t i = 0; i < logo->nlines; i++)
                    r->lines[i] = strdup(logo->lines[i]);
                r->nlines = logo->nlines;
            }
            r->width = logo->width;
            r->padding_right = 2;
            return r;
        }
    } else {
        return NULL;
    }
    ResolvedLogo *r = render_cloud_with_fx(cloud, frame, config, render_height,
                                           info_line_count, fx);
    anim_cloud_free(cloud);
    return r;
}
