#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "anim.h"
#include "app.h"
#include "common.h"
#include "config.h"
#include "detect.h"
#include "logo.h"
#include "logo_image.h"
#include "modules.h"
#include "print.h"
#include "sharkvis_sync.h"

void app_init(App *app) {
    memset(app, 0, sizeof *app);
    config_default(&app->config);
}

static void free_options(CliOptions *o) {
    free(o->structure);
    for (size_t i = 0; i < o->ndisabled; i++)
        free(o->structure_disabled[i]);
    free(o->structure_disabled);
    free(o->config_path);
    free(o->logo_name);
    memset(o, 0, sizeof *o);
}

void app_free(App *app) {
    free_options(&app->options);
    config_free(&app->config);
    anim_resolved_free(app->logo);
    app->logo = NULL;
}

char **app_config_search_dirs(size_t *n) {
    char **out = NULL;
    size_t m = 0;
    *n = 0;
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg) {
        out = malloc(sizeof(char *));
        out[m++] = strdup(xdg);
    } else if (home) {
        char p[1152];
        snprintf(p, sizeof p, "%s/.config", home);
        out = malloc(sizeof(char *));
        out[m++] = strdup(p);
    }
    if (home) {
        char p[1152];
        snprintf(p, sizeof p, "%s/.config", home);
        out = realloc(out, (m + 1) * sizeof(char *));
        out[m++] = strdup(p);
    }
    *n = m;
    return out;
}

void app_free_strs(char **p, size_t n) {
    for (size_t i = 0; i < n; i++)
        free(p[i]);
    free(p);
}

static JfConfig *load_config_file_into(const char *path, JfConfig *out) {
    char *text = NULL;
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > 10000000) {
        fclose(f);
        return NULL;
    }
    text = malloc((size_t)sz + 1);
    if (fread(text, 1, (size_t)sz, f) != (size_t)sz) {
        free(text);
        fclose(f);
        return NULL;
    }
    text[sz] = 0;
    fclose(f);
    char err[256];
    JfConfig cfg;
    config_default(&cfg);
    if (!config_from_jsonc(&cfg, text, err, sizeof err)) {
        free(text);
        config_free(&cfg);
        return NULL;
    }
    free(text);
    cfg.loaded_from = strdup(path);
    config_free(out);
    *out = cfg;
    return out;
}

void app_load_config(App *app) {
    if (app->options.no_config)
        return;
    if (app->options.config_path) {
        JfConfig cfg;
        config_default(&cfg);
        if (load_config_file_into(app->options.config_path, &cfg))
            app->config = cfg;
        else
            config_free(&cfg);
        return;
    }
    size_t n = 0;
    char **dirs = app_config_search_dirs(&n);
    for (size_t i = 0; i < n; i++) {
        char p[1280];
        snprintf(p, sizeof p, "%s/jefetch/config.jsonc", dirs[i]);
        JfConfig cfg;
        config_default(&cfg);
        if (load_config_file_into(p, &cfg)) {
            app->config = cfg;
            break;
        }
        config_free(&cfg);
    }
    app_free_strs(dirs, n);
}

char *app_ensure_default_config(App *app) {
    (void)app;
    size_t n = 0;
    char **dirs = app_config_search_dirs(&n);
    if (n == 0)
        return NULL;
    char path[1280];
    snprintf(path, sizeof path, "%s/jefetch/config.jsonc", dirs[0]);
    char *ret = NULL;
    char *content = NULL;
    FILE *f = fopen(path, "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        content = malloc((size_t)(sz > 0 ? sz : 0) + 1);
        if (sz > 0)
            fread(content, 1, (size_t)sz, f);
        content[sz > 0 ? sz : 0] = 0;
        fclose(f);
        char *t = content;
        while (*t == ' ' || *t == '\t' || *t == '\n' || *t == '\r')
            t++;
        if (!*t) {
            char bak[1408];
            snprintf(bak, sizeof bak, "%s.bak", path);
            FILE *src = fopen(path, "r");
            FILE *dst = fopen(bak, "w");
            if (src && dst) {
                char buf[4096];
                size_t k;
                while ((k = fread(buf, 1, sizeof buf, src)) > 0)
                    fwrite(buf, 1, k, dst);
            }
            if (src)
                fclose(src);
            if (dst)
                fclose(dst);
            char dir[1280];
            snprintf(dir, sizeof dir, "%s/jefetch", dirs[0]);
            char cur[1280];
            size_t cn = 0;
            if (dir[0] == '/')
                cur[cn++] = '/';
            char *save = NULL;
            char *dup = strdup(dir + (dir[0] == '/' ? 1 : 0));
            char *tok = strtok_r(dup, "/", &save);
            while (tok) {
                size_t l = strlen(tok);
                if (cn + l + 1 < sizeof cur) {
                    memcpy(cur + cn, tok, l);
                    cn += l;
                    cur[cn] = 0;
                    mkdir(cur, 0755);
                    cur[cn++] = '/';
                    cur[cn] = 0;
                }
                tok = strtok_r(NULL, "/", &save);
            }
            free(dup);
            FILE *o = fopen(path, "w");
            if (o) {
                fwrite(JF_DEFAULT_JSONC_CONFIG, 1, strlen(JF_DEFAULT_JSONC_CONFIG), o);
                fclose(o);
                ret = strdup(path);
            }
        }
        free(content);
        app_free_strs(dirs, n);
        return ret;
    }
    {
        char dir[1280];
        snprintf(dir, sizeof dir, "%s/jefetch", dirs[0]);
        char cur[1280];
        size_t cn = 0;
        if (dir[0] == '/')
            cur[cn++] = '/';
        char *save = NULL;
        char *dup = strdup(dir + (dir[0] == '/' ? 1 : 0));
        char *tok = strtok_r(dup, "/", &save);
        while (tok) {
            size_t l = strlen(tok);
            if (cn + l + 1 < sizeof cur) {
                memcpy(cur + cn, tok, l);
                cn += l;
                cur[cn] = 0;
                mkdir(cur, 0755);
                cur[cn++] = '/';
                cur[cn] = 0;
            }
            tok = strtok_r(NULL, "/", &save);
        }
        free(dup);
        FILE *o = fopen(path, "w");
        if (o) {
            fwrite(JF_DEFAULT_JSONC_CONFIG, 1, strlen(JF_DEFAULT_JSONC_CONFIG), o);
            fclose(o);
            ret = strdup(path);
        }
    }
    app_free_strs(dirs, n);
    return ret;
}

int app_stdout_is_tty(void) {
    return isatty(STDOUT_FILENO) == 1;
}

KeyAction app_classify_key(unsigned char b) {
    if (b == 'q' || b == 'Q' || b == 0x03 || b == 0x1b)
        return KEY_QUIT;
    if (b == 't' || b == 'T')
        return KEY_TOGGLE;
    return KEY_IGNORE;
}

static void expand_tilde(const char *path, char *out, size_t n) {
    if (path[0] == '~' && path[1] == '/') {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(out, n, "%s/%s", home, path + 2);
            return;
        }
    }
    snprintf(out, n, "%s", path);
}

static int sgr_has_bg(const char *slot) {
    char *dup = strdup(slot);
    char *save = NULL;
    char *tok = strtok_r(dup, ";", &save);
    while (tok) {
        char *end;
        long v = strtol(tok, &end, 10);
        if (end != tok && !*end) {
            if (v == 48 || (v >= 40 && v <= 47) || (v >= 100 && v <= 107)) {
                free(dup);
                return 1;
            }
        }
        tok = strtok_r(NULL, ";", &save);
    }
    free(dup);
    return 0;
}

static int color_payload(const char *name, char *out, size_t n) {
    char *sgr = jf_named_color_sgr(name);
    if (!sgr)
        return 0;
    const char *t = sgr;
    if (!strncmp(t, "\x1b[", 2))
        t += 2;
    size_t l = strlen(t);
    if (l > 0 && t[l - 1] == 'm')
        l--;
    if (l == 0) {
        free(sgr);
        return 0;
    }
    if (l >= n)
        l = n - 1;
    memcpy(out, t, l);
    out[l] = 0;
    free(sgr);
    return 1;
}

static ResolvedLogo *builtin_logo_v(const char *name, const LogoConfig *lc) {
    const JfLogo *logo = logo_by_name(name);
    if (!logo)
        return NULL;
    size_t nslots = logo->nslots > 0 ? logo->nslots : 1;
    char **slots = malloc(nslots * sizeof(char *));
    if (logo->nslots == 0) {
        slots[0] = strdup(logo->color);
    } else {
        for (size_t i = 0; i < logo->nslots; i++)
            slots[i] = strdup(logo->slots[i]);
    }
    if (lc->color) {
        char payload[64];
        if (color_payload(lc->color, payload, sizeof payload)) {
            for (size_t i = 0; i < nslots; i++) {
                free(slots[i]);
                slots[i] = strdup(payload);
            }
        }
    }
    size_t pad_top = lc->has_pad_top ? lc->padding_top : 0;
    size_t pad_left = lc->has_pad_left ? lc->padding_left : 0;
    size_t pad_right = lc->has_pad_right ? lc->padding_right : 4;
    int keep_trailing = 0;
    {
        char tmp[64];
        snprintf(tmp, sizeof tmp, "%s", logo->color);
        if (sgr_has_bg(tmp))
            keep_trailing = 1;
        for (size_t i = 0; !keep_trailing && i < logo->nslots; i++) {
            if (sgr_has_bg(logo->slots[i]))
                keep_trailing = 1;
        }
    }
    char carry[64];
    snprintf(carry, sizeof carry, "\x1b[%sm", slots[0]);
    char **lines = NULL;
    size_t nlines = 0;
    size_t art_width = 0;
    for (size_t li = 0; li < logo->nlines; li++) {
        const char *rawin = logo->lines[li];
        char trimmed[2048];
        if (keep_trailing) {
            snprintf(trimmed, sizeof trimmed, "%s", rawin);
        } else {
            size_t l = strlen(rawin);
            while (l > 0 && (rawin[l - 1] == ' ' || rawin[l - 1] == '\t'))
                l--;
            if (l >= sizeof trimmed)
                l = sizeof trimmed - 1;
            memcpy(trimmed, rawin, l);
            trimmed[l] = 0;
        }
        JfBuf out;
        memset(&out, 0, sizeof out);
        jf_buf_put(&out, "\x1b[1m");
        jf_buf_put(&out, carry);
        for (size_t i = 0; i < pad_left; i++)
            jf_buf_putc(&out, ' ');
        const char *p = trimmed;
        while (*p) {
            if (*p == '$') {
                if (p[1] >= '0' && p[1] <= '9') {
                    unsigned nslot = (unsigned)(p[1] - '0');
                    if (nslot >= 1 && nslot <= nslots) {
                        snprintf(carry, sizeof carry, "\x1b[%sm", slots[nslot - 1]);
                        jf_buf_put(&out, carry);
                        p += 2;
                        continue;
                    }
                } else if (p[1] == '$') {
                    jf_buf_putc(&out, '$');
                    p += 2;
                    continue;
                }
                jf_buf_putc(&out, '$');
                p++;
            } else {
                size_t k = 1;
                unsigned char c = (unsigned char)*p;
                if (c >= 0x80) {
                    if ((c & 0xE0) == 0xC0)
                        k = 2;
                    else if ((c & 0xF0) == 0xE0)
                        k = 3;
                    else
                        k = 4;
                }
                jf_buf_putn(&out, p, k);
                p += k;
            }
        }
        jf_buf_put(&out, JF_RESET);
        size_t vis = jf_visible_len(out.data ? out.data : "");
        if (vis > art_width)
            art_width = vis;
        lines = realloc(lines, (nlines + 1) * sizeof(char *));
        lines[nlines++] = out.data ? out.data : strdup("");
    }
    for (size_t i = 0; i < nslots; i++)
        free(slots[i]);
    free(slots);
    for (size_t i = 0; i < pad_top; i++) {
        lines = realloc(lines, (nlines + 1) * sizeof(char *));
        memmove(lines + 1, lines, nlines * sizeof(char *));
        lines[0] = strdup("");
        nlines++;
    }
    ResolvedLogo *r = calloc(1, sizeof *r);
    r->lines = lines;
    r->nlines = nlines;
    r->colors = calloc(nlines ? nlines : 1, sizeof(char *));
    for (size_t i = 0; i < nlines; i++)
        r->colors[i] = strdup("");
    r->ncolors = nlines;
    r->width = art_width;
    r->padding_right = pad_right;
    return r;
}

