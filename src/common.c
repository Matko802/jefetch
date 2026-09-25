#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

void jf_format_bytes(unsigned long long bytes, char *out, size_t n) {
    if (bytes == 0) {
        snprintf(out, n, "0 B");
        return;
    }
    double v = (double)bytes;
    const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    int u = 0;
    while (v >= 1024.0 && u < 5) {
        v /= 1024.0;
        u++;
    }
    snprintf(out, n, "%.2f %s", v, units[u]);
}

void jf_format_uptime(unsigned long long secs, char *out, size_t n) {
    unsigned long long days = secs / 86400;
    unsigned long long hours = (secs % 86400) / 3600;
    unsigned long long mins = (secs % 3600) / 60;
    char tmp[128];
    tmp[0] = 0;
    size_t pos = 0;
    if (days > 0)
        pos += (size_t)snprintf(tmp + pos, sizeof tmp - pos, "%llu day%s",
                                days, days == 1 ? "" : "s");
    if (hours > 0) {
        if (pos > 0)
            pos += (size_t)snprintf(tmp + pos, sizeof tmp - pos, ", ");
        pos += (size_t)snprintf(tmp + pos, sizeof tmp - pos, "%llu hour%s",
                                hours, hours == 1 ? "" : "s");
    }
    if (mins > 0) {
        if (pos > 0)
            pos += (size_t)snprintf(tmp + pos, sizeof tmp - pos, ", ");
        pos += (size_t)snprintf(tmp + pos, sizeof tmp - pos, "%llu min%s",
                                mins, mins == 1 ? "" : "s");
    }
    if (pos == 0)
        snprintf(tmp, sizeof tmp, "%llu secs", secs);
    snprintf(out, n, "%s", tmp);
}

int jf_percent(unsigned long long used, unsigned long long total, unsigned *out) {
    if (total == 0)
        return 0;
    double v = ((double)used / (double)total) * 100.0;
    v = round(v);
    if (v < 0.0)
        v = 0.0;
    if (v > 100.0)
        v = 100.0;
    *out = (unsigned)v;
    return 1;
}

void jf_percent_bar(unsigned long long used, unsigned long long total, char *out, size_t n) {
    unsigned pct = 0;
    jf_percent(used, total, &pct);
    unsigned filled = (unsigned)round((double)pct / 100.0 * 10.0);
    size_t pos = 0;
    for (unsigned i = 0; i < 10 && pos + 4 < n; i++) {
        const char *g = i < filled ? "\xe2\x96\x88" : "\xe2\x96\x91";
        size_t l = 3;
        memcpy(out + pos, g, l);
        pos += l;
    }
    out[pos] = 0;
}

static size_t utf8_one(const char *s, unsigned *cw) {
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) {
        *cw = 1;
        return 1;
    }
    if ((c & 0xE0) == 0xC0) {
        *cw = 2;
        return 2;
    }
    if ((c & 0xF0) == 0xE0) {
        *cw = 2;
        return 3;
    }
    *cw = 2;
    return 4;
}

void jf_truncate_to_width(const char *s, size_t width, int ellipsis, char *out, size_t n) {
    size_t w = 0;
    size_t pos = 0;
    while (*s && pos + 5 < n) {
        unsigned cw = 1;
        size_t k = utf8_one(s, &cw);
        if (w + cw > width) {
            if (ellipsis && width == 1) {
                memcpy(out, "\xe2\x80\xa6", 3);
                pos = 3;
            }
            break;
        }
        memcpy(out + pos, s, k);
        pos += k;
        w += cw;
        s += k;
    }
    out[pos] = 0;
}

void jf_terminal_size(size_t *cols, size_t *rows) {
    struct winsize ws;
    memset(&ws, 0, sizeof ws);
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        *cols = ws.ws_col > 0 ? (size_t)ws.ws_col : 80;
        *rows = ws.ws_row > 0 ? (size_t)ws.ws_row : 24;
        return;
    }
    *cols = 80;
    *rows = 24;
}

int jf_colors_enabled(void) {
    const char *no = getenv("NO_COLOR");
    if (no && *no)
        return 0;
    const char *term = getenv("TERM");
    if (term) {
        char l[64];
        size_t i = 0;
        while (term[i] && i + 1 < sizeof l) {
            char c = term[i];
            l[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        l[i] = 0;
        if (!strcmp(l, "dumb"))
            return 0;
    }
    return 1;
}

static int locale_has_utf8(const char *v) {
    if (!v || !*v)
        return 1;
    char l[128];
    size_t i = 0, j = 0;
    while (v[i] && j + 1 < sizeof l) {
        char c = v[i++];
        if (c == '-' || c == '_')
            continue;
        l[j++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    l[j] = 0;
    return strstr(l, "utf8") != NULL;
}

int jf_utf8_supported(void) {
    if (!locale_has_utf8(getenv("LC_ALL")))
        return 0;
    if (!locale_has_utf8(getenv("LC_CTYPE")))
        return 0;
    if (!locale_has_utf8(getenv("LANG")))
        return 0;
    return 1;
}

static void buf_grow(JfBuf *b, size_t extra) {
    if (b->len + extra <= b->cap)
        return;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->len + extra)
        cap *= 2;
    char *p = realloc(b->data, cap);
    if (!p)
        abort();
    b->data = p;
    b->cap = cap;
}

void jf_buf_put(JfBuf *b, const char *s) {
    size_t n = strlen(s);
    buf_grow(b, n + 1);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}

void jf_buf_putn(JfBuf *b, const char *s, size_t n) {
    buf_grow(b, n + 1);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}

void jf_buf_putc(JfBuf *b, char c) {
    buf_grow(b, 2);
    b->data[b->len++] = c;
    b->data[b->len] = 0;
}

void jf_buf_free(JfBuf *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

uint64_t jf_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}
