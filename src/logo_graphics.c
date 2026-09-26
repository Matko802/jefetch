#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "img_b64.h"
#include "logo.h"
#include "logo_image.h"
#include "print.h"

#define KITTY_PROBE_ID 31u
#define KITTY_CHUNK 4096

typedef struct {
    int fd;
    struct termios orig_term;
    int orig_flags;
    FILE *file;
} TtyQuery;

static TtyQuery *tty_open(void) {
    FILE *file = fopen("/dev/tty", "r+");
    if (!file)
        return NULL;
    int fd = fileno(file);
    struct termios orig;
    if (tcgetattr(fd, &orig) != 0) {
        fclose(file);
        return NULL;
    }
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1) {
        fclose(file);
        return NULL;
    }
    struct termios raw = orig;
    raw.c_lflag &= (unsigned)(~(ICANON | ECHO));
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &raw) != 0) {
        fclose(file);
        return NULL;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        tcsetattr(fd, TCSANOW, &orig);
        fclose(file);
        return NULL;
    }
    TtyQuery *q = malloc(sizeof *q);
    q->fd = fd;
    q->orig_term = orig;
    q->orig_flags = flags;
    q->file = file;
    return q;
}

static void tty_close(TtyQuery *q) {
    if (!q)
        return;
    fcntl(q->fd, F_SETFL, q->orig_flags);
    tcsetattr(q->fd, TCSANOW, &q->orig_term);
    fclose(q->file);
    free(q);
}

static int tty_write_all(TtyQuery *q, const uint8_t *bytes, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t k = write(q->fd, bytes + off, n - off);
        if (k <= 0) {
            if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                struct timespec ts = {0, 5000000};
                nanosleep(&ts, NULL);
                continue;
            }
            return 0;
        }
        off += (size_t)k;
    }
    return 1;
}

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

typedef int (*DoneFn)(const uint8_t *, size_t);

static uint8_t *tty_read_until_fn(TtyQuery *q, unsigned budget_ms, DoneFn done, size_t *n) {
    size_t cap = 1024, len = 0;
    uint8_t *buf = malloc(cap);
    if (!buf) {
        *n = 0;
        return NULL;
    }
    uint64_t start = now_ms();
    for (;;) {
        uint8_t tmp[512];
        ssize_t k = read(q->fd, tmp, sizeof tmp);
        if (k > 0) {
            if (len + (size_t)k > cap) {
                size_t ncap = (len + (size_t)k) * 2;
                if (ncap > 256 * 1024)
                    break;
                uint8_t *nd = realloc(buf, ncap);
                if (!nd)
                    break;
                buf = nd;
                cap = ncap;
            }
            if (len + (size_t)k > cap)
                break;
            memcpy(buf + len, tmp, (size_t)k);
            len += (size_t)k;
            if (done(buf, len))
                break;
        }
        if (now_ms() - start >= budget_ms)
            break;
        struct timespec ts = {0, 10000000};
        nanosleep(&ts, NULL);
    }
    *n = len;
    return buf;
}

static int never_done(const uint8_t *b, size_t n) {
    (void)b;
    (void)n;
    return 0;
}

static void tty_drain(TtyQuery *q, unsigned budget_ms) {
    size_t n = 0;
    uint8_t *b = tty_read_until_fn(q, budget_ms, never_done, &n);
    free(b);
}

int graphics_kitty_response_ok(const uint8_t *buf, size_t n) {
    char id[32];
    snprintf(id, sizeof id, "_Gi=%u", KITTY_PROBE_ID);
    char *text = malloc(n + 1);
    memcpy(text, buf, n);
    text[n] = 0;
    int ok = strstr(text, id) != NULL && strstr(text, "OK") != NULL;
    free(text);
    return ok;
}

static int da_complete(const uint8_t *buf, size_t n) {
    char *text = malloc(n + 1);
    memcpy(text, buf, n);
    text[n] = 0;
    char *i = strstr(text, "\x1b[?");
    int ok = 0;
    if (i)
        ok = strchr(i, 'c') != NULL;
    free(text);
    return ok;
}