static void apply_line_spec(char **colors, size_t n, const char *spec, const char *ansi) {
    char *dash = strchr(spec, '-');
    if (dash) {
        char a[32], b[32];
        size_t al = (size_t)(dash - spec);
        if (al >= sizeof a)
            return;
        memcpy(a, spec, al);
        a[al] = 0;
        snprintf(b, sizeof b, "%s", dash + 1);
        char *e1, *e2;
        unsigned long av = strtoul(a, &e1, 10);
        unsigned long bv = strtoul(b, &e2, 10);
        if (e1 != a && e2 != b) {
            size_t from = av > 0 ? av - 1 : 0;
            size_t to = bv > 0 ? bv - 1 : 0;
            for (size_t i = from; i <= to && i < n; i++) {
                free(colors[i]);
                colors[i] = strdup(ansi);
            }
            return;
        }
    }
    {
        char *e;
        unsigned long v = strtoul(spec, &e, 10);
        if (e != spec) {
            size_t idx = v == 0 ? 0 : v - 1;
            if (idx < n) {
                free(colors[idx]);
                colors[idx] = strdup(ansi);
            }
        }
    }
}

static ResolvedLogo *logo_from_lines(const char *text, const LogoConfig *lc) {
    size_t cap = 16, n = 0;
    char **lines = malloc(cap * sizeof(char *));
    const char *p = text;
    while (1) {
        const char *e = strchr(p, '\n');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        while (l > 0 && p[l - 1] == '\r')
            l--;
        if (n >= cap) {
            cap *= 2;
            lines = realloc(lines, cap * sizeof(char *));
        }
        lines[n] = malloc(l + 1);
        memcpy(lines[n], p, l);
        lines[n][l] = 0;
        n++;
        if (!e)
            break;
        p = e + 1;
    }
    if (n == 0) {
        lines[n++] = strdup("");
    }
    char **colors = calloc(n ? n : 1, sizeof(char *));
    for (size_t i = 0; i < n; i++)
        colors[i] = strdup("");
    for (size_t i = 0; i < lc->ncolor_map; i++) {
        char *ansi = jf_named_color_sgr(lc->color_map[i].color);
        if (!ansi)
            ansi = strdup("");
        apply_line_spec(colors, n, lc->color_map[i].line, ansi);
        free(ansi);
    }
    if (lc->color) {
        char *ansi = jf_named_color_sgr(lc->color);
        if (ansi) {
            for (size_t i = 0; i < n; i++) {
                free(colors[i]);
                colors[i] = strdup(ansi);
            }
            free(ansi);
        }
    }
    size_t pad_top = lc->has_pad_top ? lc->padding_top : 0;
    for (size_t i = 0; i < pad_top; i++) {
        lines = realloc(lines, (n + 1) * sizeof(char *));
        memmove(lines + 1, lines, n * sizeof(char *));
        lines[0] = strdup("");
        colors = realloc(colors, (n + 1) * sizeof(char *));
        memmove(colors + 1, colors, n * sizeof(char *));
        colors[0] = strdup("");
        n++;
    }
    size_t pad_left = lc->has_pad_left ? lc->padding_left : 0;
    if (pad_left > 0) {
        for (size_t i = 0; i < n; i++) {
            size_t l = strlen(lines[i]);
            char *nl = malloc(l + pad_left + 1);
            memset(nl, ' ', pad_left);
            memcpy(nl + pad_left, lines[i], l + 1);
            free(lines[i]);
            lines[i] = nl;
        }
    }
    size_t art_width = 0;
    for (size_t i = 0; i < n; i++) {
        size_t v = jf_visible_len(lines[i]);
        if (v > art_width)
            art_width = v;
    }
    ResolvedLogo *r = calloc(1, sizeof *r);
    r->lines = lines;
    r->nlines = n;
    r->colors = colors;
    r->ncolors = n;
    r->width = art_width;
    r->padding_right = lc->has_pad_right ? lc->padding_right : 0;
    return r;
}

