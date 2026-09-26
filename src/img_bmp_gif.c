#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "img.h"

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

int img_decode_bmp(const uint8_t *d, size_t n, unsigned *w, unsigned *h,
                      uint8_t **rgba, char *err, size_t errn) {
    if (n < 54) {
        snprintf(err, errn, "bmp too small");
        return 0;
    }
    uint32_t off = rd32(d + 10);
    uint32_t dib = rd32(d + 14);
    if (dib < 40) {
        snprintf(err, errn, "unsupported bmp header");
        return 0;
    }
    int32_t wi = (int32_t)rd32(d + 18);
    int32_t hi = (int32_t)rd32(d + 22);
    uint16_t planes = rd16(d + 26);
    uint16_t bpp = rd16(d + 28);
    uint32_t comp = rd32(d + 30);
    if (planes != 1 || wi <= 0 || hi == 0) {
        snprintf(err, errn, "bad bmp dimensions");
        return 0;
    }
    int top_down = hi < 0;
    unsigned uw = (unsigned)wi;
    unsigned uh = top_down ? (unsigned)-hi : (unsigned)hi;
    if (uw == 0 || uh == 0 || uw > 16384 || uh > 16384) {
        snprintf(err, errn, "bad bmp dimensions");
        return 0;
    }
    if (comp != 0 && comp != 3) {
        snprintf(err, errn, "compressed bmp not supported");
        return 0;
    }
    uint8_t *out = malloc((size_t)uw * uh * 4);
    if (!out)
        return 0;
    if (bpp == 24 || bpp == 32) {
        size_t stride = ((size_t)uw * (bpp / 8) + 3) & ~(size_t)3;
        for (unsigned y = 0; y < uh; y++) {
            unsigned sy = top_down ? y : uh - 1 - y;
            size_t row = (size_t)off + (size_t)sy * stride;
            if (row + (size_t)uw * (bpp / 8) > n) {
                free(out);
                snprintf(err, errn, "bmp truncated");
                return 0;
            }
            for (unsigned x = 0; x < uw; x++) {
                size_t s = row + x * (bpp / 8);
                size_t o = ((size_t)y * uw + x) * 4;
                out[o] = d[s + 2];
                out[o + 1] = d[s + 1];
                out[o + 2] = d[s];
                out[o + 3] = bpp == 32 ? d[s + 3] : 255;
            }
        }
    } else if (bpp == 8 || bpp == 4 || bpp == 1) {
        unsigned ncol = 1u << bpp;
        size_t pal_off = 14 + dib;
        if (pal_off + (size_t)ncol * 4 > n) {
            free(out);
            snprintf(err, errn, "bmp truncated");
            return 0;
        }
        size_t stride = (((size_t)uw * bpp + 31) / 32) * 4;
        for (unsigned y = 0; y < uh; y++) {
            unsigned sy = top_down ? y : uh - 1 - y;
            size_t row = (size_t)off + (size_t)sy * stride;
            if (row + stride > n) {
                free(out);
                snprintf(err, errn, "bmp truncated");
                return 0;
            }
            for (unsigned x = 0; x < uw; x++) {
                unsigned idx;
                if (bpp == 8) {
                    idx = d[row + x];
                } else if (bpp == 4) {
                    idx = (x & 1) ? d[row + x / 2] & 0xF : (d[row + x / 2] >> 4) & 0xF;
                } else {
                    idx = (d[row + x / 8] >> (7 - (x % 8))) & 1;
                }
                if (idx >= ncol)
                    idx = 0;
                size_t ps = pal_off + (size_t)idx * 4;
                size_t o = ((size_t)y * uw + x) * 4;
                out[o] = d[ps + 2];
                out[o + 1] = d[ps + 1];
                out[o + 2] = d[ps];
                out[o + 3] = 255;
            }
        }
    } else {
        free(out);
        snprintf(err, errn, "unsupported bmp depth");
        return 0;
    }
    *w = uw;
    *h = uh;
    *rgba = out;
    return 1;
}

typedef struct {
    const uint8_t *d;
    size_t n;
    size_t pos;
    int bits;
    unsigned buf;
} LzwReader;