static int kitty_or_da(const uint8_t *b, size_t n) {
    if (graphics_kitty_response_ok(b, n))
        return 1;
    return da_complete(b, n);
}

int graphics_sixel_response_ok(const uint8_t *buf, size_t n) {
    char *text = malloc(n + 1);
    memcpy(text, buf, n);
    text[n] = 0;
    int ok = 0;
    char *p = text;
    while ((p = strstr(p, "\x1b[?")) != NULL && !ok) {
        p += 3;
        char *c = strchr(p, 'c');
        if (!c)
            break;
        char body[256];
        size_t l = (size_t)(c - p);
        if (l >= sizeof body)
            l = sizeof body - 1;
        memcpy(body, p, l);
        body[l] = 0;
        char *save = NULL;
        char *tok = strtok_r(body, ";", &save);
        while (tok) {
            char digits[64];
            size_t d = 0;
            for (size_t i = 0; tok[i] && d + 1 < sizeof digits; i++) {
                if (tok[i] >= '0' && tok[i] <= '9')
                    digits[d++] = tok[i];
            }
            digits[d] = 0;
            if (!strcmp(digits, "4")) {
                ok = 1;
                break;
            }
            tok = strtok_r(NULL, ";", &save);
        }
        p = c + 1;
    }
    free(text);
    return ok;
}

int graphics_detect(GraphicsProto *out) {
    const char *tp = getenv("TERM_PROGRAM");
    if (tp) {
        if (!strcmp(tp, "iTerm.app") || !strcmp(tp, "WezTerm")) {
            *out = GFX_ITERM2;
            return 1;
        }
        if (!strcmp(tp, "ghostty")) {
            *out = GFX_KITTY;
            return 1;
        }
    }
    if (getenv("KITTY_WINDOW_ID")) {
        *out = GFX_KITTY;
        return 1;
    }
    const char *term = getenv("TERM");
    if (term && !strcmp(term, "xterm-kitty")) {
        *out = GFX_KITTY;
        return 1;
    }
    TtyQuery *tty = tty_open();
    if (!tty)
        return 0;
    char probe[128];
    snprintf(probe, sizeof probe, "\x1b_Gi=%u,s=1,v=1,a=q,t=d,f=24,m=0;AAAA\x1b\\\x1b[c",
             KITTY_PROBE_ID);
    int ok = tty_write_all(tty, (uint8_t *)probe, strlen(probe));
    int proto = -1;
    if (ok) {
        size_t n = 0;
        uint8_t *buf = tty_read_until_fn(tty, 300, kitty_or_da, &n);
        if (graphics_kitty_response_ok(buf, n))
            proto = 0;
        else if (graphics_sixel_response_ok(buf, n))
            proto = 1;
        free(buf);
    }
    tty_close(tty);
    if (proto == 0) {
        *out = GFX_KITTY;
        return 1;
    }
    if (proto == 1) {
        *out = GFX_SIXEL;
        return 1;
    }
    return 0;
}

int graphics_parse_cpr(const uint8_t *buf, size_t n, unsigned *row, unsigned *col) {
    char *text = malloc(n + 1);
    memcpy(text, buf, n);
    text[n] = 0;
    char *i = NULL, *p = text;
    while ((p = strstr(p, "\x1b[")) != NULL) {
        i = p;
        p += 2;
    }
    int ok = 0;
    if (i) {
        char *rest = i + 2;
        char *end = strchr(rest, 'R');
        if (end) {
            *end = 0;
            char *semi = strchr(rest, ';');
            if (semi) {
                *semi = 0;
                char *e1, *e2;
                unsigned long r = strtoul(rest, &e1, 10);
                unsigned long c = strtoul(semi + 1, &e2, 10);
                if (e1 != rest && e2 != semi + 1) {
                    *row = (unsigned)r;
                    *col = (unsigned)c;
                    ok = 1;
                }
            }
        }
    }
    free(text);
    return ok;
}