static int image_logo_requested(const LogoConfig *lc) {
    if (lc->logo_type) {
        char l[32];
        size_t i = 0;
        while (lc->logo_type[i] && i + 1 < sizeof l) {
            char c = lc->logo_type[i];
            l[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        l[i] = 0;
        if (!strcmp(l, "image"))
            return 1;
        if (!strcmp(l, "builtin") || !strcmp(l, "none"))
            return 0;
    }
    if (lc->source && lc->source[0] && !strchr(lc->source, '\n')) {
        char expanded[2048];
        expand_tilde(lc->source, expanded, sizeof expanded);
        struct stat st;
        if (stat(expanded, &st) == 0 && S_ISREG(st.st_mode))
            return logo_looks_like_image(expanded);
    }
    return 0;
}

static ResolvedLogo *image_logo_from_config(const LogoConfig *lc, char *err, size_t errn) {
    if (!lc->source || !lc->source[0]) {
        snprintf(err, errn, "image logo needs a \"source\" path");
        return NULL;
    }
    RawImage raw = {0};
    char e2[256];
    if (!logo_image_load(lc->source, &raw, e2, sizeof e2)) {
        snprintf(err, errn, "%s", e2);
        return NULL;
    }
    LogoImage *img = logo_image_from_raw(&raw, lc->has_width, lc->has_width ? lc->width : 0,
                                         lc->has_height, lc->has_height ? lc->height : 0);
    raw_image_free(&raw);
    if (!img) {
        snprintf(err, errn, "image logo needs a \"source\" path");
        return NULL;
    }
    ResolvedLogo *logo = logo_image_to_resolved(img, lc->has_pad_right ? lc->padding_right : 2);
    logo_image_free(img);
    if (lc->has_pad_top) {
        for (size_t i = 0; i < lc->padding_top; i++) {
            logo->lines = realloc(logo->lines, (logo->nlines + 1) * sizeof(char *));
            memmove(logo->lines + 1, logo->lines, logo->nlines * sizeof(char *));
            logo->lines[0] = strdup("");
            logo->colors = realloc(logo->colors, (logo->ncolors + 1) * sizeof(char *));
            memmove(logo->colors + 1, logo->colors, logo->ncolors * sizeof(char *));
            logo->colors[0] = strdup("");
            logo->nlines++;
            logo->ncolors++;
        }
    }
    if (lc->has_pad_left && lc->padding_left > 0) {
        for (size_t i = 0; i < logo->nlines; i++) {
            if (logo->lines[i][0]) {
                size_t l = strlen(logo->lines[i]);
                char *nl = malloc(l + lc->padding_left + 1);
                memset(nl, ' ', lc->padding_left);
                memcpy(nl + lc->padding_left, logo->lines[i], l + 1);
                free(logo->lines[i]);
                logo->lines[i] = nl;
            }
        }
        logo->width += lc->padding_left;
    }
    return logo;
}

static ResolvedLogo *resolve_logo(const JfConfig *cfg) {
    if (cfg->logo.logo_type) {
        char l[32];
        size_t i = 0;
        while (cfg->logo.logo_type[i] && i + 1 < sizeof l) {
            char c = cfg->logo.logo_type[i];
            l[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        l[i] = 0;
        if (!strcmp(l, "builtin")) {
            char id[256] = "";
            if (cfg->logo.source && cfg->logo.source[0]) {
                size_t k = 0;
                while (cfg->logo.source[k] && k + 1 < sizeof id) {
                    char c = cfg->logo.source[k];
                    id[k++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
                }
                id[k] = 0;
            } else {
                OsInfo oi;
                detect_os(&oi);
                size_t k = 0;
                while (oi.id[k] && k + 1 < sizeof id) {
                    char c = oi.id[k];
                    id[k++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
                }
                id[k] = 0;
            }
            return builtin_logo_v(id[0] ? id : "linux", &cfg->logo);
        }
        if (!strcmp(l, "none"))
            return NULL;
    }
    if (image_logo_requested(&cfg->logo)) {
        char err[256];
        ResolvedLogo *logo = image_logo_from_config(&cfg->logo, err, sizeof err);
        if (logo)
            return logo;
        fprintf(stderr, "jefetch: %s\n", err);
    }
    if (cfg->logo.source) {
        char expanded[2048];
        expand_tilde(cfg->logo.source, expanded, sizeof expanded);
        char *text = NULL;
        FILE *f = fopen(expanded, "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (sz >= 0 && sz < 1000000) {
                text = malloc((size_t)sz + 1);
                if (fread(text, 1, (size_t)sz, f) != (size_t)sz) {
                    free(text);
                    text = NULL;
                } else {
                    text[sz] = 0;
                }
            }
            fclose(f);
        }
        if (text) {
            ResolvedLogo *r = logo_from_lines(text, &cfg->logo);
            free(text);
            return r;
        }
    }
    if (cfg->logo.source && strchr(cfg->logo.source, '\n'))
        return logo_from_lines(cfg->logo.source, &cfg->logo);
    char id[256] = "";
    OsInfo oi;
    detect_os(&oi);
    size_t k = 0;
    while (oi.id[k] && k + 1 < sizeof id) {
        char c = oi.id[k];
        id[k++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    id[k] = 0;
    char name[256] = "";
    if (cfg->logo.source && cfg->logo.source[0]) {
        k = 0;
        while (cfg->logo.source[k] && k + 1 < sizeof name) {
            char c = cfg->logo.source[k];
            name[k++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        name[k] = 0;
    } else if (cfg->logo.logo_type && strcmp(cfg->logo.logo_type, "auto") &&
               strcasecmp(cfg->logo.logo_type, "auto") != 0) {
        k = 0;
        while (cfg->logo.logo_type[k] && k + 1 < sizeof name) {
            char c = cfg->logo.logo_type[k];
            name[k++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        name[k] = 0;
    } else {
        snprintf(name, sizeof name, "%s", id);
    }
    return builtin_logo_v(name[0] ? name : "linux", &cfg->logo);
}

static void colorize_logo_str(const char *line, const char *color_name, char *out, size_t n) {
    while (*color_name == ' ' || *color_name == '\t')
        color_name++;
    if (!*color_name) {
        snprintf(out, n, "%s", line);
        return;
    }
    if (color_name[0] == 0x1b) {
        snprintf(out, n, "%s%s%s", color_name, line, JF_RESET);
        return;
    }
    ApplyResult ar = jf_color_code_to_ansi(color_name);
    if (ar.is_ansi)
        snprintf(out, n, "%s%s%s", ar.start, line, ar.end);
    else
        snprintf(out, n, "%s", line);
    apply_result_free(&ar);
}

static const char *DEFAULT_STRUCTURE[] = {
    "title", "separator", "os", "host", "kernel", "uptime", "packages", "shell",
    "display", "de", "wm", "theme", "icons", "font", "cursor", "terminal",
    "terminalfont", "cpu", "gpu", "memory", "swap", "disk", "localip", "battery",
    "locale", "break", "colors"
};

static void anim_configs(const App *app, AnimConfig *base, AnimConfig *active,
                         SharkvisMode *mode) {
    anim_config_default(base);
    if (app->config.logo.animation)
        anim_config_from_str(base, app->config.logo.animation);
    anim_apply_style_chars(base, app->config.logo.style, app->config.logo.chars);
    if (!base->speed_set)
        base->speed = 0.0f;
    anim_config_default(active);
    if (app->config.logo.sharkvis)
        anim_config_from_str(active, app->config.logo.sharkvis);
    if (app->config.logo.sharkvis) {
        const char *s = app->config.logo.sharkvis;
        while (*s == ' ' || *s == '\t')
            s++;
        char first[128];
        size_t i = 0;
        while (s[i] && s[i] != ' ' && s[i] != '\t' && i + 1 < sizeof first) {
            first[i] = s[i];
            i++;
        }
        first[i] = 0;
        if (first[0]) {
            SharkvisMode m = sv_mode_parse(first);
            char l[128];
            size_t k = 0;
            while (first[k] && k + 1 < sizeof l) {
                char c = first[k];
                l[k++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
            }
            l[k] = 0;
            if (!strcmp(l, "on") || !strcmp(l, "true") || !strcmp(l, "1") ||
                !strcmp(l, "yes") || !strcmp(l, "enable") || !strcmp(l, "enabled") ||
                !strcmp(l, "off") || !strcmp(l, "false") || !strcmp(l, "0") ||
                !strcmp(l, "no") || !strcmp(l, "disable") || !strcmp(l, "disabled") ||
                !strcmp(l, "auto")) {
                active->sharkvis = m;
                active->sharkvis_set = 1;
            }
        }
        if (!active->sharkvis_set) {
            const char *t = app->config.logo.sharkvis;
            while (*t == ' ' || *t == '\t')
                t++;
            if (*t) {
                char l2[128];
                size_t k = 0;
                while (t[k] && k + 1 < sizeof l2) {
                    char c = t[k];
                    l2[k++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
                }
                l2[k] = 0;
                if (strcmp(l2, "on") && strcmp(l2, "true") && strcmp(l2, "1") &&
                    strcmp(l2, "yes") && strcmp(l2, "enable") && strcmp(l2, "enabled") &&
                    strcmp(l2, "off") && strcmp(l2, "false") && strcmp(l2, "0") &&
                    strcmp(l2, "no") && strcmp(l2, "disable") && strcmp(l2, "disabled") &&
                    strcmp(l2, "auto")) {
                    active->sharkvis = SVM_AUTO;
                    active->sharkvis_set = 1;
                }
            }
        }
    }
    anim_apply_style_chars(active, app->config.logo.style, app->config.logo.chars);
    if (!active->chars_set && base->original_glyphs) {
        active->original_glyphs = 1;
        active->shading_explicit = 1;
        active->chars_set = 1;
    }
    /* Unset speed stays 0 in sharkvis mode (Rust behavior): with no
     * audio playing the logo holds still; motion comes from audio
     * (yaw/pitch/roll, beat dip, boom) or an explicit profile speed. */
    if (!active->speed_set)
        active->speed = 0.0f;
    *mode = active->sharkvis_set ? active->sharkvis : base->sharkvis;
}

static int profile_text_live(const LogoConfig *lc) {
    AnimConfig base;
    anim_config_default(&base);
    if (lc->animation)
        anim_config_from_str(&base, lc->animation);
    int r = base.text_live_colors;
    anim_config_free(&base);
    if (r)
        return 1;
    AnimConfig act;
    anim_config_default(&act);
    if (lc->sharkvis)
        anim_config_from_str(&act, lc->sharkvis);
    r = act.text_live_colors;
    anim_config_free(&act);
    return r;
}

static int display_wants_sharkvis(const JfConfig *cfg) {
    (void)cfg;
    return profile_text_live(&cfg->logo);
}

static int should_animate(App *app) {
    if (app->options.force_static)
        return 0;
    const char *anim = app->config.logo.animation ? app->config.logo.animation : "";
    char a[2048];
    size_t i = 0;
    while (anim[i] && i + 1 < sizeof a) {
        char c = anim[i];
        a[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    a[i] = 0;
    if (!strcmp(a, "off") || !strcmp(a, "none") || !strcmp(a, "static") ||
        !strcmp(a, "false") || !strcmp(a, "0"))
        return 0;
    AnimConfig base, active;
    SharkvisMode mode;
    anim_configs(app, &base, &active, &mode);
    char *t = a;
    while (*t == ' ' || *t == '\t')
        t++;
    int base_on = base.speed_set &&
                  (strstr(a, "spin") || strstr(a, "areo") || strstr(a, "rotate") ||
                   !strcmp(a, "on") || !strcmp(a, "true") || !strcmp(a, "1") || *t);
    int r = base_on || (active.speed_set && mode != SVM_OFF);
    anim_config_free(&base);
    anim_config_free(&active);
    return r;
}

typedef struct {
    int is_object;
    char *name;
    ModuleArgs args;
    JsonValue *raw;
} BuildEntry;

static void build_entry_from(BuildEntry *dst, const char *name, const ModuleArgs *args,
                             JsonValue *raw, int is_object) {
    memset(dst, 0, sizeof *dst);
    dst->is_object = is_object;
    dst->name = strdup(name);
    if (args) {
        if (args->key)
            dst->args.key = strdup(args->key);
        if (args->key_color)
            dst->args.key_color = strdup(args->key_color);
        if (args->format)
            dst->args.format = strdup(args->format);
        if (args->prefix)
            dst->args.prefix = strdup(args->prefix);
        dst->args.hide_if_empty = args->hide_if_empty;
        dst->args.hide_if_not_supported = args->hide_if_not_supported;
        if (args->output_color)
            dst->args.output_color = strdup(args->output_color);
        dst->args.title = args->title;
        if (args->type)
            dst->args.type = strdup(args->type);
        dst->args.has_fmt = args->has_fmt;
    }
    dst->raw = raw ? json_clone(raw) : NULL;
}

static BuildEntry *build_entries(const App *app, size_t *n) {
    BuildEntry *out = NULL;
    size_t m = 0;
    *n = 0;
    if (app->options.structure) {
        char *dup = strdup(app->options.structure);
        char *save = NULL;
        char *tok = strtok_r(dup, ":", &save);
        while (tok) {
            while (*tok == ' ' || *tok == '\t')
                tok++;
            char *e = tok + strlen(tok);
            while (e > tok && (e[-1] == ' ' || e[-1] == '\t'))
                *--e = 0;
            if (*tok) {
                const ModuleEntry *found = NULL;
                for (size_t k = 0; k < app->config.nmodules; k++) {
                    const char *mn = module_entry_name(&app->config.modules[k]);
                    const char *a = tok, *b = mn;
                    int eq = 1;
                    while (*a && *b) {
                        char ca = *a, cb = *b;
                        if (ca >= 'A' && ca <= 'Z')
                            ca += 32;
                        if (cb >= 'A' && cb <= 'Z')
                            cb += 32;
                        if (ca != cb) {
                            eq = 0;
                            break;
                        }
                        a++;
                        b++;
                    }
                    if (eq && !*a && !*b) {
                        found = &app->config.modules[k];
                        break;
                    }
                }
                out = realloc(out, (m + 1) * sizeof(BuildEntry));
                if (found)
                    build_entry_from(&out[m], module_entry_name(found), &found->args,
                                     found->raw, found->is_object);
                else
                    build_entry_from(&out[m], tok, NULL, NULL, 0);
                m++;
            }
            tok = strtok_r(NULL, ":", &save);
        }
        free(dup);
    } else if (app->config.nmodules > 0) {
        for (size_t k = 0; k < app->config.nmodules; k++) {
            const ModuleEntry *found = &app->config.modules[k];
            out = realloc(out, (m + 1) * sizeof(BuildEntry));
            build_entry_from(&out[m], module_entry_name(found), &found->args, found->raw,
                             found->is_object);
            m++;
        }
    } else {
        for (size_t k = 0;
             k < sizeof(DEFAULT_STRUCTURE) / sizeof(DEFAULT_STRUCTURE[0]); k++) {
            out = realloc(out, (m + 1) * sizeof(BuildEntry));
            build_entry_from(&out[m], DEFAULT_STRUCTURE[k], NULL, NULL, 0);
            m++;
        }
    }
    *n = m;
    return out;
}

static void free_entries(BuildEntry *e, size_t n) {
    for (size_t i = 0; i < n; i++) {
        free(e[i].name);
        free(e[i].args.key);
        free(e[i].args.key_color);
        free(e[i].args.format);
        free(e[i].args.prefix);
        free(e[i].args.output_color);
        free(e[i].args.type);
        json_free(e[i].raw);
    }
    free(e);
}

static void apply_logo_overrides(App *app) {
    if (app->options.logo_name) {
        char expanded[2048];
        expand_tilde(app->options.logo_name, expanded, sizeof expanded);
        struct stat st;
        if (stat(expanded, &st) == 0 && S_ISREG(st.st_mode)) {
            free(app->config.logo.source);
            app->config.logo.source = strdup(app->options.logo_name);
            free(app->config.logo.logo_type);
            if (logo_looks_like_image(expanded))
                app->config.logo.logo_type = strdup("image");
            else
                app->config.logo.logo_type = strdup("file");
        } else {
            free(app->config.logo.source);
            app->config.logo.source = strdup(app->options.logo_name);
            free(app->config.logo.logo_type);
            app->config.logo.logo_type = strdup("builtin");
        }
    }
    if (!app->config.logo.color && app->config.logo.animation) {
        char c[256];
        if (anim_animation_color(app->config.logo.animation, c, sizeof c)) {
            char *t = c;
            while (*t == ' ' || *t == '\t')
                t++;
            if (*t)
                app->config.logo.color = strdup(t);
        }
    }
}

static void apply_logo_colors(App *app) {
    OsInfo oi;
    detect_os(&oi);
    char id[256];
    size_t k = 0;
    while (oi.id[k] && k + 1 < sizeof id) {
        char c = oi.id[k];
        id[k++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    id[k] = 0;
    const JfLogo *logo = logo_by_name(id);
    if (!logo)
        return;
    if (!app->config.display.title_color) {
        const char *sgr = logo->color_title ? logo->color_title
                                            : (logo->nslots > 0 ? logo->slots[0] : "34");
        char tmp[64];
        snprintf(tmp, sizeof tmp, "bold_%s", sgr);
        app->config.display.title_color = strdup(tmp);
    }
    if (!app->config.display.key_color) {
        const char *sgr = logo->color_keys ? logo->color_keys
                                           : (logo->nslots > 1 ? logo->slots[1] : "36");
        char tmp[64];
        snprintf(tmp, sizeof tmp, "bold_%s", sgr);
        app->config.display.key_color = strdup(tmp);
    }
}

static void pick_logo(App *app) {
    anim_resolved_free(app->logo);
    app->logo = resolve_logo(&app->config);
    if (!app->logo) {
        OsInfo oi;
        detect_os(&oi);
        char id[256];
        size_t k = 0;
        while (oi.id[k] && k + 1 < sizeof id) {
            char c = oi.id[k];
            id[k++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        id[k] = 0;
        app->logo = builtin_logo_v(id[0] ? id : "linux", &app->config.logo);
        if (!app->logo)
            app->logo = builtin_logo_v("linux", &app->config.logo);
        if (!app->logo)
            app->logo = builtin_logo_v("unknown", &app->config.logo);
    }
    apply_logo_colors(app);
}

static void separator_colored(const char *sep, const JfConfig *cfg, char *out, size_t n) {
    (void)sep;
    const char *s = cfg->display.separator ? cfg->display.separator : ": ";
    if (cfg->display.separator_color) {
        ApplyResult ar = jf_color_code_to_ansi(cfg->display.separator_color);
        if (ar.is_ansi) {
            snprintf(out, n, "%s%s%s%s", ar.start,
                     jf_live_text_suffix(cfg->display.text_live), s, ar.end);
            apply_result_free(&ar);
            return;
        }
        apply_result_free(&ar);
    }
    snprintf(out, n, "%s", s);
}

static void instance_for(const BuildEntry *e, ModuleInstance *inst) {
    memset(inst, 0, sizeof *inst);
    inst->module = strdup(e->name);
    ModuleArgs *dst = &inst->args;
    if (e->args.key)
        dst->key = strdup(e->args.key);
    if (e->args.key_color)
        dst->key_color = strdup(e->args.key_color);
    if (e->args.format)
        dst->format = strdup(e->args.format);
    if (e->args.prefix)
        dst->prefix = strdup(e->args.prefix);
    dst->hide_if_empty = e->args.hide_if_empty;
    dst->hide_if_not_supported = e->args.hide_if_not_supported;
    if (e->args.output_color)
        dst->output_color = strdup(e->args.output_color);
    dst->title = e->args.title;
    if (e->args.type)
        dst->type = strdup(e->args.type);
    dst->has_fmt = e->args.has_fmt;
    inst->raw = json_clone(e->raw);
}

static int entry_disabled(const App *app, const char *name) {
    if (!name)
        return 1;
    for (size_t i = 0; i < app->options.ndisabled; i++) {
        const char *a = app->options.structure_disabled[i];
        const char *b = name;
        if (!a || !b)
            continue;
        int eq = 1;
        while (*a && *b) {
            char ca = *a, cb = *b;
            if (ca >= 'A' && ca <= 'Z')
                ca += 32;
            if (cb >= 'A' && cb <= 'Z')
                cb += 32;
            if (ca != cb) {
                eq = 0;
                break;
            }
            a++;
            b++;
        }
        if (eq && !*a && !*b)
            return 1;
    }
    return 0;
}

static void print_json(const App *app, const BuildEntry *entries, size_t n) {
    JsonValue *arr = json_new_arr();
    for (size_t i = 0; i < n; i++) {
        if (entry_disabled(app, entries[i].name))
            continue;
        ModuleInstance inst;
        instance_for(&entries[i], &inst);
        const char *type_name = module_json_type_name(entries[i].name);
        char *err = module_json_error(entries[i].name, &inst, &app->config);
        if (err) {
            JsonValue *e = json_new_obj();
            json_obj_put(e, "type", json_new_str(type_name));
            json_obj_put(e, "error", json_new_str(err));
            free(err);
            json_arr_push(arr, e);
            module_instance_free(&inst);
            continue;
        }
        JsonValue *res = module_json_result(entries[i].name, &inst, &app->config);
        module_instance_free(&inst);
        if (!res)
            continue;
        JsonValue *e = json_new_obj();
        json_obj_put(e, "type", json_new_str(type_name));
        json_obj_put(e, "result", res);
        json_arr_push(arr, e);
    }
    char *out = NULL;
    size_t len = 0;
    json_write_pretty(arr, &out, &len);
    json_free(arr);
    printf("%s", out ? out : "[]");
    free(out);
}

typedef struct {
    const App *app;
    BuildEntry *entry;
    size_t idx;
    ModuleOutput *out;
} RenderJob;

static void *render_job_fn(void *arg) {
    RenderJob *job = arg;
    if (!job->entry || !job->entry->name || entry_disabled(job->app, job->entry->name)) {
        job->out = NULL;
        return NULL;
    }
    ModuleInstance inst;
    instance_for(job->entry, &inst);
    job->out = module_run_instance(&inst, &job->app->config);
    module_instance_free(&inst);
    return NULL;
}

static char **render_modules_with(const App *app, const BuildEntry *entries, size_t n,
                                  size_t *nlines) {
    typedef struct {
        size_t idx;
        ModuleOutput *out;
    } Ordered;
    Ordered *ordered = NULL;
    size_t nordered = 0;
    *nlines = 0;
    if (n > 1) {
        RenderJob *jobs = calloc(n, sizeof(RenderJob));
        pthread_t *ths = calloc(n, sizeof(pthread_t));
        unsigned char *started = calloc(n, 1);
        if (!jobs || !ths || !started) {
            free(jobs);
            free(ths);
            free(started);
        } else {
            for (size_t i = 0; i < n; i++) {
                jobs[i].app = app;
                jobs[i].entry = (BuildEntry *)&entries[i];
                jobs[i].idx = i;
                jobs[i].out = NULL;
                if (pthread_create(&ths[i], NULL, render_job_fn, &jobs[i]) == 0)
                    started[i] = 1;
                else {
                    render_job_fn(&jobs[i]);
                }
            }
            for (size_t i = 0; i < n; i++) {
                if (started[i])
                    pthread_join(ths[i], NULL);
                Ordered *no = realloc(ordered, (nordered + 1) * sizeof(Ordered));
                if (!no)
                    break;
                ordered = no;
                ordered[nordered].idx = jobs[i].idx;
                ordered[nordered].out = jobs[i].out;
                nordered++;
            }
            free(jobs);
            free(ths);
            free(started);
            for (size_t i = 0; i < nordered; i++) {
                for (size_t j = i + 1; j < nordered; j++) {
                    if (ordered[j].idx < ordered[i].idx) {
                        Ordered t = ordered[i];
                        ordered[i] = ordered[j];
                        ordered[j] = t;
                    }
                }
            }
        }
        if (nordered == 0 && (n > 1)) {
            for (size_t i = 0; i < n; i++) {
                if (entry_disabled(app, entries[i].name)) {
                    Ordered *no = realloc(ordered, (nordered + 1) * sizeof(Ordered));
                    if (!no)
                        break;
                    ordered = no;
                    ordered[nordered].idx = i;
                    ordered[nordered].out = NULL;
                    nordered++;
                    continue;
                }
                ModuleInstance inst;
                instance_for(&entries[i], &inst);
                ModuleOutput *o = module_run_instance(&inst, &app->config);
                module_instance_free(&inst);
                Ordered *no = realloc(ordered, (nordered + 1) * sizeof(Ordered));
                if (!no) {
                    module_output_free_contents(o);
                    free(o);
                    break;
                }
                ordered = no;
                ordered[nordered].idx = i;
                ordered[nordered].out = o;
                nordered++;
            }
        }
    } else {
        for (size_t i = 0; i < n; i++) {
            if (entry_disabled(app, entries[i].name)) {
                ordered = realloc(ordered, (nordered + 1) * sizeof(Ordered));
                ordered[nordered].idx = i;
                ordered[nordered].out = NULL;
                nordered++;
                continue;
            }
            ModuleInstance inst;
            instance_for(&entries[i], &inst);
            ModuleOutput *o = module_run_instance(&inst, &app->config);
            module_instance_free(&inst);
            ordered = realloc(ordered, (nordered + 1) * sizeof(Ordered));
            ordered[nordered].idx = i;
            ordered[nordered].out = o;
            nordered++;
        }
    }
    char **lines = NULL;
    size_t nl = 0;
    for (size_t i = 0; i < nordered; i++) {
        ModuleOutput *o = ordered[i].out;
        if (!o)
            continue;
        if (o->blank) {
            lines = realloc(lines, (nl + 1) * sizeof(char *));
            lines[nl++] = strdup("");
            module_output_free_contents(o);
            free(o);
            continue;
        }
        if (!o->supported || o->nvalues == 0) {
            module_output_free_contents(o);
            free(o);
            continue;
        }
        if (o->nper > 0 && o->nper == o->nvalues) {
            char sep[128];
            separator_colored("", &app->config, sep, sizeof sep);
            for (size_t k = 0; k < o->nvalues; k++) {
                size_t kl = jf_visible_len(o->per_value_keys[k]);
                JfBuf ln;
                memset(&ln, 0, sizeof ln);
                if (kl == 0) {
                    jf_buf_put(&ln, o->values[k]);
                } else {
                    jf_buf_put(&ln, o->per_value_keys[k]);
                    jf_buf_put(&ln, sep);
                    for (size_t p = 0; p < app->config.display.padding; p++)
                        jf_buf_putc(&ln, ' ');
                    jf_buf_put(&ln, o->values[k]);
                }
                lines = realloc(lines, (nl + 1) * sizeof(char *));
                lines[nl++] = ln.data ? ln.data : strdup("");
            }
            module_output_free_contents(o);
            free(o);
            continue;
        }
        size_t key_visible = jf_visible_len(o->key);
        if (key_visible == 0) {
            for (size_t k = 0; k < o->nvalues; k++) {
                lines = realloc(lines, (nl + 1) * sizeof(char *));
                lines[nl++] = strdup(o->values[k]);
            }
            module_output_free_contents(o);
            free(o);
            continue;
        }
        char sep[128];
        separator_colored("", &app->config, sep, sizeof sep);
        size_t indent = key_visible + jf_visible_len(sep) + app->config.display.padding;
        for (size_t k = 0; k < o->nvalues; k++) {
            JfBuf ln;
            memset(&ln, 0, sizeof ln);
            if (k == 0 || o->repeat_key) {
                jf_buf_put(&ln, o->key);
                jf_buf_put(&ln, sep);
                for (size_t p = 0; p < app->config.display.padding; p++)
                    jf_buf_putc(&ln, ' ');
                jf_buf_put(&ln, o->values[k]);
            } else {
                for (size_t p = 0; p < indent; p++)
                    jf_buf_putc(&ln, ' ');
                jf_buf_put(&ln, o->values[k]);
            }
            lines = realloc(lines, (nl + 1) * sizeof(char *));
            lines[nl++] = ln.data ? ln.data : strdup("");
        }
        module_output_free_contents(o);
        free(o);
    }
    free(ordered);
    *nlines = nl;
    return lines;
}

static void free_lines(char **l, size_t n) {
    for (size_t i = 0; i < n; i++)
        free(l[i]);
    free(l);
}

static int config_stamp(const char *path, uint64_t *mtime_ms, size_t *size) {
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    *mtime_ms = (uint64_t)st.st_mtime * 1000 + (uint64_t)st.st_mtim.tv_nsec / 1000000;
    *size = (size_t)st.st_size;
    return 1;
}

static struct termios live_saved_term;
static int live_have_term = 0;

static void live_signal_restore(int sig) {
    const char seq[] = "\x1b[?25h\x1b[0m\n";
    write(STDOUT_FILENO, seq, sizeof seq - 1);
    signal(sig, SIG_DFL);
    raise(sig);
}

static void install_live_signal_handlers(void) {
    int sigs[] = {SIGTERM, SIGINT, SIGHUP};
    for (size_t i = 0; i < 3; i++)
        signal(sigs[i], live_signal_restore);
}

static void debug_log_keys(const char *src, const uint8_t *buf, size_t n) {
    const char *p = getenv("JEFETCH_DEBUG_KEYS");
    if (!p)
        return;
    FILE *f = fopen(p, "a");
    if (!f)
        return;
    fprintf(f, "%s: ", src);
    for (size_t i = 0; i < n; i++)
        fprintf(f, "%02x ", buf[i]);
    fprintf(f, "\n");
    fclose(f);
}

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} KeyQueue;

static int keyqueue_push(KeyQueue *pending, const uint8_t *buf, size_t k) {
    if (k <= 1)
        return 1;
    size_t need = pending->len + k - 1;
    if (need > pending->cap) {
        size_t ncap = pending->cap ? pending->cap * 2 : 32;
        while (ncap < need) {
            ncap *= 2;
            if (ncap > 4096) {
                return 0;
            }
        }
        uint8_t *nd = realloc(pending->data, ncap);
        if (!nd)
            return 0;
        pending->data = nd;
        pending->cap = ncap;
    }
    memcpy(pending->data + pending->len, buf + 1, k - 1);
    pending->len += k - 1;
    return 1;
}

static int poll_key_byte(int tty_fd, int is_tty, KeyQueue *pending) {
    if (pending->len > 0) {
        int b = pending->data[0];
        memmove(pending->data, pending->data + 1, pending->len - 1);
        pending->len--;
        return b;
    }
    if (is_tty && tty_fd != -1) {
        uint8_t buf[16];
        ssize_t k = read(tty_fd, buf, sizeof buf);
        if (k > 0) {
            debug_log_keys("tty", buf, (size_t)k);
            keyqueue_push(pending, buf, (size_t)k);
            return buf[0];
        }
    }
    {
        uint8_t buf[16];
        int flags = fcntl(STDIN_FILENO, F_GETFL);
        if (flags == -1)
            return -1;
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
        ssize_t k = read(STDIN_FILENO, buf, sizeof buf);
        fcntl(STDIN_FILENO, F_SETFL, flags);
        if (k > 0) {
            debug_log_keys("stdin", buf, (size_t)k);
            keyqueue_push(pending, buf, (size_t)k);
            return buf[0];
        }
    }
    return -1;
}

static KeyAction poll_key_action(int tty_fd, int is_tty, KeyQueue *pending) {
    int b = poll_key_byte(tty_fd, is_tty, pending);
    if (b == 0x1b) {
        struct timespec ts = {0, 25000000};
        nanosleep(&ts, NULL);
        uint8_t tail[16];
        size_t nt = 0;
        for (;;) {
            int c = poll_key_byte(tty_fd, is_tty, pending);
            if (c < 0)
                break;
            if (nt < sizeof tail)
                tail[nt++] = (uint8_t)c;
            if (nt >= 16)
                break;
        }
        if (nt > 0 && (tail[0] == '[' || tail[0] == 'O'))
            return KEY_IGNORE;
        return KEY_QUIT;
    }
    if (b < 0)
        return KEY_IGNORE;
    return app_classify_key((unsigned char)b);
}

static void apply_display_sharkvis_static(char **lines, size_t n) {
    for (size_t i = 0; i < n; i++) {
        Rgb live = {0, 0, 0};
        size_t need = strlen(lines[i]) + 32;
        char *tmp = malloc(need);
        if (!tmp)
            continue;
        sv_swap_placeholders(lines[i], 0, live, NULL, tmp, need);
        free(lines[i]);
        lines[i] = tmp;
    }
}

static int run_static(App *app, BuildEntry *entries, size_t n) {
    size_t nl = 0;
    char **lines = render_modules_with(app, entries, n, &nl);
    apply_display_sharkvis_static(lines, nl);
    int use_image = 0;
    if (app->logo && app->config.logo.source) {
        NativeSpec spec;
        memset(&spec, 0, sizeof spec);
        if (image_logo_requested(&app->config.logo)) {
            size_t pad_left = app->config.logo.has_pad_left ? app->config.logo.padding_left : 0;
            size_t pad_top = app->config.logo.has_pad_top ? app->config.logo.padding_top : 0;
            spec.path = app->config.logo.source;
            spec.cols = app->logo->width >= pad_left ? app->logo->width - pad_left : 0;
            spec.rows = app->logo->nlines >= pad_top ? app->logo->nlines - pad_top : 0;
            spec.gap = app->logo->padding_right;
            spec.pad_left = pad_left;
            spec.pad_top = pad_top;
            spec.text = lines;
            spec.ntext = nl;
            if (spec.cols > 0 && spec.rows > 0 && graphics_display_native(&spec)) {
                free_lines(lines, nl);
                return 0;
            }
            use_image = 1;
            (void)use_image;
        }
    }
    size_t logo_pad = app->logo ? app->logo->width : 0;
    JfBuf out;
    memset(&out, 0, sizeof out);
    if (app->logo) {
        size_t m = nl > app->logo->nlines ? nl : app->logo->nlines;
        for (size_t row = 0; row < m; row++) {
            const char *logo_line = row < app->logo->nlines ? app->logo->lines[row] : NULL;
            const char *text_line = row < nl ? lines[row] : "";
            char lcol[8192] = "";
            if (logo_line) {
                const char *cn = row < app->logo->ncolors ? app->logo->colors[row] : "";
                colorize_logo_str(logo_line, cn, lcol, sizeof lcol);
            } else {
                memset(lcol, ' ', logo_pad);
                lcol[logo_pad] = 0;
            }
            size_t vis = jf_visible_len(lcol);
            JfBuf ln;
            memset(&ln, 0, sizeof ln);
            jf_buf_put(&ln, lcol);
            for (size_t k = vis; k < logo_pad; k++)
                jf_buf_putc(&ln, ' ');
            for (size_t k = 0; k < app->logo->padding_right; k++)
                jf_buf_putc(&ln, ' ');
            jf_buf_put(&ln, text_line);
            char *s = ln.data ? ln.data : strdup("");
            char *e = s + strlen(s);
            while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
                *--e = 0;
            jf_buf_put(&out, s);
            jf_buf_putc(&out, '\n');
            free(s);
        }
    } else {
        for (size_t i = 0; i < nl; i++) {
            char *s = strdup(lines[i]);
            char *e = s + strlen(s);
            while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
                *--e = 0;
            jf_buf_put(&out, s);
            jf_buf_putc(&out, '\n');
            free(s);
        }
    }
    if (!jf_colors_enabled()) {
        char *tmp = malloc(out.len + 1);
        jf_strip_sgr(out.data ? out.data : "", tmp, out.len + 1);
        printf("%s", tmp);
        free(tmp);
    } else {
        printf("%s", out.data ? out.data : "");
    }
    fflush(stdout);
    jf_buf_free(&out);
    free_lines(lines, nl);
    return 0;
}

static ResolvedLogo *logo_clone(const ResolvedLogo *l) {
    if (!l)
        return NULL;
    ResolvedLogo *r = calloc(1, sizeof *r);
    for (size_t i = 0; i < l->nlines; i++) {
        r->lines = realloc(r->lines, (r->nlines + 1) * sizeof(char *));
        r->lines[r->nlines++] = strdup(l->lines[i]);
    }
    for (size_t i = 0; i < l->ncolors; i++) {
        r->colors = realloc(r->colors, (r->ncolors + 1) * sizeof(char *));
        r->colors[r->ncolors++] = strdup(l->colors[i]);
    }
    r->width = l->width;
    r->padding_right = l->padding_right;
    return r;
}

typedef struct {
    JfConfig cfg;
    char **disabled;
    size_t ndisabled;
    BuildEntry *entries;
    size_t nentries;
    unsigned long long gen;
    char **lines;
    size_t nlines;
    int ready;
    pthread_mutex_t mu;
} RefreshState;

static void *refresh_thread_fn(void *arg) {
    RefreshState *st = arg;
    App tmp;
    memset(&tmp, 0, sizeof tmp);
    BuildEntry *entries = NULL;
    size_t nentries = 0;
    pthread_mutex_lock(&st->mu);
    config_clone(&tmp.config, &st->cfg);
    tmp.options.structure_disabled = NULL;
    tmp.options.ndisabled = 0;
    for (size_t i = 0; i < st->ndisabled; i++) {
        char *d = st->disabled[i] ? strdup(st->disabled[i]) : NULL;
        if (!d)
            continue;
        char **nd = realloc(tmp.options.structure_disabled,
                            (tmp.options.ndisabled + 1) * sizeof(char *));
        if (!nd) {
            free(d);
            continue;
        }
        tmp.options.structure_disabled = nd;
        tmp.options.structure_disabled[tmp.options.ndisabled++] = d;
    }
    nentries = st->nentries;
    if (nentries > 0) {
        entries = calloc(nentries, sizeof(BuildEntry));
        if (entries) {
            for (size_t i = 0; i < nentries; i++) {
                entries[i].is_object = st->entries[i].is_object;
                entries[i].name = st->entries[i].name ? strdup(st->entries[i].name) : NULL;
                if (st->entries[i].args.key)
                    entries[i].args.key = strdup(st->entries[i].args.key);
                if (st->entries[i].args.key_color)
                    entries[i].args.key_color = strdup(st->entries[i].args.key_color);
                if (st->entries[i].args.format)
                    entries[i].args.format = strdup(st->entries[i].args.format);
                if (st->entries[i].args.prefix)
                    entries[i].args.prefix = strdup(st->entries[i].args.prefix);
                entries[i].args.hide_if_empty = st->entries[i].args.hide_if_empty;
                entries[i].args.hide_if_not_supported =
                    st->entries[i].args.hide_if_not_supported;
                if (st->entries[i].args.output_color)
                    entries[i].args.output_color = strdup(st->entries[i].args.output_color);
                entries[i].args.title = st->entries[i].args.title;
                if (st->entries[i].args.type)
                    entries[i].args.type = strdup(st->entries[i].args.type);
                entries[i].args.has_fmt = st->entries[i].args.has_fmt;
                entries[i].raw = json_clone(st->entries[i].raw);
                if (!entries[i].name) {
                    json_free(entries[i].raw);
                    entries[i].raw = NULL;
                }
            }
        } else {
            nentries = 0;
        }
    }
    unsigned long long gen = st->gen;
    pthread_mutex_unlock(&st->mu);
    size_t nl = 0;
    char **lines = NULL;
    if (entries || nentries == 0)
        lines = render_modules_with(&tmp, entries ? entries : st->entries,
                                    entries ? nentries : 0, &nl);
    config_free(&tmp.config);
    for (size_t i = 0; i < tmp.options.ndisabled; i++)
        free(tmp.options.structure_disabled[i]);
    free(tmp.options.structure_disabled);
    if (entries) {
        for (size_t i = 0; i < nentries; i++) {
            free(entries[i].name);
            free(entries[i].args.key);
            free(entries[i].args.key_color);
            free(entries[i].args.format);
            free(entries[i].args.prefix);
            free(entries[i].args.output_color);
            free(entries[i].args.type);
            json_free(entries[i].raw);
        }
        free(entries);
    }
    pthread_mutex_lock(&st->mu);
    if (st->ready) {
        for (size_t i = 0; i < st->nlines; i++)
            free(st->lines[i]);
        free(st->lines);
    }
    if (gen == st->gen) {
        st->lines = lines;
        st->nlines = nl;
        st->ready = 1;
    } else {
        for (size_t i = 0; i < nl; i++)
            free(lines[i]);
        free(lines);
    }
    pthread_mutex_unlock(&st->mu);
    return NULL;
}

static void draw_animated_frame(JfBuf *out, const char **anim_lines, size_t nanim,
                                char **info, size_t ninfo, size_t cols,
                                const LiveFrame *live, int display_live) {
    for (size_t row = 0; row < nanim; row++) {
        JfBuf line;
        memset(&line, 0, sizeof line);
        jf_buf_put(&line, anim_lines[row]);
        long info_row = (long)row - 1;
        if (info_row >= 0 && (size_t)info_row < ninfo) {
            jf_buf_put(&line, "  ");
            if (display_live) {
                Rgb g;
                char tmp[4096];
                const Rgb *tp = live->has_term_pal ? live->term_pal : NULL;
                if (sv_grad_for_row(live, (size_t)info_row, ninfo, &g))
                    sv_swap_placeholders(info[info_row], 1, g, tp, tmp, sizeof tmp);
                else
                    sv_swap_placeholders(info[info_row], 0, g, tp, tmp, sizeof tmp);
                jf_buf_put(&line, tmp);
            } else {
                jf_buf_put(&line, info[info_row]);
            }
        }
        char *t = line.data ? line.data : strdup("");
        if (!t) {
            jf_buf_free(&line);
            continue;
        }
        char *e = t + strlen(t);
        while (e > t && (e[-1] == ' ' || e[-1] == '\t'))
            *--e = 0;
        size_t tl = strlen(t);
        char *clipped = malloc(tl + 16);
        if (!clipped) {
            jf_buf_put(out, t);
        } else {
            jf_truncate_visible(t, cols, clipped, tl + 16);
            jf_buf_put(out, clipped);
            free(clipped);
        }
        jf_buf_put(out, "\x1b[K");
        if (row + 1 < nanim)
            jf_buf_putc(out, '\n');
        free(t);
    }
    jf_buf_put(out, "\x1b[J");
}

static void draw_static_live(JfBuf *out, const ResolvedLogo *logo, char **info, size_t ninfo,
                             size_t cols, size_t render_height,
                             const LiveFrame *live, int display_live) {
    size_t logo_h = logo ? logo->nlines : 0;
    size_t logo_w = logo ? logo->width : 0;
    size_t logo_gap = logo ? logo->padding_right : 0;
    int logo_live = display_live && live && sv_has_display_color(live);
    for (size_t row = 0; row < render_height; row++) {
        JfBuf line;
        memset(&line, 0, sizeof line);
        if (logo) {
            if (row >= 1 && row < 1 + logo_h) {
                size_t i = row - 1;
                const char *logo_line = i < logo->nlines ? logo->lines[i] : "";
                const char *cn = i < logo->ncolors ? logo->colors[i] : "";
                char lcol[8192];
                colorize_logo_str(logo_line, cn, lcol, sizeof lcol);
                if (logo_live) {
                    Rgb g;
                    int has = 0;
                    if (logo_h > 0)
                        has = sv_grad_for_row(live, row - 1, logo_h, &g);
                    if (has) {
                        char esc[32];
                        const Rgb *tp = live->has_term_pal ? live->term_pal : NULL;
                        sv_live_esc(tp, g, esc, sizeof esc);
                        char plain[8192];
                        jf_strip_sgr(lcol, plain, sizeof plain);
                        size_t vis = jf_visible_len(plain);
                        jf_buf_put(&line, esc);
                        jf_buf_put(&line, plain);
                        jf_buf_put(&line, "\x1b[0m");
                        for (size_t k = vis; k < logo_w; k++)
                            jf_buf_putc(&line, ' ');
                    } else {
                        size_t vis = jf_visible_len(lcol);
                        jf_buf_put(&line, lcol);
                        for (size_t k = vis; k < logo_w; k++)
                            jf_buf_putc(&line, ' ');
                    }
                } else {
                    size_t vis = jf_visible_len(lcol);
                    jf_buf_put(&line, lcol);
                    for (size_t k = vis; k < logo_w; k++)
                        jf_buf_putc(&line, ' ');
                }
            } else if (logo_w > 0) {
                for (size_t k = 0; k < logo_w; k++)
                    jf_buf_putc(&line, ' ');
            }
        }
        long info_row = (long)row - 1;
        if (info_row >= 0 && (size_t)info_row < ninfo) {
            if (logo)
                for (size_t k = 0; k < logo_gap; k++)
                    jf_buf_putc(&line, ' ');
            if (display_live) {
                Rgb g;
                char tmp[4096];
                const Rgb *tp = live->has_term_pal ? live->term_pal : NULL;
                if (sv_grad_for_row(live, (size_t)info_row, ninfo, &g))
                    sv_swap_placeholders(info[info_row], 1, g, tp, tmp, sizeof tmp);
                else
                    sv_swap_placeholders(info[info_row], 0, g, tp, tmp, sizeof tmp);
                jf_buf_put(&line, tmp);
            } else {
                jf_buf_put(&line, info[info_row]);
            }
        }
        char *t = line.data ? line.data : strdup("");
        if (!t) {
            jf_buf_free(&line);
            continue;
        }
        char *e = t + strlen(t);
        while (e > t && (e[-1] == ' ' || e[-1] == '\t'))
            *--e = 0;
        size_t tl = strlen(t);
        char *clipped = malloc(tl + 16);
        if (!clipped) {
            jf_buf_put(out, t);
        } else {
            jf_truncate_visible(t, cols, clipped, tl + 16);
            jf_buf_put(out, clipped);
            free(clipped);
        }
        jf_buf_put(out, "\x1b[K");
        if (row + 1 < render_height)
            jf_buf_putc(out, '\n');
        free(t);
    }
    jf_buf_put(out, "\x1b[J");
}

static double jf_wrap_angle(double phase) {
    double tau = 6.283185307179586;
    return fmod(phase, tau);
}

static int run_live(App *app, BuildEntry *entries, size_t nentries, int start_animated) {
    ResolvedLogo *base_logo = logo_clone(app->logo);
    int animated = start_animated && base_logo != NULL;
    size_t nbase = 0;
    char **base_lines = render_modules_with(app, entries, nentries, &nbase);
    AnimConfig base_cfg, active_cfg;
    SharkvisMode mode;
    anim_configs(app, &base_cfg, &active_cfg, &mode);
    int display_live = display_wants_sharkvis(&app->config);
    LogoCloud *base_cloud = base_logo ? anim_build_cloud(base_logo, &base_cfg) : NULL;
    LogoCloud *active_cloud = base_logo ? anim_build_cloud(base_logo, &active_cfg) : NULL;
    int using_active = 0;
    Sync *shark_sync = sync_new();
    LiveFrame shark_live;
    memset(&shark_live, 0, sizeof shark_live);
    uint64_t shark_polled = jf_now_ms() - 1000;
    double spin_phase = 0, yaw_phase = 0, pitch_phase = 0, roll_phase = 0;
    uint64_t last_fx = jf_now_ms(), last_sound = jf_now_ms();
    char *watch_path = NULL;
    if (!app->options.no_config) {
        if (app->config.loaded_from) {
            watch_path = strdup(app->config.loaded_from);
        } else {
            size_t nd = 0;
            char **dirs = app_config_search_dirs(&nd);
            if (nd > 0) {
                watch_path = malloc(strlen(dirs[0]) + 24);
                sprintf(watch_path, "%s/jefetch/config.jsonc", dirs[0]);
            }
            app_free_strs(dirs, nd);
        }
    }
    uint64_t last_stamp_ms = 0;
    size_t last_stamp_sz = 0;
    int have_stamp = 0;
    if (watch_path)
        have_stamp = config_stamp(watch_path, &last_stamp_ms, &last_stamp_sz);
    printf("\x1b[0m\x1b[2J\x1b[3J\x1b[H\x1b[?25l");
    fflush(stdout);
    int tty_fd = -1;
    struct termios orig_term;
    int have_term = 0;
    FILE *ttyf = fopen("/dev/tty", "r+");
    if (ttyf) {
        tty_fd = fileno(ttyf);
        if (tcgetattr(tty_fd, &orig_term) == 0) {
            struct termios raw = orig_term;
            raw.c_lflag &= (unsigned)(~(ICANON | ECHO));
            raw.c_cc[VMIN] = 0;
            raw.c_cc[VTIME] = 0;
            tcsetattr(tty_fd, TCSANOW, &raw);
            live_saved_term = orig_term;
            live_have_term = 1;
            install_live_signal_handlers();
            have_term = 1;
        } else {
            tty_fd = -1;
        }
    }
    int is_tty = have_term;
    JfBuf out;
    memset(&out, 0, sizeof out);
    KeyQueue pending = {0};
    RefreshState refresh;
    memset(&refresh, 0, sizeof refresh);
    pthread_mutex_init(&refresh.mu, NULL);
    int refresh_busy = 0;
    pthread_t refresh_thr;
    int has_refresh_thr = 0;
    unsigned long long refresh_gen = 0;
    uint64_t last_refresh = jf_now_ms();
    uint64_t last_config_check = jf_now_ms() - 1000;
    int needs_draw = 1;
    for (;;) {
        uint64_t now = jf_now_ms();
        if (now - last_config_check >= 250) {
            last_config_check = now;
            if (watch_path) {
                uint64_t sm = 0;
                size_t ss = 0;
                int hs = config_stamp(watch_path, &sm, &ss);
                if (hs != have_stamp || (hs && (sm != last_stamp_ms || ss != last_stamp_sz))) {
                    have_stamp = hs;
                    last_stamp_ms = sm;
                    last_stamp_sz = ss;
                    JfConfig nc;
                    config_default(&nc);
                    if (load_config_file_into(watch_path, &nc)) {
                        config_free(&app->config);
                        app->config = nc;
                        app->config.display.text_live =
                            profile_text_live(&app->config.logo);
                        apply_logo_overrides(app);
                        anim_resolved_free(app->logo);
                        app->logo = NULL;
                        pick_logo(app);
                        anim_resolved_free(base_logo);
                        base_logo = logo_clone(app->logo);
                        free_entries(entries, nentries);
                        entries = build_entries(app, &nentries);
                        anim_config_free(&base_cfg);
                        anim_config_free(&active_cfg);
                        anim_configs(app, &base_cfg, &active_cfg, &mode);
                        display_live = display_wants_sharkvis(&app->config);
                        anim_cloud_free(base_cloud);
                        anim_cloud_free(active_cloud);
                        base_cloud = base_logo ? anim_build_cloud(base_logo, &base_cfg) : NULL;
                        active_cloud =
                            base_logo ? anim_build_cloud(base_logo, &active_cfg) : NULL;
                        using_active = 0;
                        animated = should_animate(app) && base_logo != NULL;
                        refresh_gen++;
                        pthread_mutex_lock(&refresh.mu);
                        refresh.gen = refresh_gen;
                        refresh.ready = 0;
                        for (size_t i = 0; i < refresh.nlines; i++)
                            free(refresh.lines[i]);
                        free(refresh.lines);
                        refresh.lines = NULL;
                        refresh.nlines = 0;
                        pthread_mutex_unlock(&refresh.mu);
                        last_refresh = jf_now_ms() - 2000;
                    } else {
                        config_free(&nc);
                    }
                }
            }
        }
        if (now - last_refresh >= 1000 && !refresh_busy) {
            if (has_refresh_thr) {
                pthread_join(refresh_thr, NULL);
                has_refresh_thr = 0;
            }
            last_refresh = now;
            refresh_busy = 1;
            refresh_gen++;
            pthread_mutex_lock(&refresh.mu);
            config_free(&refresh.cfg);
            memset(&refresh.cfg, 0, sizeof refresh.cfg);
            config_clone(&refresh.cfg, &app->config);
            for (size_t i = 0; i < refresh.ndisabled; i++)
                free(refresh.disabled[i]);
            free(refresh.disabled);
            refresh.disabled = NULL;
            refresh.ndisabled = 0;
            for (size_t i = 0; i < app->options.ndisabled; i++) {
                if (!app->options.structure_disabled[i])
                    continue;
                char *d = strdup(app->options.structure_disabled[i]);
                if (!d)
                    continue;
                char **nd = realloc(refresh.disabled,
                                    (refresh.ndisabled + 1) * sizeof(char *));
                if (!nd) {
                    free(d);
                    continue;
                }
                refresh.disabled = nd;
                refresh.disabled[refresh.ndisabled++] = d;
            }
            for (size_t i = 0; i < refresh.nentries; i++) {
                free(refresh.entries[i].name);
                free(refresh.entries[i].args.key);
                free(refresh.entries[i].args.key_color);
                free(refresh.entries[i].args.format);
                free(refresh.entries[i].args.prefix);
                free(refresh.entries[i].args.output_color);
                free(refresh.entries[i].args.type);
                json_free(refresh.entries[i].raw);
            }
            free(refresh.entries);
            refresh.entries = NULL;
            refresh.nentries = 0;
            int entries_ok = 1;
            for (size_t i = 0; i < nentries; i++) {
                BuildEntry *nd = realloc(refresh.entries,
                                         (refresh.nentries + 1) * sizeof(BuildEntry));
                if (!nd) {
                    entries_ok = 0;
                    break;
                }
                refresh.entries = nd;
                BuildEntry *d = &refresh.entries[refresh.nentries++];
                memset(d, 0, sizeof *d);
                d->is_object = entries[i].is_object;
                d->name = entries[i].name ? strdup(entries[i].name) : NULL;
                if (!d->name) {
                    refresh.nentries--;
                    continue;
                }
                if (entries[i].args.key)
                    d->args.key = strdup(entries[i].args.key);
                if (entries[i].args.key_color)
                    d->args.key_color = strdup(entries[i].args.key_color);
                if (entries[i].args.format)
                    d->args.format = strdup(entries[i].args.format);
                if (entries[i].args.prefix)
                    d->args.prefix = strdup(entries[i].args.prefix);
                d->args.hide_if_empty = entries[i].args.hide_if_empty;
                d->args.hide_if_not_supported = entries[i].args.hide_if_not_supported;
                if (entries[i].args.output_color)
                    d->args.output_color = strdup(entries[i].args.output_color);
                d->args.title = entries[i].args.title;
                if (entries[i].args.type)
                    d->args.type = strdup(entries[i].args.type);
                d->args.has_fmt = entries[i].args.has_fmt;
                d->raw = json_clone(entries[i].raw);
            }
            (void)entries_ok;
            refresh.gen = refresh_gen;
            refresh.ready = 0;
            for (size_t i = 0; i < refresh.nlines; i++)
                free(refresh.lines[i]);
            free(refresh.lines);
            refresh.lines = NULL;
            refresh.nlines = 0;
            pthread_mutex_unlock(&refresh.mu);
            if (pthread_create(&refresh_thr, NULL, refresh_thread_fn, &refresh) == 0) {
                has_refresh_thr = 1;
            } else {
                pthread_mutex_lock(&refresh.mu);
                refresh_busy = 0;
                pthread_mutex_unlock(&refresh.mu);
            }
        }
        pthread_mutex_lock(&refresh.mu);
        if (refresh.ready) {
            refresh.ready = 0;
            pthread_mutex_unlock(&refresh.mu);
            if (has_refresh_thr) {
                pthread_join(refresh_thr, NULL);
                has_refresh_thr = 0;
            }
            pthread_mutex_lock(&refresh.mu);
            refresh_busy = 0;
            if (refresh.gen == refresh_gen) {
                for (size_t i = 0; i < nbase; i++)
                    free(base_lines[i]);
                free(base_lines);
                base_lines = refresh.lines;
                nbase = refresh.nlines;
                refresh.lines = NULL;
                refresh.nlines = 0;
                needs_draw = 1;
            } else {
                for (size_t i = 0; i < refresh.nlines; i++)
                    free(refresh.lines[i]);
                free(refresh.lines);
                refresh.lines = NULL;
                refresh.nlines = 0;
            }
        }
        pthread_mutex_unlock(&refresh.mu);
        size_t info_count = nbase;
        size_t render_height = info_count + 2;
        if (render_height < 36)
            render_height = 36;
        size_t tcols = 0, trows = 0;
        jf_terminal_size(&tcols, &trows);
        if (trows > 0 && render_height > (trows > 1 ? trows : 1))
            render_height = trows > 1 ? trows : 1;
        if (animated || display_live) {
            now = jf_now_ms();
            if (now - shark_polled >= 30) {
                int want = base_cfg.live_colors || active_cfg.live_colors ||
                           base_cfg.live_term_colors || active_cfg.live_term_colors ||
                           display_live;
                SharkvisMode pm = (display_live && mode == SVM_OFF) ? SVM_AUTO : mode;
                live_frame_free_contents(&shark_live);
                shark_live = sync_poll(shark_sync, pm, active_cfg.beat_depth, want);
                shark_polled = jf_now_ms();
            } else {
                const LiveFrame *last = sync_last(shark_sync);
                live_frame_free_contents(&shark_live);
                shark_live = *last;
                shark_live.glyphs = NULL;
                shark_live.nglyphs = 0;
                if (last->nglyphs && last->glyphs) {
                    shark_live.glyphs = malloc(last->nglyphs * sizeof(char *));
                    if (shark_live.glyphs) {
                        size_t k = 0;
                        for (; k < last->nglyphs; k++) {
                            shark_live.glyphs[k] = strdup(last->glyphs[k]);
                            if (!shark_live.glyphs[k])
                                break;
                        }
                        if (k == last->nglyphs) {
                            shark_live.nglyphs = last->nglyphs;
                        } else {
                            for (size_t j = 0; j < k; j++)
                                free(shark_live.glyphs[j]);
                            free(shark_live.glyphs);
                            shark_live.glyphs = NULL;
                        }
                    }
                }
            }
            if (display_live && !animated && sv_has_display_color(&shark_live))
                needs_draw = 1;
        }
        Rgb tpal[16];
        int term_flow = (active_cfg.live_term_colors || base_cfg.live_term_colors) &&
                        sv_term_palette(tpal);
        if (term_flow) {
            Rgb tlo, thi;
            sv_term_flow(shark_sync, shark_live.energy, tpal, &tlo, &thi);
            shark_live.active = 1;
            shark_live.has_grad = 1;
            shark_live.has_flat = 0;
            shark_live.glo = tlo;
            shark_live.ghi = thi;
            shark_live.has_term_pal = 1;
            memcpy(shark_live.term_pal, tpal, sizeof tpal);
            if (display_live && !animated)
                needs_draw = 1;
        }
        if (animated) {
            int logo_active = mode != SVM_OFF && shark_live.active;
            using_active = logo_active ? 1 : 0;
            AnimConfig *ccfg = using_active ? &active_cfg : &base_cfg;
            LogoCloud *cloud = using_active ? active_cloud : base_cloud;
            spin_phase += (double)shark_live.speed_mult;
            RenderFx fx;
            render_fx_none(&fx);
            if (using_active) {
                if (ccfg->live_colors) {
                    if (shark_live.has_grad) {
                        fx.has_grad = 1;
                        fx.grad_lo[0] = shark_live.glo.r;
                        fx.grad_lo[1] = shark_live.glo.g;
                        fx.grad_lo[2] = shark_live.glo.b;
                        fx.grad_hi[0] = shark_live.ghi.r;
                        fx.grad_hi[1] = shark_live.ghi.g;
                        fx.grad_hi[2] = shark_live.ghi.b;
                        if (shark_live.has_term_pal) {
                            fx.has_term_pal = 1;
                            memcpy(fx.term_pal, shark_live.term_pal, sizeof fx.term_pal);
                        }
                    } else if (shark_live.has_flat) {
                        fx.has_grad = 1;
                        fx.grad_lo[0] = fx.grad_hi[0] = shark_live.flat.r;
                        fx.grad_lo[1] = fx.grad_hi[1] = shark_live.flat.g;
                        fx.grad_lo[2] = fx.grad_hi[2] = shark_live.flat.b;
                        if (shark_live.has_term_pal) {
                            fx.has_term_pal = 1;
                            memcpy(fx.term_pal, shark_live.term_pal, sizeof fx.term_pal);
                        }
                    }
                }
                if (!ccfg->original_glyphs && !ccfg->shading_explicit && shark_live.nglyphs &&
                    shark_live.glyphs) {
                    char **sh = malloc(shark_live.nglyphs * sizeof(char *));
                    if (sh) {
                        fx.has_shading = 1;
                        fx.shading = sh;
                        fx.nshading = 0;
                        for (size_t i = 0; i < shark_live.nglyphs; i++) {
                            if (!shark_live.glyphs[i])
                                continue;
                            char *d = strdup(shark_live.glyphs[i]);
                            if (!d)
                                continue;
                            fx.shading[fx.nshading++] = d;
                        }
                        if (fx.nshading == 0) {
                            free(fx.shading);
                            fx.shading = NULL;
                            fx.has_shading = 0;
                        }
                    }
                }
                uint64_t fx_now = jf_now_ms();
                float dt = (float)(fx_now - last_fx) / 1000.0f;
                if (dt < 0.001f)
                    dt = 0.001f;
                if (dt > 0.5f)
                    dt = 0.5f;
                last_fx = fx_now;
                float yaw_step = 0, pitch_step = 0;
                anim_stereo_spin(shark_live.left, shark_live.right, &yaw_step, &pitch_step);
                yaw_phase += yaw_step;
                pitch_phase += pitch_step;
                if (shark_live.energy > 0.04f) {
                    roll_phase += (double)shark_live.energy * 0.09;
                    last_sound = fx_now;
                } else if (ccfg->has_return_secs) {
                    if ((float)(fx_now - last_sound) / 1000.0f >= ccfg->return_secs) {
                        yaw_phase = anim_ease_to_root(yaw_phase, dt);
                        pitch_phase = anim_ease_to_root(pitch_phase, dt);
                        roll_phase = anim_ease_to_root(roll_phase, dt);
                    }
                }
                fx.audio[0] = (float)jf_wrap_angle(pitch_phase);
                fx.audio[1] = (float)jf_wrap_angle(yaw_phase);
                fx.audio[2] = (float)jf_wrap_angle(roll_phase);
                float boom = ccfg->has_boom ? ccfg->boom : 0.0f;
                fx.scale = 1.0f + ccfg->grow * shark_live.beat + boom * shark_live.energy;
                if (fx.scale < 1.02f)
                    fx.scale = 1.0f;
            }
            if (term_flow && !fx.has_grad && shark_live.has_grad) {
                fx.has_grad = 1;
                fx.grad_lo[0] = shark_live.glo.r;
                fx.grad_lo[1] = shark_live.glo.g;
                fx.grad_lo[2] = shark_live.glo.b;
                fx.grad_hi[0] = shark_live.ghi.r;
                fx.grad_hi[1] = shark_live.ghi.g;
                fx.grad_hi[2] = shark_live.ghi.b;
                if (shark_live.has_term_pal) {
                    fx.has_term_pal = 1;
                    memcpy(fx.term_pal, shark_live.term_pal, sizeof fx.term_pal);
                }
            }
            ResolvedLogo *anim_logo = NULL;
            if (cloud)
                anim_logo = anim_render_cloud_with_fx(cloud, spin_phase, ccfg, render_height,
                                                      info_count, &fx);
            else if (base_logo)
                anim_logo = logo_clone(base_logo);
            render_fx_free(&fx);
            out.len = 0;
            jf_buf_put(&out, "\x1b[H");
            const char **alines = NULL;
            size_t nanim = 0;
            if (anim_logo) {
                alines = (const char **)anim_logo->lines;
                nanim = anim_logo->nlines;
            }
            JfBuf frame;
            memset(&frame, 0, sizeof frame);
            draw_animated_frame(&frame, alines, nanim, base_lines, nbase, tcols,
                                &shark_live, display_live);
            jf_buf_put(&out, frame.data ? frame.data : "");
            jf_buf_free(&frame);
            if (!jf_colors_enabled()) {
                char *tmp = malloc(out.len + 1);
                if (tmp) {
                    jf_strip_sgr(out.data ? out.data : "", tmp, out.len + 1);
                    printf("%s", tmp);
                    free(tmp);
                } else {
                    printf("%s", out.data ? out.data : "");
                }
            } else {
                printf("%s", out.data ? out.data : "");
            }
            fflush(stdout);
            anim_resolved_free(anim_logo);
        } else if (needs_draw) {
            out.len = 0;
            jf_buf_put(&out, "\x1b[H");
            JfBuf frame;
            memset(&frame, 0, sizeof frame);
            draw_static_live(&frame, base_logo, base_lines, nbase, tcols, render_height,
                             &shark_live, display_live);
            jf_buf_put(&out, frame.data ? frame.data : "");
            jf_buf_free(&frame);
            if (!jf_colors_enabled()) {
                char *tmp = malloc(out.len + 1);
                if (tmp) {
                    jf_strip_sgr(out.data ? out.data : "", tmp, out.len + 1);
                    printf("%s", tmp);
                    free(tmp);
                } else {
                    printf("%s", out.data ? out.data : "");
                }
            } else {
                printf("%s", out.data ? out.data : "");
            }
            fflush(stdout);
            needs_draw = 0;
        }
        unsigned long long interval_us = anim_frame_interval_us(&base_cfg);
        size_t slices = (size_t)(interval_us / 1000 / 10);
        if (slices < 1)
            slices = 1;
        if (slices > 200)
            slices = 200;
        int quit = 0;
        for (size_t s = 0; s < slices; s++) {
            struct timespec ts = {0, 10000000};
            nanosleep(&ts, NULL);
            KeyAction ka = poll_key_action(tty_fd, is_tty, &pending);
            if (ka == KEY_QUIT) {
                quit = 1;
                break;
            }
            if (ka == KEY_TOGGLE && base_logo) {
                animated = !animated;
                needs_draw = 1;
            }
        }
        if (quit)
            break;
    }
    printf("\x1b[?25h\x1b[0m\n");
    fflush(stdout);
    if (has_refresh_thr) {
        pthread_join(refresh_thr, NULL);
        has_refresh_thr = 0;
    }
    pthread_mutex_lock(&refresh.mu);
    refresh_busy = 0;
    refresh.ready = 0;
    for (size_t i = 0; i < refresh.nlines; i++)
        free(refresh.lines[i]);
    free(refresh.lines);
    refresh.lines = NULL;
    refresh.nlines = 0;
    pthread_mutex_unlock(&refresh.mu);
    if (have_term)
        tcsetattr(tty_fd, TCSANOW, &orig_term);
    if (ttyf)
        fclose(ttyf);
    live_have_term = 0;
    free(pending.data);
    anim_resolved_free(base_logo);
    free_lines(base_lines, nbase);
    anim_config_free(&base_cfg);
    anim_config_free(&active_cfg);
    anim_cloud_free(base_cloud);
    anim_cloud_free(active_cloud);
    sync_free(shark_sync);
    live_frame_free_contents(&shark_live);
    free(watch_path);
    jf_buf_free(&out);
    config_free(&refresh.cfg);
    for (size_t i = 0; i < refresh.ndisabled; i++)
        free(refresh.disabled[i]);
    free(refresh.disabled);
    for (size_t i = 0; i < refresh.nentries; i++) {
        free(refresh.entries[i].name);
        free(refresh.entries[i].args.key);
        free(refresh.entries[i].args.key_color);
        free(refresh.entries[i].args.format);
        free(refresh.entries[i].args.prefix);
        free(refresh.entries[i].args.output_color);
        free(refresh.entries[i].args.type);
        json_free(refresh.entries[i].raw);
    }
    free(refresh.entries);
    for (size_t i = 0; i < refresh.nlines; i++)
        free(refresh.lines[i]);
    free(refresh.lines);
    pthread_mutex_destroy(&refresh.mu);
    free_entries(entries, nentries);
    return 0;
}

int app_run(App *app) {
    app_load_config(app);
    if (!app->config.loaded_from && !app->options.no_config)
        free(app_ensure_default_config(app));
    app->config.display.text_live = profile_text_live(&app->config.logo);
    apply_logo_overrides(app);
    pick_logo(app);
    size_t nentries = 0;
    BuildEntry *entries = build_entries(app, &nentries);
    if (app->options.json) {
        print_json(app, entries, nentries);
        free_entries(entries, nentries);
        return 0;
    }
    if (!app->options.force_static && app_stdout_is_tty())
        return run_live(app, entries, nentries, should_animate(app));
    if (should_animate(app))
        return run_live(app, entries, nentries, 1);
    int rc = run_static(app, entries, nentries);
    free_entries(entries, nentries);
    return rc;
}