static int lzw_bits(LzwReader *r, int width) {
    while (r->bits < width) {
        if (r->pos >= r->n)
            return -1;
        r->buf |= (unsigned)r->d[r->pos++] << r->bits;
        r->bits += 8;
    }
    int v = (int)(r->buf & ((1u << width) - 1));
    r->buf >>= width;
    r->bits -= width;
    return v;
}

int img_decode_gif(const uint8_t *d, size_t n, unsigned *w, unsigned *h,
                      uint8_t **rgba, char *err, size_t errn) {
    if (n < 13) {
        snprintf(err, errn, "gif too small");
        return 0;
    }
    unsigned gw = d[6] | ((unsigned)d[7] << 8);
    unsigned gh = d[8] | ((unsigned)d[9] << 8);
    if (gw == 0 || gh == 0 || gw > 16384 || gh > 16384) {
        snprintf(err, errn, "bad gif dimensions");
        return 0;
    }
    uint8_t gct_flag = d[10];
    size_t pos = 13;
    uint8_t gpal[256][3];
    memset(gpal, 0, sizeof gpal);
    if (gct_flag & 0x80) {
        size_t ng = 2u << (gct_flag & 7);
        if (pos + ng * 3 > n) {
            snprintf(err, errn, "gif truncated");
            return 0;
        }
        for (size_t i = 0; i < ng; i++) {
            gpal[i][0] = d[pos];
            gpal[i][1] = d[pos + 1];
            gpal[i][2] = d[pos + 2];
            pos += 3;
        }
    }
    int transparent = -1;
    uint8_t *indices = NULL;
    unsigned fw = 0, fh = 0;
    int interlaced = 0;
    uint8_t lpal[256][3];
    int has_lpal = 0;
    while (pos < n) {
        uint8_t sep = d[pos++];
        if (sep == 0x3B)
            break;
        if (sep == 0x21) {
            if (pos >= n)
                break;
            uint8_t label = d[pos++];
            size_t sub = 0;
            if (label == 0xF9) {
                if (pos < n && d[pos] == 4 && pos + 6 <= n) {
                    uint8_t flags = d[pos + 1];
                    if (flags & 1)
                        transparent = d[pos + 4];
                }
            }
            while (pos < n && d[pos] != 0) {
                size_t sz = d[pos];
                pos += 1 + sz;
                sub += sz;
                if (pos > n)
                    break;
            }
            if (pos < n)
                pos++;
            (void)sub;
            continue;
        }
        if (sep != 0x2C)
            continue;
        if (pos + 9 > n)
            break;
        pos += 4;
        unsigned iw = d[pos] | ((unsigned)d[pos + 1] << 8);
        unsigned ih = d[pos + 2] | ((unsigned)d[pos + 3] << 8);
        uint8_t packed = d[pos + 4];
        pos += 5;
        interlaced = (packed & 0x40) != 0;
        if (iw == 0 || ih == 0) {
            snprintf(err, errn, "bad gif frame");
            free(indices);
            return 0;
        }
        if (packed & 0x80) {
            size_t ng = 2u << (packed & 7);
            if (pos + ng * 3 > n) {
                snprintf(err, errn, "gif truncated");
                free(indices);
                return 0;
            }
            for (size_t i = 0; i < ng; i++) {
                lpal[i][0] = d[pos];
                lpal[i][1] = d[pos + 1];
                lpal[i][2] = d[pos + 2];
                pos += 3;
            }
            has_lpal = 1;
        }
        if (pos >= n)
            break;
        int min_code = d[pos++];
        uint8_t *comp = NULL;
        size_t clen = 0, ccap = 0;
        while (pos < n && d[pos] != 0) {
            size_t sz = d[pos++];
            if (pos + sz > n)
                break;
            if (clen + sz > ccap) {
                if (clen + sz > 32 * 1024 * 1024) {
                    free(comp);
                    free(indices);
                    snprintf(err, errn, "gif too large");
                    return 0;
                }
                ccap = ccap ? ccap * 2 : 1024;
                while (ccap < clen + sz)
                    ccap *= 2;
                uint8_t *nd = realloc(comp, ccap);
                if (!nd) {
                    free(comp);
                    free(indices);
                    snprintf(err, errn, "out of memory");
                    return 0;
                }
                comp = nd;
            }
            memcpy(comp + clen, d + pos, sz);
            clen += sz;
            pos += sz;
        }
        if (pos < n)
            pos++;
        if (min_code < 2 || min_code > 8) {
            free(comp);
            free(indices);
            snprintf(err, errn, "bad gif lzw");
            return 0;
        }
        size_t npix = (size_t)iw * ih;
        uint8_t *px = malloc(npix);
        if (!px) {
            free(comp);
            free(indices);
            return 0;
        }
        uint8_t *prefix = malloc(4096);
        uint8_t *suffix = malloc(4096);
        uint8_t *stack = malloc(4097);
        if (!prefix || !suffix || !stack) {
            free(prefix);
            free(suffix);
            free(stack);
            free(px);
            free(comp);
            free(indices);
            return 0;
        }
        LzwReader r = {comp, clen, 0, 0, 0};
        int clear = 1 << min_code;
        int eoi = clear + 1;
        int width = min_code + 1;
        int next = eoi + 1;
        for (int i = 0; i < clear; i++) {
            prefix[i] = (uint8_t)i;
            suffix[i] = (uint8_t)i;
        }
        size_t got = 0;
        int prev = -1;
        int first = 0;
        int code = lzw_bits(&r, width);
        while (code != eoi && got < npix) {
            if (code < 0)
                break;
            if (code == clear) {
                width = min_code + 1;
                next = eoi + 1;
                prev = -1;
                code = lzw_bits(&r, width);
                continue;
            }
            int cur = code;
            size_t sp = 0;
            if (code >= next) {
                if (prev < 0)
                    break;
                cur = prev;
                stack[sp++] = (uint8_t)first;
            }
            while (cur >= clear) {
                if (cur >= next || sp >= 4096)
                    break;
                stack[sp++] = suffix[cur];
                cur = prefix[cur];
            }
            if (cur >= next)
                break;
            first = cur;
            stack[sp++] = (uint8_t)cur;
            while (sp > 0 && got < npix)
                px[got++] = stack[--sp];
            if (prev >= 0 && next < 4096) {
                prefix[next] = (uint8_t)prev;
                suffix[next] = (uint8_t)first;
                next++;
                if (next == (1 << width) && width < 12)
                    width++;
            }
            prev = code;
            code = lzw_bits(&r, width);
        }
        free(prefix);
        free(suffix);
        free(stack);
        free(comp);
        if (got < npix)
            memset(px + got, 0, npix - got);
        free(indices);
        indices = px;
        fw = iw;
        fh = ih;
        break;
    }
    if (!indices) {
        snprintf(err, errn, "no gif frame");
        return 0;
    }
    uint8_t *out = malloc((size_t)fw * fh * 4);
    if (!out) {
        free(indices);
        return 0;
    }
    uint8_t (*pal)[3] = has_lpal ? lpal : gpal;
    if (interlaced) {
        size_t pass_row[4] = {0, 4, 2, 1};
        size_t pass_step[4] = {8, 8, 4, 2};
        size_t src = 0;
        for (int pass = 0; pass < 4; pass++) {
            for (size_t y = pass_row[pass]; y < fh; y += pass_step[pass]) {
                for (size_t x = 0; x < fw && src < (size_t)fw * fh; x++, src++) {
                    uint8_t idx = indices[src];
                    size_t o = ((size_t)y * fw + x) * 4;
                    out[o] = pal[idx][0];
                    out[o + 1] = pal[idx][1];
                    out[o + 2] = pal[idx][2];
                    out[o + 3] = (int)idx == transparent ? 0 : 255;
                }
            }
        }
    } else {
        for (size_t i = 0; i < (size_t)fw * fh; i++) {
            uint8_t idx = indices[i];
            out[i * 4] = pal[idx][0];
            out[i * 4 + 1] = pal[idx][1];
            out[i * 4 + 2] = pal[idx][2];
            out[i * 4 + 3] = (int)idx == transparent ? 0 : 255;
        }
    }
    free(indices);
    *w = fw;
    *h = fh;
    *rgba = out;
    return 1;
}