int graphics_cursor_pos(unsigned *row, unsigned *col) {
    TtyQuery *tty = tty_open();
    if (!tty)
        return 0;
    if (!tty_write_all(tty, (const uint8_t *)"\x1b[6n", 4)) {
        tty_close(tty);
        return 0;
    }
    int ok = 0;
    size_t cap = 256, len = 0;
    uint8_t *buf = malloc(cap);
    if (!buf) {
        tty_close(tty);
        return 0;
    }
    uint64_t start = now_ms();
    for (;;) {
        uint8_t tmp[128];
        ssize_t k = read(tty->fd, tmp, sizeof tmp);
        if (k > 0) {
            if (len + (size_t)k > cap) {
                size_t ncap = (len + (size_t)k) * 2;
                if (ncap > 8192)
                    break;
                uint8_t *nd = realloc(buf, ncap);
                if (!nd)
                    break;
                buf = nd;
                cap = ncap;
            }
            if (len + (size_t)k > cap)
                break;
            memcpy(buf + len, tmp, (size_t)k);
            len += (size_t)k;
            unsigned r = 0, c = 0;
            if (graphics_parse_cpr(buf, len, &r, &c)) {
                *row = r;
                *col = c;
                ok = 1;
                break;
            }
        }
        if (now_ms() - start >= 200)
            break;
        struct timespec ts = {0, 10000000};
        nanosleep(&ts, NULL);
    }
    free(buf);
    tty_close(tty);
    return ok;
}

int graphics_parse_cell_size(const uint8_t *buf, size_t n, unsigned *w, unsigned *h) {
    char *text = malloc(n + 1);
    memcpy(text, buf, n);
    text[n] = 0;
    char *i = NULL, *p = text;
    while ((p = strstr(p, "\x1b[4;")) != NULL) {
        i = p;
        p += 4;
    }
    int ok = 0;
    if (i) {
        char *rest = i + 4;
        char *end = strchr(rest, 't');
        if (end) {
            *end = 0;
            char *semi = strchr(rest, ';');
            if (semi) {
                *semi = 0;
                char *e1, *e2;
                unsigned long hh = strtoul(rest, &e1, 10);
                unsigned long ww = strtoul(semi + 1, &e2, 10);
                if (e1 != rest && e2 != semi + 1) {
                    *w = (unsigned)ww;
                    *h = (unsigned)hh;
                    ok = 1;
                }
            }
        }
    }
    free(text);
    return ok;
}

int graphics_cell_size(unsigned *w, unsigned *h) {
    TtyQuery *tty = tty_open();
    if (!tty)
        return 0;
    if (!tty_write_all(tty, (const uint8_t *)"\x1b[14t", 5)) {
        tty_close(tty);
        return 0;
    }
    int ok = 0;
    size_t cap = 256, len = 0;
    uint8_t *buf = malloc(cap);
    if (!buf) {
        tty_close(tty);
        return 0;
    }
    uint64_t start = now_ms();
    for (;;) {
        uint8_t tmp[128];
        ssize_t k = read(tty->fd, tmp, sizeof tmp);
        if (k > 0) {
            if (len + (size_t)k > cap) {
                size_t ncap = (len + (size_t)k) * 2;
                if (ncap > 8192)
                    break;
                uint8_t *nd = realloc(buf, ncap);
                if (!nd)
                    break;
                buf = nd;
                cap = ncap;
            }
            if (len + (size_t)k > cap)
                break;
            memcpy(buf + len, tmp, (size_t)k);
            len += (size_t)k;
            unsigned ww = 0, hh = 0;
            if (graphics_parse_cell_size(buf, len, &ww, &hh)) {
                *w = ww;
                *h = hh;
                ok = 1;
                break;
            }
        }
        if (now_ms() - start >= 200)
            break;
        struct timespec ts = {0, 10000000};
        nanosleep(&ts, NULL);
    }
    free(buf);
    tty_close(tty);
    return ok;
}

