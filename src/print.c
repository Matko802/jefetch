#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "config.h"
#include "print.h"

const char *JF_RESET = "\x1b[0m";
const char *JF_SHARKVIS_PLACEHOLDER_START = "\x1b[38;2;1;2;3m";

int jf_is_sharkvis_color_name(const char *s) {
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

const char *jf_live_text_suffix(int text_live) {
    return text_live ? JF_SHARKVIS_PLACEHOLDER_START : "";
}

void apply_result_free(ApplyResult *r) {
    free(r->start);
    free(r->end);
    r->start = NULL;
    r->end = NULL;
}

static char *sgr_from_codes(const unsigned *codes, size_t n) {
    size_t need = 4;
    for (size_t i = 0; i < n; i++)
        need += 5;
    char *o = malloc(need);
    size_t pos = 0;
    o[pos++] = 0x1b;
    o[pos++] = '[';
    for (size_t i = 0; i < n; i++) {
        if (i > 0)
            o[pos++] = ';';
        pos += (size_t)snprintf(o + pos, need - pos, "%u", codes[i]);
    }
    o[pos++] = 'm';
    o[pos] = 0;
    return o;
}

char *jf_named_color_sgr(const char *name) {
    if (jf_is_sharkvis_color_name(name))
        return strdup(JF_SHARKVIS_PLACEHOLDER_START);
    char n[128];
    size_t i = 0;
    while (name[i] == ' ' || name[i] == '\t')
        name++;
    size_t l = strlen(name);
    while (l > 0 && (name[l - 1] == ' ' || name[l - 1] == '\t'))
        l--;
    if (l >= sizeof n)
        l = sizeof n - 1;
    memcpy(n, name, l);
    n[l] = 0;
    if (!n[0] || !strcmp(n, "reset") || !strcmp(n, "reset_default") || !strcmp(n, "#"))
        return strdup(JF_RESET);
    if (!strcmp(n, "default") || !strcmp(n, "fg_default"))
        return strdup("\x1b[39m");
    if (!strcmp(n, "bg_default"))
        return strdup("\x1b[49m");
    static const char *prefixes[] = {
        "bold_", "bold-", "italic_", "italic-", "underline_", "underline-",
        "invert_", "invert-", "reverse_", "dim_", "dim-", "strikethrough_",
        "strikethrough-", NULL
    };
    char style[32] = "";
    char base[128];
    snprintf(base, sizeof base, "%s", n);
    for (int k = 0; prefixes[k]; k++) {
        size_t pl = strlen(prefixes[k]);
        if (!strncmp(n, prefixes[k], pl)) {
            size_t sl = pl - 1;
            if (sl >= sizeof style)
                sl = sizeof style - 1;
            memcpy(style, prefixes[k], sl);
            style[sl] = 0;
            snprintf(base, sizeof base, "%s", n + pl);
            break;
        }
    }
    unsigned codes[8];
    size_t nc = 0;
    if (!strcmp(style, "bold"))
        codes[nc++] = 1;
    else if (!strcmp(style, "italic"))
        codes[nc++] = 3;
    else if (!strcmp(style, "underline"))
        codes[nc++] = 4;
    else if (!strcmp(style, "invert") || !strcmp(style, "reverse"))
        codes[nc++] = 7;
    else if (!strcmp(style, "dim"))
        codes[nc++] = 2;
    else if (!strcmp(style, "strikethrough"))
        codes[nc++] = 9;
    int is_bg = 0;
    char b2[128];
    snprintf(b2, sizeof b2, "%s", base);
    if (!strncmp(b2, "bg_", 3) || !strncmp(b2, "bg-", 3)) {
        is_bg = 1;
        memmove(b2, b2 + 3, strlen(b2 + 3) + 1);
    } else if (!strncmp(b2, "fg_", 3) || !strncmp(b2, "fg-", 3)) {
        memmove(b2, b2 + 3, strlen(b2 + 3) + 1);
    }
    char bl[128];
    size_t bi = 0;
    while (b2[bi] && bi + 1 < sizeof bl) {
        char c = b2[bi];
        bl[bi++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    bl[bi] = 0;
    unsigned esc = 0;
    if (!strcmp(bl, "black"))
        esc = 30;
    else if (!strcmp(bl, "red"))
        esc = 31;
    else if (!strcmp(bl, "green"))
        esc = 32;
    else if (!strcmp(bl, "yellow"))
        esc = 33;
    else if (!strcmp(bl, "blue"))
        esc = 34;
    else if (!strcmp(bl, "magenta") || !strcmp(bl, "purple"))
        esc = 35;
    else if (!strcmp(bl, "cyan"))
        esc = 36;
    else if (!strcmp(bl, "white"))
        esc = 37;
    else if (!strcmp(bl, "bright_black") || !strcmp(bl, "bright-black") ||
             !strcmp(bl, "gray") || !strcmp(bl, "grey"))
        esc = 90;
    else if (!strcmp(bl, "bright_red") || !strcmp(bl, "bright-red"))
        esc = 91;
    else if (!strcmp(bl, "bright_green") || !strcmp(bl, "bright-green"))
        esc = 92;
    else if (!strcmp(bl, "bright_yellow") || !strcmp(bl, "bright-yellow"))
        esc = 93;
    else if (!strcmp(bl, "bright_blue") || !strcmp(bl, "bright-blue"))
        esc = 94;
    else if (!strcmp(bl, "bright_magenta") || !strcmp(bl, "bright-magenta") ||
             !strcmp(bl, "bright_purple"))
        esc = 95;
    else if (!strcmp(bl, "bright_cyan") || !strcmp(bl, "bright-cyan"))
        esc = 96;
    else if (!strcmp(bl, "bright_white") || !strcmp(bl, "bright-white"))
        esc = 97;
    if (esc == 0) {
        char *end;
        unsigned long num = strtoul(b2, &end, 10);
        if (end != b2 && *end == 0 && num < 256) {
            codes[nc++] = (unsigned)num;
            return sgr_from_codes(codes, nc);
        }
        if (b2[0] == '#' && strlen(b2) == 7) {
            int ok = 1;
            for (int q = 1; q < 7; q++) {
                char c = b2[q];
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                    ok = 0;
                    break;
                }
            }
            if (ok) {
                unsigned long v = strtoul(b2 + 1, NULL, 16);
                char *o = malloc(32);
                snprintf(o, 32, "\x1b[38;2;%lu;%lu;%lum", (v >> 16) & 0xff,
                         (v >> 8) & 0xff, v & 0xff);
                return o;
            }
        }
        if (strchr(b2, ';')) {
            int allnum = 1;
            char *dup = strdup(b2);
            char *save = NULL;
            char *tok = strtok_r(dup, ";", &save);
            if (!tok)
                allnum = 0;
            while (tok) {
                for (char *p = tok; *p; p++) {
                    if (*p < '0' || *p > '9') {
                        allnum = 0;
                        break;
                    }
                }
                if (!allnum)
                    break;
                tok = strtok_r(NULL, ";", &save);
            }
            free(dup);
            if (allnum) {
                char *o = malloc(strlen(b2) + 4);
                snprintf(o, strlen(b2) + 4, "\x1b[%sm", b2);
                return o;
            }
        }
        return NULL;
    }
    codes[nc++] = is_bg ? esc + 10 : esc;
    return sgr_from_codes(codes, nc);
}

ApplyResult jf_color_code_to_ansi(const char *color) {
    ApplyResult r = {0};
    if (jf_is_sharkvis_color_name(color)) {
        r.is_ansi = 1;
        r.start = strdup(JF_SHARKVIS_PLACEHOLDER_START);
        r.end = strdup(JF_RESET);
        return r;
    }
    char *sgr = jf_named_color_sgr(color);
    if (sgr) {
        r.is_ansi = 1;
        r.start = sgr;
        r.end = strdup(JF_RESET);
        return r;
    }
    char *end;
    unsigned long num = strtoul(color, &end, 10);
    while (*color == ' ' || *color == '\t')
        color++;
    if (end != color && *end == 0 && num < 65536) {
        r.is_ansi = 1;
        r.start = malloc(16);
        snprintf(r.start, 16, "\x1b[%lum", num);
        r.end = strdup(JF_RESET);
        return r;
    }
    if (color[0] == '#' && strlen(color) == 7) {
        int ok = 1;
        for (int i = 1; i < 7; i++) {
            char c = color[i];
            int hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            if (!hex) {
                ok = 0;
                break;
            }
        }
        if (ok) {
            unsigned long v = strtoul(color + 1, NULL, 16);
            r.is_ansi = 1;
            r.start = malloc(32);
            snprintf(r.start, 32, "\x1b[38;2;%lu;%lu;%lum", (v >> 16) & 0xff,
                     (v >> 8) & 0xff, v & 0xff);
            r.end = strdup(JF_RESET);
            return r;
        }
    }
    return r;
}

char *jf_expand_dollar_code(const char *code) {
    if (!strcmp(code, "reset"))
        return strdup(JF_RESET);
    if (!strcmp(code, "b"))
        return strdup("\x1b[1m");
    if (!strcmp(code, "i"))
        return strdup("\x1b[3m");
    if (!strcmp(code, "u"))
        return strdup("\x1b[4m");
    if (!strcmp(code, "s"))
        return strdup("\x1b[9m");
    if (!strcmp(code, "d"))
        return strdup("\x1b[2m");
    if (!strcmp(code, "c1"))
        return strdup("\x1b[32m");
    if (!strcmp(code, "c2"))
        return strdup("\x1b[36m");
    if (!strcmp(code, "c3"))
        return strdup("\x1b[34m");
    if (!strcmp(code, "c4"))
        return strdup("\x1b[35m");
    if (!strcmp(code, "c5"))
        return strdup("\x1b[31m");
    if (!strcmp(code, "c6"))
        return strdup("\x1b[33m");
    if (!strcmp(code, "c7"))
        return strdup("\x1b[37m");
    if (!strcmp(code, "c8"))
        return strdup("\x1b[90m");
    if (!strcmp(code, "c9"))
        return strdup("\x1b[91m");
    return jf_named_color_sgr(code);
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

size_t jf_visible_len(const char *s) {
    size_t count = 0;
    const unsigned char *b = (const unsigned char *)s;
    size_t i = 0, n = strlen(s);
    while (i < n) {
        if (b[i] == 0x1b && i + 1 < n && b[i + 1] == '[') {
            size_t j = i + 2;
            while (j < n && !((b[j] >= 'A' && b[j] <= 'Z') || (b[j] >= 'a' && b[j] <= 'z')))
                j++;
            i = j + 1 < n ? j + 1 : n;
            continue;
        }
        count++;
        i += u8len(b[i]);
    }
    return count;
}

void jf_truncate_visible(const char *s, size_t max, char *out, size_t n) {
    const unsigned char *b = (const unsigned char *)s;
    size_t len = strlen(s);
    size_t i = 0, w = 0, pos = 0;
    int cut = 0;
    out[0] = 0;
    while (i < len) {
        if (b[i] == 0x1b && i + 1 < len && b[i + 1] == '[') {
            size_t j = i + 2;
            while (j < len && !((b[j] >= 'A' && b[j] <= 'Z') || (b[j] >= 'a' && b[j] <= 'z')))
                j++;
            size_t end = j + 1 < len ? j + 1 : len;
            if (pos + (end - i) + 1 < n) {
                memcpy(out + pos, s + i, end - i);
                pos += end - i;
            }
            i = end;
            continue;
        }
        if (w + 1 > max) {
            cut = 1;
            break;
        }
        size_t l = u8len(b[i]);
        if (pos + l + 1 < n) {
            memcpy(out + pos, s + i, l);
            pos += l;
        }
        w++;
        i += l;
    }
    if (cut && pos + 5 < n) {
        memcpy(out + pos, "\x1b[0m", 4);
        pos += 4;
    }
    out[pos] = 0;
}

void jf_strip_sgr(const char *s, char *out, size_t n) {
    const unsigned char *b = (const unsigned char *)s;
    size_t len = strlen(s);
    size_t i = 0, pos = 0;
    while (i < len && pos + 5 < n) {
        if (b[i] == 0x1b && i + 1 < len && b[i + 1] == '[') {
            size_t j = i + 2;
            while (j < len && ((b[j] >= '0' && b[j] <= '9') || b[j] == ';'))
                j++;
            if (j < len && b[j] == 'm') {
                i = j + 1;
                continue;
            }
        }
        size_t l = u8len(b[i]);
        memcpy(out + pos, s + i, l);
        pos += l;
        i += l;
    }
    out[pos] = 0;
}

typedef struct {
    JfBuf text;
    size_t length;
} FmtOut;

static void fmt_put(FmtOut *o, const char *s, size_t n) {
    size_t cap = o->text.cap;
    if (!cap)
        cap = 256;
    while (o->text.len + n + 1 > cap)
        cap *= 2;
    if (cap != o->text.cap) {
        o->text.data = realloc(o->text.data, cap);
        o->text.cap = cap;
    }
    memcpy(o->text.data + o->text.len, s, n);
    o->text.len += n;
    o->text.data[o->text.len] = 0;
}

static void push_rendered(FmtOut *o, const char *s) {
    const unsigned char *b = (const unsigned char *)s;
    size_t n = strlen(s);
    size_t i = 0;
    while (i < n) {
        if (b[i] == 0x1b && i + 1 < n && b[i + 1] == '[') {
            size_t j = i + 2;
            while (j < n && !((b[j] >= 'A' && b[j] <= 'Z') || (b[j] >= 'a' && b[j] <= 'z')))
                j++;
            size_t end = j + 1 < n ? j + 1 : n;
            fmt_put(o, s + i, end - i);
            i = end;
            continue;
        }
        size_t l = u8len(b[i]);
        fmt_put(o, s + i, l);
        o->length++;
        i += l;
    }
}

static int has_value(const JfResolver *r, const char *name) {
    while (*name == ' ' || *name == '\t')
        name++;
    if (!*name)
        return 0;
    char *v = r->get_placeholder(r->ctx, name);
    if (!v)
        return 0;
    const char *p = v;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    int ok = *p != 0;
    free(v);
    return ok;
}

static void apply_options(const char *v, const char *opts, char *out, size_t n) {
    while (*opts == ' ' || *opts == '\t')
        opts++;
    if (!*opts) {
        snprintf(out, n, "%s", v);
        return;
    }
    int neg = 0;
    if (*opts == '-') {
        neg = 1;
        opts++;
    }
    char *end;
    unsigned long width = strtoul(opts, &end, 10);
    if (end == opts || *end != 0) {
        snprintf(out, n, "%s", v);
        return;
    }
    size_t cur = jf_visible_len(v);
    if (cur < width) {
        size_t pad = width - cur;
        char spaces[512];
        if (pad >= sizeof spaces)
            pad = sizeof spaces - 1;
        memset(spaces, ' ', pad);
        spaces[pad] = 0;
        if (neg)
            snprintf(out, n, "%s%s", v, spaces);
        else
            snprintf(out, n, "%s%s", spaces, v);
    } else if (cur > width) {
        jf_truncate_visible(v, width, out, n);
    } else {
        snprintf(out, n, "%s", v);
    }
}

static void resolve_placeholder(const char *name, FmtOut *o, const JfResolver *r) {
    while (*name == ' ' || *name == '\t')
        name++;
    char trimmed[512];
    snprintf(trimmed, sizeof trimmed, "%s", name);
    char *e = trimmed + strlen(trimmed);
    while (e > trimmed && (e[-1] == ' ' || e[-1] == '\t'))
        *--e = 0;
    char low[512];
    size_t i = 0;
    while (trimmed[i] && i + 1 < sizeof low) {
        char c = trimmed[i];
        low[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    low[i] = 0;
    if (!strcmp(low, "key")) {
        const char *k = r->key(r->ctx);
        push_rendered(o, k);
        return;
    }
    char base[512], opts[512];
    opts[0] = 0;
    snprintf(base, sizeof base, "%s", trimmed);
    for (const char *sep = ",:"; *sep; sep++) {
        char *pos = strchr(base, *sep);
        if (pos) {
            *pos = 0;
            snprintf(opts, sizeof opts, "%s", pos + 1);
            char *be = base + strlen(base);
            while (be > base && (be[-1] == ' ' || be[-1] == '\t'))
                *--be = 0;
            break;
        }
    }
    char *v = r->get_placeholder(r->ctx, base);
    if (v) {
        if (opts[0]) {
            char tmp[2048];
            apply_options(v, opts, tmp, sizeof tmp);
            push_rendered(o, tmp);
        } else {
            push_rendered(o, v);
        }
        free(v);
    } else {
        char tmp[600];
        snprintf(tmp, sizeof tmp, "{%s}", name);
        push_rendered(o, tmp);
    }
}

static void track_cond(const char *token, int **cond, size_t *ncond, size_t *ccond,
                       const JfResolver *r) {
    while (*token == ' ' || *token == '\t')
        token++;
    if (*token == '?') {
        const char *expr = token + 1;
        while (*expr == ' ' || *expr == '\t')
            expr++;
        int has;
        if (*expr == '!') {
            expr++;
            while (*expr == ' ' || *expr == '\t')
                expr++;
            has = !has_value(r, expr);
        } else {
            has = has_value(r, expr);
        }
        if (*ncond >= *ccond) {
            *ccond = *ccond ? *ccond * 2 : 8;
            *cond = realloc(*cond, *ccond * sizeof(int));
        }
        (*cond)[(*ncond)++] = !has;
    } else if (!strcmp(token, "?")) {
        if (*ncond > 0)
            (*ncond)--;
    }
}

static int cond_skip(const int *cond, size_t ncond) {
    for (size_t i = 0; i < ncond; i++) {
        if (cond[i])
            return 1;
    }
    return 0;
}

static int read_brace(const char *fmt, size_t i, size_t n, char *tok, size_t tn,
                      size_t *next) {
    int depth = 1;
    size_t j = i + 1;
    while (j < n) {
        if (fmt[j] == '{')
            depth++;
        else if (fmt[j] == '}') {
            depth--;
            if (depth == 0) {
                size_t l = j - i - 1;
                if (l >= tn)
                    l = tn - 1;
                memcpy(tok, fmt + i + 1, l);
                tok[l] = 0;
                *next = j + 1;
                return 1;
            }
        }
        j++;
    }
    return 0;
}

static int read_dollar(const char *fmt, size_t i, size_t n, char *code, size_t cn,
                       size_t *next) {
    if (i + 1 >= n)
        return 0;
    if (fmt[i + 1] == '{') {
        size_t j = i + 2;
        while (j < n && fmt[j] != '}')
            j++;
        if (j < n) {
            size_t l = j - i - 2;
            if (l >= cn)
                l = cn - 1;
            memcpy(code, fmt + i + 2, l);
            code[l] = 0;
            *next = j + 1;
            return 1;
        }
        return 0;
    }
    size_t l = 0;
    size_t j = i + 1;
    while (j < n && (isalnum((unsigned char)fmt[j]) || fmt[j] == '-')) {
        l++;
        j++;
    }
    if (l > 0) {
        if (l >= cn)
            l = cn - 1;
        memcpy(code, fmt + i + 1, l);
        code[l] = 0;
        *next = i + 1 + l;
        return 1;
    }
    return 0;
}

static void handle_token(const char *token, FmtOut *o, const JfResolver *r,
                         int **cond, size_t *ncond, size_t *ccond) {
    while (*token == ' ' || *token == '\t')
        token++;
    char t[1024];
    snprintf(t, sizeof t, "%s", token);
    char *e = t + strlen(t);
    while (e > t && (e[-1] == ' ' || e[-1] == '\t'))
        *--e = 0;
    if (t[0] == '?') {
        const char *expr = t + 1;
        while (*expr == ' ' || *expr == '\t')
            expr++;
        int has;
        if (*expr == '!') {
            expr++;
            while (*expr == ' ' || *expr == '\t')
                expr++;
            has = !has_value(r, expr);
        } else {
            has = has_value(r, expr);
        }
        if (*ncond >= *ccond) {
            *ccond = *ccond ? *ccond * 2 : 8;
            *cond = realloc(*cond, *ccond * sizeof(int));
        }
        (*cond)[(*ncond)++] = !has;
        return;
    }
    if (!strcmp(t, "?")) {
        if (*ncond > 0)
            (*ncond)--;
        return;
    }
    if (t[0] == '#') {
        const char *nm = t + 1;
        while (*nm == ' ' || *nm == '\t')
            nm++;
        if (!*nm) {
            fmt_put(o, JF_RESET, strlen(JF_RESET));
            return;
        }
        char low[256];
        size_t i = 0;
        while (nm[i] && i + 1 < sizeof low) {
            char c = nm[i];
            low[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        low[i] = 0;
        if (!strcmp(low, "keys") && r->get_color) {
            char *c = r->get_color(r->ctx, "keys");
            if (c) {
                fmt_put(o, c, strlen(c));
                free(c);
                return;
            }
        }
        char *sgr = jf_named_color_sgr(nm);
        if (sgr) {
            fmt_put(o, sgr, strlen(sgr));
            free(sgr);
            return;
        }
        char tmp[1100];
        snprintf(tmp, sizeof tmp, "{#%.1024s}", nm);
        push_rendered(o, tmp);
        return;
    }
    if (t[0] == '$') {
        char *end;
        unsigned long idx = strtoul(t + 1, &end, 10);
        if (end != t + 1 && *end == 0 && r->get_constant) {
            char *v = r->get_constant(r->ctx, (unsigned)idx);
            if (v) {
                push_rendered(o, v);
                free(v);
            }
            return;
        }
    }
    resolve_placeholder(t, o, r);
}

JfFormatResult jf_format(const char *fmt, const JfResolver *r) {
    FmtOut o;
    memset(&o, 0, sizeof o);
    int *cond = NULL;
    size_t ncond = 0, ccond = 0;
    size_t n = strlen(fmt);
    size_t i = 0;
    char tok[1024];
    while (i < n) {
        unsigned char c = (unsigned char)fmt[i];
        if (cond_skip(cond, ncond)) {
            if (c == '{') {
                size_t next = 0;
                if (read_brace(fmt, i, n, tok, sizeof tok, &next)) {
                    i = next;
                    track_cond(tok, &cond, &ncond, &ccond, r);
                    continue;
                }
            }
            i += u8len(c);
            continue;
        }
        if (c == '{') {
            size_t next = 0;
            if (read_brace(fmt, i, n, tok, sizeof tok, &next)) {
                handle_token(tok, &o, r, &cond, &ncond, &ccond);
                i = next;
                continue;
            }
            fmt_put(&o, "{", 1);
            o.length++;
            i++;
        } else if (c == '$') {
            size_t next = 0;
            if (read_dollar(fmt, i, n, tok, sizeof tok, &next)) {
                char *s = jf_expand_dollar_code(tok);
                if (s) {
                    fmt_put(&o, s, strlen(s));
                    free(s);
                } else {
                    fmt_put(&o, "$", 1);
                    fmt_put(&o, tok, strlen(tok));
                    o.length += 1 + strlen(tok);
                }
                i = next;
                continue;
            }
            fmt_put(&o, "$", 1);
            o.length++;
            i++;
        } else {
            size_t l = u8len(c);
            fmt_put(&o, fmt + i, l);
            o.length++;
            i += l;
        }
    }
    free(cond);
    JfFormatResult fr;
    fr.text = o.text.data ? o.text.data : strdup("");
    fr.length = o.length;
    return fr;
}

void jf_format_free(JfFormatResult *fr) {
    free(fr->text);
    fr->text = NULL;
}

typedef struct {
    const char *key;
    const JfPair *pairs;
    size_t n;
} MapCtx;

static char *map_get(void *ctx, const char *name) {
    MapCtx *m = ctx;
    for (size_t i = 0; i < m->n; i++) {
        const char *k = m->pairs[i].key;
        size_t a = 0, b = 0;
        while (k[a] && name[a]) {
            char ca = k[a], cb = name[a];
            if (ca >= 'A' && ca <= 'Z')
                ca += 32;
            if (cb >= 'A' && cb <= 'Z')
                cb += 32;
            if (ca != cb)
                break;
            a++;
            b++;
        }
        if (k[a] == 0 && name[b] == 0)
            return strdup(m->pairs[i].value);
    }
    return NULL;
}

static const char *map_key(void *ctx) {
    return ((MapCtx *)ctx)->key;
}

char *jf_format_map(const char *fmt, const char *key, const JfPair *pairs, size_t n) {
    MapCtx m = {key, pairs, n};
    JfResolver r = {map_get, map_key, NULL, NULL, &m};
    JfFormatResult fr = jf_format(fmt, &r);
    return fr.text;
}

char **module_render_ansi_lines(const ModuleRender *r, size_t *n) {
    *n = 0;
    if (r->nlines == 0)
        return NULL;
    const DisplayConfig *d = r->display;
    const ModuleArgs *a = r->args;
    const char *key = (a->key && a->key[0]) ? a->key : r->key;
    const char *color = NULL;
    if (a->key_color && a->key_color[0])
        color = a->key_color;
    else if (d->key_color && d->key_color[0])
        color = d->key_color;
    JfBuf kp;
    memset(&kp, 0, sizeof kp);
    if (!color || !*color) {
        jf_buf_put(&kp, key);
    } else {
        ApplyResult ar = jf_color_code_to_ansi(color);
        if (ar.is_ansi) {
            jf_buf_put(&kp, ar.start);
            if (d->text_live)
                jf_buf_put(&kp, JF_SHARKVIS_PLACEHOLDER_START);
            jf_buf_put(&kp, key);
            jf_buf_put(&kp, ar.end);
        } else {
            jf_buf_put(&kp, key);
        }
        apply_result_free(&ar);
    }
    size_t key_visible = jf_visible_len(kp.data ? kp.data : "");
    JfBuf sep;
    memset(&sep, 0, sizeof sep);
    if (d->separator_color && d->separator_color[0]) {
        ApplyResult ar = jf_color_code_to_ansi(d->separator_color);
        if (ar.is_ansi) {
            jf_buf_put(&sep, ar.start);
            if (d->text_live)
                jf_buf_put(&sep, JF_SHARKVIS_PLACEHOLDER_START);
            jf_buf_put(&sep, d->separator);
            jf_buf_put(&sep, ar.end);
        } else {
            jf_buf_put(&sep, d->separator);
        }
        apply_result_free(&ar);
    } else {
        jf_buf_put(&sep, d->separator);
    }
    char **out = malloc(r->nlines * sizeof(char *));
    for (size_t i = 0; i < r->nlines; i++) {
        JfBuf line;
        memset(&line, 0, sizeof line);
        if (i == 0) {
            jf_buf_put(&line, kp.data ? kp.data : "");
            jf_buf_put(&line, sep.data ? sep.data : "");
            for (size_t p = 0; p < d->padding; p++)
                jf_buf_putc(&line, ' ');
            jf_buf_put(&line, r->value_lines[i]);
            for (size_t p = 0; p < r->pad_right; p++)
                jf_buf_putc(&line, ' ');
        } else {
            size_t blank = key_visible + 1 + d->padding;
            for (size_t p = 0; p < blank; p++)
                jf_buf_putc(&line, ' ');
            jf_buf_put(&line, r->value_lines[i]);
        }
        out[i] = line.data ? line.data : strdup("");
    }
    *n = r->nlines;
    jf_buf_free(&kp);
    jf_buf_free(&sep);
    return out;
}

void module_render_free_lines(char **lines, size_t n) {
    for (size_t i = 0; i < n; i++)
        free(lines[i]);
    free(lines);
}