char *graphics_kitty_transmit_seq(const uint8_t *rgba, unsigned w, unsigned h,
                                  unsigned id) {
    if (!rgba || w == 0 || h == 0 || (size_t)w * h > 4 * 1024 * 1024)
        return strdup("");
    size_t n = (size_t)w * h * 4;
    size_t bl = jf_b64_len(n);
    char *enc = malloc(bl);
    if (!enc)
        return strdup("");
    jf_b64_encode(rgba, n, enc);
    size_t el = strlen(enc);
    size_t cap = el + 256;
    if (cap > 64 * 1024 * 1024) {
        free(enc);
        return strdup("");
    }
    char *out = malloc(cap);
    if (!out) {
        free(enc);
        return strdup("");
    }
    size_t pos = 0;
    size_t i = 0;
    int first = 1;
    while (i < el) {
        size_t j = i + KITTY_CHUNK;
        if (j > el)
            j = el;
        int last = j == el;
        char saved = enc[j];
        enc[j] = 0;
        int n2;
        if (first) {
            n2 = snprintf(NULL, 0, "\x1b_Ga=t,f=32,s=%u,v=%u,i=%u,m=%d,q=1;%s\x1b\\",
                          w, h, id, last ? 0 : 1, enc + i);
            if (n2 < 0)
                break;
            while (pos + (size_t)n2 + 1 > cap) {
                if (cap >= 64 * 1024 * 1024)
                    break;
                size_t ncap = cap * 2;
                char *nd = realloc(out, ncap);
                if (!nd)
                    break;
                out = nd;
                cap = ncap;
            }
            if (pos + (size_t)n2 + 1 > cap)
                break;
            pos += (size_t)snprintf(out + pos, cap - pos,
                                    "\x1b_Ga=t,f=32,s=%u,v=%u,i=%u,m=%d,q=1;%s\x1b\\",
                                    w, h, id, last ? 0 : 1, enc + i);
            first = 0;
        } else {
            n2 = snprintf(NULL, 0, "\x1b_Gm=%d,q=1;%s\x1b\\", last ? 0 : 1, enc + i);
            if (n2 < 0)
                break;
            while (pos + (size_t)n2 + 1 > cap) {
                if (cap >= 64 * 1024 * 1024)
                    break;
                size_t ncap = cap * 2;
                char *nd = realloc(out, ncap);
                if (!nd)
                    break;
                out = nd;
                cap = ncap;
            }
            if (pos + (size_t)n2 + 1 > cap)
                break;
            pos += (size_t)snprintf(out + pos, cap - pos, "\x1b_Gm=%d,q=1;%s\x1b\\",
                                    last ? 0 : 1, enc + i);
        }
        enc[j] = saved;
        i = j;
    }
    free(enc);
    out[pos] = 0;
    return out;
}

char *graphics_kitty_place_seq(unsigned id, unsigned cols, unsigned rows) {
    char *o = malloc(64);
    snprintf(o, 64, "\x1b_Ga=p,i=%u,c=%u,r=%u,C=1,q=1\x1b\\", id, cols, rows);
    return o;
}

char *graphics_sixel_encode(const uint8_t *rgba, size_t w, size_t h) {
    if (w == 0 || h == 0)
        return strdup("");
    if (!rgba || w > 2000 || h > 2000 || w * h > 4 * 1024 * 1024)
        return strdup("");
    size_t np = w * h;
    uint8_t *regs = malloc(np);
    if (!regs)
        return strdup("");
    int used[216] = {0};
    for (size_t i = 0; i < np; i++) {
        unsigned a = rgba[i * 4 + 3];
        unsigned r6 = rgba[i * 4] * a * 5 / (255 * 255);
        unsigned g6 = rgba[i * 4 + 1] * a * 5 / (255 * 255);
        unsigned b6 = rgba[i * 4 + 2] * a * 5 / (255 * 255);
        unsigned reg = r6 * 36 + g6 * 6 + b6;
        regs[i] = (uint8_t)reg;
        used[reg] = 1;
    }
    size_t cap = w * h / 2 + 1024;
    if (cap > 16 * 1024 * 1024)
        cap = 16 * 1024 * 1024;
    char *out = malloc(cap);
    if (!out) {
        free(regs);
        return strdup("");
    }
    size_t pos = 0;
    int wlen = snprintf(out + pos, cap - pos, "\x1bPq\"1;1;%zu;%zu", w, h);
    if (wlen < 0 || (size_t)wlen >= cap - pos) {
        free(regs);
        free(out);
        return strdup("");
    }
    pos += (size_t)wlen;
    for (int reg = 0; reg < 216; reg++) {
        if (!used[reg])
            continue;
        wlen = snprintf(out + pos, cap - pos, "#%d;2;%d;%d;%d", 16 + reg,
                        (reg / 36) * 20, ((reg % 36) / 6) * 20, (reg % 6) * 20);
        if (wlen < 0 || (size_t)wlen >= cap - pos) {
            free(regs);
            free(out);
            return strdup("");
        }
        pos += (size_t)wlen;
    }
    size_t bands = (h + 5) / 6;
    for (size_t band = 0; band < bands; band++) {
        size_t y0 = band * 6;
        for (int reg = 0; reg < 216; reg++) {
            if (!used[reg])
                continue;
            while (pos + 64 > cap) {
                if (cap >= 16 * 1024 * 1024) {
                    free(regs);
                    free(out);
                    return strdup("");
                }
                size_t ncap = cap * 2;
                if (ncap > 16 * 1024 * 1024)
                    ncap = 16 * 1024 * 1024;
                char *nd = realloc(out, ncap);
                if (!nd) {
                    free(regs);
                    free(out);
                    return strdup("");
                }
                out = nd;
                cap = ncap;
            }
            wlen = snprintf(out + pos, cap - pos, "#%d", 16 + reg);
            if (wlen < 0 || (size_t)wlen >= cap - pos) {
                free(regs);
                free(out);
                return strdup("");
            }
            pos += (size_t)wlen;
            size_t x = 0;
            while (x < w) {
                unsigned bits = 0;
                for (int k = 0; k < 6; k++) {
                    size_t y = y0 + (size_t)k;
                    if (y < h && regs[y * w + x] == (uint8_t)reg)
                        bits |= 1u << k;
                }
                size_t run = 1;
                while (x + run < w && run < 255) {
                    unsigned next = 0;
                    for (int k = 0; k < 6; k++) {
                        size_t y = y0 + (size_t)k;
                        if (y < h && regs[y * w + x + run] == (uint8_t)reg)
                            next |= 1u << k;
                    }
                    if (next != bits)
                        break;
                    run++;
                }
                char ch = (char)(63 + bits);
                if (run > 3) {
                    while (pos + 32 > cap) {
                        if (cap >= 16 * 1024 * 1024) {
                            free(regs);
                            free(out);
                            return strdup("");
                        }
                        size_t ncap = cap * 2;
                        char *nd = realloc(out, ncap);
                        if (!nd) {
                            free(regs);
                            free(out);
                            return strdup("");
                        }
                        out = nd;
                        cap = ncap;
                    }
                    wlen = snprintf(out + pos, cap - pos, "!%zu%c", run, ch);
                    if (wlen < 0 || (size_t)wlen >= cap - pos) {
                        free(regs);
                        free(out);
                        return strdup("");
                    }
                    pos += (size_t)wlen;
                } else {
                    while (pos + run + 1 > cap) {
                        if (cap >= 16 * 1024 * 1024) {
                            free(regs);
                            free(out);
                            return strdup("");
                        }
                        size_t ncap = cap * 2;
                        char *nd = realloc(out, ncap);
                        if (!nd) {
                            free(regs);
                            free(out);
                            return strdup("");
                        }
                        out = nd;
                        cap = ncap;
                    }
                    for (size_t k = 0; k < run; k++)
                        out[pos++] = ch;
                    out[pos] = 0;
                }
                x += run;
            }
        }
        if (band + 1 < bands) {
            if (pos + 1 >= cap) {
                free(regs);
                free(out);
                return strdup("");
            }
            out[pos++] = '-';
        }
        out[pos] = 0;
    }
    if (pos + 3 > cap) {
        free(regs);
        free(out);
        return strdup("");
    }
    memcpy(out + pos, "\x1b\\", 2);
    pos += 2;
    out[pos] = 0;
    free(regs);
    return out;
}

char *graphics_iterm2_seq(const uint8_t *png, size_t pngn, const char *name,
                          unsigned cols, unsigned rows) {
    if (!png || pngn == 0 || pngn > 32 * 1024 * 1024 || !name)
        return strdup("");
    size_t nl = jf_b64_len(strlen(name));
    char *nb = malloc(nl);
    if (!nb)
        return strdup("");
    jf_b64_encode((const uint8_t *)name, strlen(name), nb);
    size_t pl = jf_b64_len(pngn);
    char *pb = malloc(pl);
    if (!pb) {
        free(nb);
        return strdup("");
    }
    jf_b64_encode(png, pngn, pb);
    size_t cap = strlen(nb) + pl + 256;
    char *o = malloc(cap);
    if (!o) {
        free(nb);
        free(pb);
        return strdup("");
    }
    snprintf(o, cap,
             "\x1b]1337;File=name=%s;size=%zu;width=%u;height=%u;"
             "preserveAspectRatio=0;inline=1:%s\x07",
             nb, pngn, cols, rows, pb);
    free(nb);
    free(pb);
    return o;
}

static int stdout_is_tty(void) {
    return isatty(STDOUT_FILENO) == 1;
}

static int write_out(const char *s) {
    size_t n = strlen(s);
    size_t off = 0;
    while (off < n) {
        ssize_t k = write(STDOUT_FILENO, s + off, n - off);
        if (k <= 0) {
            if (k < 0 && errno == EINTR)
                continue;
            return 0;
        }
        off += (size_t)k;
    }
    return 1;
}

static void cup_str(char *out, size_t n, unsigned row, size_t col) {
    if (row < 1)
        row = 1;
    if (col < 1)
        col = 1;
    snprintf(out, n, "\x1b[%u;%zuH", row, col);
}

typedef struct {
    unsigned start_row;
    unsigned image_row;
    size_t image_col;
    size_t text_col;
    size_t term_cols;
    size_t rows;
} Layout;

static int begin_layout(size_t logo_cols, size_t logo_rows, size_t gap, size_t pad_left,
                        size_t pad_top, size_t text_rows, Layout *lay) {
    unsigned sr = 0, sc = 0;
    if (!graphics_cursor_pos(&sr, &sc))
        return 0;
    size_t tc = 0, tr = 0;
    jf_terminal_size(&tc, &tr);
    size_t text_col = 1 + pad_left + logo_cols + gap;
    if (text_col > tc)
        return 0;
    lay->start_row = sr;
    lay->image_row = sr + (unsigned)pad_top;
    lay->image_col = 1 + pad_left;
    lay->text_col = text_col;
    lay->term_cols = tc;
    size_t rows = pad_top + logo_rows;
    if (rows < text_rows)
        rows = text_rows;
    if (rows < 1)
        rows = 1;
    lay->rows = rows;
    return 1;
}

static void push_row(JfBuf *out, const Layout *lay, size_t i, const char *text) {
    char cup[64];
    cup_str(cup, sizeof cup, lay->start_row + (unsigned)i, lay->text_col);
    jf_buf_put(out, cup);
    size_t avail = lay->term_cols >= lay->text_col ? lay->term_cols - lay->text_col + 1 : 0;
    if (avail > 0) {
        char *tmp = malloc(strlen(text) + 16);
        jf_truncate_visible(text, avail, tmp, strlen(text) + 16);
        jf_buf_put(out, tmp);
        free(tmp);
    }
    jf_buf_put(out, "\r\n");
}

static unsigned image_id(void) {
    return 10000 + (unsigned)getpid() % 50000;
}

static void hires_dims(size_t cols, size_t rows, size_t *tw, size_t *th) {
    size_t w = cols * 8;
    if (w < 1)
        w = 1;
    if (w > 800)
        w = 800;
    size_t h = w * rows * 2 / (cols > 1 ? cols : 1);
    if (h < 1)
        h = 1;
    if (h > 800)
        h = 800;
    *tw = w;
    *th = h;
}

static int display_kitty(const RawImage *raw, const NativeSpec *spec) {
    Layout lay;
    if (!begin_layout(spec->cols, spec->rows, spec->gap, spec->pad_left, spec->pad_top,
                      spec->ntext, &lay))
        return 0;
    size_t tw, th;
    hires_dims(spec->cols, spec->rows, &tw, &th);
    uint8_t *px = logo_resize_box(raw->rgba, raw->width, raw->height, tw, th);
    if (!px)
        return 0;
    unsigned id = image_id();
    char *seq = graphics_kitty_transmit_seq(px, (unsigned)tw, (unsigned)th, id);
    free(px);
    if (!write_out(seq)) {
        free(seq);
        return 0;
    }
    free(seq);
    JfBuf out;
    memset(&out, 0, sizeof out);
    char cup[64];
    cup_str(cup, sizeof cup, lay.image_row, lay.image_col);
    jf_buf_put(&out, cup);
    char *place = graphics_kitty_place_seq(id, (unsigned)spec->cols, (unsigned)spec->rows);
    jf_buf_put(&out, place);
    free(place);
    for (size_t i = 0; i < lay.rows; i++)
        push_row(&out, &lay, i, i < spec->ntext ? spec->text[i] : "");
    int ok = write_out(out.data ? out.data : "");
    jf_buf_free(&out);
    if (!ok)
        return 0;
    TtyQuery *tty = tty_open();
    if (tty) {
        tty_drain(tty, 50);
        tty_close(tty);
    }
    return 1;
}

static int display_sixel(const RawImage *raw, const NativeSpec *spec) {
    unsigned cw = 0, ch = 0;
    if (!graphics_cell_size(&cw, &ch) || cw == 0 || ch == 0)
        return 0;
    size_t pw = spec->cols * cw;
    size_t ph = spec->rows * ch;
    if (pw > 1000) {
        double s = 1000.0 / (double)pw;
        pw = 1000;
        ph = (size_t)((double)ph * s);
        if (ph < 1)
            ph = 1;
    }
    uint8_t *px = logo_resize_box(raw->rgba, raw->width, raw->height, pw, ph);
    if (!px)
        return 0;
    Layout lay;
    if (!begin_layout(spec->cols, spec->rows, spec->gap, spec->pad_left, spec->pad_top,
                      spec->ntext, &lay)) {
        free(px);
        return 0;
    }
    char *six = graphics_sixel_encode(px, pw, ph);
    free(px);
    JfBuf out;
    memset(&out, 0, sizeof out);
    jf_buf_put(&out, six);
    free(six);
    for (size_t i = 0; i < lay.rows; i++)
        push_row(&out, &lay, i, i < spec->ntext ? spec->text[i] : "");
    int ok = write_out(out.data ? out.data : "");
    jf_buf_free(&out);
    return ok;
}

static int display_iterm2(const RawImage *raw, const NativeSpec *spec) {
    size_t tw, th;
    hires_dims(spec->cols, spec->rows, &tw, &th);
    uint8_t *px = logo_resize_box(raw->rgba, raw->width, raw->height, tw, th);
    if (!px)
        return 0;
    size_t pngn = 0;
    uint8_t *png = jf_png_encode(px, (unsigned)tw, (unsigned)th, &pngn);
    free(px);
    if (!png)
        return 0;
    Layout lay;
    if (!begin_layout(spec->cols, spec->rows, spec->gap, spec->pad_left, spec->pad_top,
                      spec->ntext, &lay)) {
        free(png);
        return 0;
    }
    char *seq = graphics_iterm2_seq(png, pngn, spec->path, (unsigned)spec->cols,
                                    (unsigned)spec->rows);
    free(png);
    JfBuf out;
    memset(&out, 0, sizeof out);
    jf_buf_put(&out, seq);
    free(seq);
    for (size_t i = 0; i < lay.rows; i++)
        push_row(&out, &lay, i, i < spec->ntext ? spec->text[i] : "");
    int ok = write_out(out.data ? out.data : "");
    jf_buf_free(&out);
    return ok;
}

int graphics_display_native(const NativeSpec *spec) {
    GraphicsProto proto;
    if (!stdout_is_tty())
        return 0;
    if (!graphics_detect(&proto))
        return 0;
    RawImage raw = {0};
    char err[256];
    if (!logo_image_load(spec->path, &raw, err, sizeof err))
        return 0;
    int ok = 0;
    if (proto == GFX_KITTY)
        ok = display_kitty(&raw, spec);
    else if (proto == GFX_SIXEL)
        ok = display_sixel(&raw, spec);
    else
        ok = display_iterm2(&raw, spec);
    raw_image_free(&raw);
    return ok;
}
