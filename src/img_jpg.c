#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "img.h"

typedef struct {
    const uint8_t *d;
    size_t n;
    size_t pos;
    unsigned buf;
    int bits;
} Jbr;

static unsigned jb_bits(Jbr *r, int count) {
    while (r->bits < count) {
        if (r->pos >= r->n)
            return 0;
        uint8_t b = r->d[r->pos++];
        if (b == 0xFF) {
            if (r->pos < r->n && r->d[r->pos] == 0x00)
                r->pos++;
        }
        r->buf = (r->buf << 8) | b;
        r->bits += 8;
    }
    r->bits -= count;
    return (r->buf >> r->bits) & (count == 32 ? 0xFFFFFFFFu : ((1u << count) - 1));
}

static int jextend(unsigned v, int s) {
    if (s == 0)
        return 0;
    unsigned limit = 1u << (s - 1);
    if (v < limit)
        return (int)(v - ((1u << s) - 1));
    return (int)v;
}

typedef struct {
    uint8_t vals[256];
    int n;
    uint16_t counts[17];
    uint16_t mincode[17];
    uint16_t maxcode[17];
    int valptr[17];
} Jhuff2;

static int jhuff_build(Jhuff2 *h, const uint8_t *counts, const uint8_t *vals, int nvals) {
    int k = 0;
    int code = 0;
    h->n = 0;
    for (int len = 0; len < 17; len++) {
        h->counts[len] = 0;
        h->mincode[len] = 0;
        h->maxcode[len] = 0;
        h->valptr[len] = 0;
    }
    for (int len = 1; len <= 16; len++) {
        h->counts[len] = counts[len - 1];
        h->mincode[len] = (uint16_t)code;
        h->valptr[len] = k;
        for (int i = 0; i < counts[len - 1]; i++) {
            if (k >= nvals || k >= 256)
                return 0;
            h->vals[k] = vals[k];
            k++;
        }
        code += counts[len - 1];
        h->maxcode[len] = (uint16_t)(code - 1);
        code <<= 1;
        if (code > 131072)
            return 0;
    }
    h->n = k;
    return 1;
}

static int jhuff_decode(Jbr *r, Jhuff2 *h) {
    int code = 0;
    for (int len = 1; len <= 16; len++) {
        if (r->pos >= r->n && r->bits == 0)
            return -1;
        while (r->bits == 0) {
            if (r->pos >= r->n)
                return -1;
            uint8_t b = r->d[r->pos++];
            if (b == 0xFF) {
                if (r->pos < r->n && r->d[r->pos] == 0x00)
                    r->pos++;
                else if (r->pos < r->n)
                    return -2;
            }
            r->buf = (r->buf << 8) | b;
            r->bits += 8;
        }
        code = (code << 1) | (int)((r->buf >> (r->bits - 1)) & 1);
        r->bits--;
        if ((unsigned)(code - h->mincode[len]) < h->counts[len]) {
            int idx = h->valptr[len] + (code - h->mincode[len]);
            if (idx < 0 || idx >= h->n)
                return -1;
            return h->vals[idx];
        }
    }
    return -1;
}

static int idct_block(const int *in, uint8_t *out, int stride) {
    static float ct[8][8];
    static int init = 0;
    if (!init) {
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 8; j++)
                ct[i][j] = cosf((2 * i + 1) * j * 3.14159265f / 16.0f);
        }
        init = 1;
    }
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            float s = 0;
            for (int v = 0; v < 8; v++) {
                for (int u = 0; u < 8; u++) {
                    float cu = u == 0 ? 0.70710678f : 1.0f;
                    float cv = v == 0 ? 0.70710678f : 1.0f;
                    s += cu * cv * in[v * 8 + u] * ct[x][u] * ct[y][v];
                }
            }
            s = s / 4.0f + 128.0f;
            int vi = (int)(s + 0.5f);
            if (vi < 0)
                vi = 0;
            if (vi > 255)
                vi = 255;
            out[y * stride + x] = (uint8_t)vi;
        }
    }
    return 1;
}

static const uint8_t ZIGZAG[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
};

int img_decode_jpg(const uint8_t *d, size_t n, unsigned *w, unsigned *h,
                   uint8_t **rgba, char *err, size_t errn) {
    if (n < 2 || d[0] != 0xFF || d[1] != 0xD8) {
        snprintf(err, errn, "not a jpeg");
        return 0;
    }
    size_t pos = 2;
    unsigned width = 0, height = 0;
    int ncomp = 0;
    int comp_h[3] = {0, 0, 0};
    int comp_v[3] = {0, 0, 0};
    int comp_tq[3] = {0, 0, 0};
    uint16_t quant[4][64];
    memset(quant, 0, sizeof quant);
    Jhuff2 huff_dc[2], huff_ac[2];
    memset(huff_dc, 0, sizeof huff_dc);
    memset(huff_ac, 0, sizeof huff_ac);
    int have_dc[2] = {0, 0};
    int have_ac[2] = {0, 0};
    int restart_interval = 0;
    size_t scan_start = 0;
    int comp_td[3] = {0, 0, 0};
    int comp_ta[3] = {0, 0, 0};
    int progressive = 0;
    while (pos + 1 < n) {
        if (d[pos] != 0xFF) {
            pos++;
            continue;
        }
        uint8_t marker = d[pos + 1];
        while (marker == 0xFF && pos + 2 < n) {
            pos++;
            marker = d[pos + 1];
        }
        if (marker == 0xD8 || marker == 0xD9) {
            pos += 2;
            if (marker == 0xD9)
                break;
            continue;
        }
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            pos += 2;
            continue;
        }
        if (pos + 4 > n)
            break;
        unsigned len = ((unsigned)d[pos + 2] << 8) | d[pos + 3];
        if (len < 2 || pos + 2 + len > n)
            break;
        const uint8_t *seg = d + pos + 4;
        size_t slen = len - 2;
        if (marker == 0xDB) {
            size_t p = 0;
            while (p + 65 <= slen) {
                int tq = seg[p] & 0xF;
                int prec = (seg[p] >> 4) & 0xF;
                p++;
                if (tq > 3)
                    break;
                for (int i = 0; i < 64; i++) {
                    if (prec)
                        quant[tq][ZIGZAG[i]] = (uint16_t)((seg[p] << 8) | seg[p + 1]);
                    else
                        quant[tq][ZIGZAG[i]] = seg[p];
                    p += prec ? 2 : 1;
                }
            }
        } else if (marker == 0xC0 || marker == 0xC2) {
            if (slen < 6)
                break;
            if (marker == 0xC2)
                progressive = 1;
            height = ((unsigned)seg[1] << 8) | seg[2];
            width = ((unsigned)seg[3] << 8) | seg[4];
            ncomp = seg[5];
            if (ncomp < 1 || ncomp > 3 || slen < 6 + (size_t)ncomp * 3)
                break;
            for (int i = 0; i < ncomp; i++) {
                comp_h[i] = (seg[7 + i * 3] >> 4) & 0xF;
                comp_v[i] = seg[7 + i * 3] & 0xF;
                comp_tq[i] = seg[8 + i * 3];
            }
        } else if (marker == 0xC4) {
            size_t p = 0;
            while (p + 17 <= slen) {
                int tc = (seg[p] >> 4) & 0xF;
                int th = seg[p] & 0xF;
                if (th > 1 || tc > 1)
                    break;
                int total = 0;
                for (int i = 0; i < 16; i++)
                    total += seg[p + 1 + i];
                if (p + 17 + (size_t)total > slen)
                    break;
                Jhuff2 hh;
                if (jhuff_build(&hh, seg + p + 1, seg + p + 17, total)) {
                    if (tc == 0) {
                        huff_dc[th] = hh;
                        have_dc[th] = 1;
                    } else {
                        huff_ac[th] = hh;
                        have_ac[th] = 1;
                    }
                }
                p += 17 + (size_t)total;
            }
        } else if (marker == 0xDD) {
            if (slen >= 2)
                restart_interval = ((int)seg[0] << 8) | seg[1];
        } else if (marker == 0xDA) {
            if (slen < 1 + (size_t)ncomp * 2 + 3)
                break;
            for (int i = 0; i < ncomp; i++) {
                comp_td[i] = (seg[1 + i * 2 + 1] >> 4) & 0xF;
                comp_ta[i] = seg[1 + i * 2 + 1] & 0xF;
            }
            scan_start = pos + 2 + len;
            break;
        }
        pos += 2 + len;
    }
    if (width == 0 || height == 0 || ncomp == 0 || width > 16384 || height > 16384) {
        snprintf(err, errn, "bad jpeg header");
        return 0;
    }
    if (progressive) {
        snprintf(err, errn, "progressive jpeg not supported");
        return 0;
    }
    int hs = comp_h[0], vs = comp_v[0];
    if (ncomp == 3) {
        if (!((hs == 2 && vs == 2) || (hs == 2 && vs == 1) || (hs == 1 && vs == 2) ||
              (hs == 1 && vs == 1))) {
            snprintf(err, errn, "unsupported jpeg sampling");
            return 0;
        }
        for (int i = 1; i < 3; i++) {
            if (comp_h[i] != 1 || comp_v[i] != 1) {
                snprintf(err, errn, "unsupported jpeg sampling");
                return 0;
            }
        }
    } else if (ncomp != 1) {
        snprintf(err, errn, "unsupported jpeg components");
        return 0;
    }
    for (int i = 0; i < ncomp; i++) {
        if (!have_dc[comp_td[i]] || !have_ac[comp_ta[i]]) {
            snprintf(err, errn, "missing jpeg huffman table");
            return 0;
        }
    }
    unsigned mcu_w = (width + 8u * (unsigned)hs - 1) / (8u * (unsigned)hs);
    unsigned mcu_h = (height + 8u * (unsigned)vs - 1) / (8u * (unsigned)vs);
    unsigned comp_w[3], comp_hpx[3];
    for (int i = 0; i < ncomp; i++) {
        comp_w[i] = mcu_w * 8u * (unsigned)comp_h[i];
        comp_hpx[i] = mcu_h * 8u * (unsigned)comp_v[i];
    }
    uint8_t *planes[3] = {NULL, NULL, NULL};
    for (int i = 0; i < ncomp; i++) {
        planes[i] = malloc((size_t)comp_w[i] * comp_hpx[i]);
        if (!planes[i]) {
            for (int k = 0; k < i; k++)
                free(planes[k]);
            return 0;
        }
    }
    Jbr r = {d, n, scan_start, 0, 0};
    int prev_dc[3] = {0, 0, 0};
    int rst_count = 0;
    int ok = 1;
    for (unsigned my = 0; my < mcu_h && ok; my++) {
        for (unsigned mx = 0; mx < mcu_w && ok; mx++) {
            for (int ci = 0; ci < ncomp && ok; ci++) {
                for (int vy = 0; vy < comp_v[ci] && ok; vy++) {
                    for (int vx = 0; vx < comp_h[ci] && ok; vx++) {
                        int block[64] = {0};
                        int s = jhuff_decode(&r, &huff_dc[comp_td[ci]]);
                        if (s < 0) {
                            ok = 0;
                            break;
                        }
                        int diff = s ? jextend((int)jb_bits(&r, s), s) : 0;
                        prev_dc[ci] += diff;
                        block[0] = prev_dc[ci] * (int)quant[comp_tq[ci]][0];
                        int k = 1;
                        while (k < 64) {
                            int rs = jhuff_decode(&r, &huff_ac[comp_ta[ci]]);
                            if (rs < 0) {
                                ok = 0;
                                break;
                            }
                            int run = (rs >> 4) & 15;
                            int sz = rs & 15;
                            if (sz != 0) {
                                k += run;
                                int ac = jextend((int)jb_bits(&r, sz), sz);
                                int tp = ZIGZAG[k & 63] & 63;
                                block[tp] = ac * (int)quant[comp_tq[ci]][tp];
                                k++;
                            } else if (run != 15) {
                                break;
                            } else {
                                k += 16;
                            }
                        }
                        if (!ok)
                            break;
                        uint8_t px[64];
                        idct_block(block, px, 8);
                        unsigned bx = mx * 8u * (unsigned)comp_h[ci] + (unsigned)vx * 8;
                        unsigned by = my * 8u * (unsigned)comp_v[ci] + (unsigned)vy * 8;
                        for (int y = 0; y < 8; y++) {
                            for (int x = 0; x < 8; x++) {
                                unsigned dx = bx + (unsigned)x;
                                unsigned dy = by + (unsigned)y;
                                if (dx < comp_w[ci] && dy < comp_hpx[ci])
                                    planes[ci][dy * comp_w[ci] + dx] = px[y * 8 + x];
                            }
                        }
                    }
                }
            }
            (void)rst_count;
            if (restart_interval > 0) {
                rst_count++;
                if (rst_count == restart_interval) {
                    rst_count = 0;
                    prev_dc[0] = prev_dc[1] = prev_dc[2] = 0;
                    r.bits = 0;
                    r.buf = 0;
                    while (r.pos + 1 < r.n) {
                        if (r.d[r.pos] == 0xFF && r.d[r.pos + 1] != 0x00) {
                            r.pos += 2;
                            break;
                        }
                        r.pos++;
                    }
                }
            }
        }
    }
    if (!ok) {
        for (int i = 0; i < ncomp; i++)
            free(planes[i]);
        snprintf(err, errn, "jpeg data error");
        return 0;
    }
    uint8_t *out = malloc((size_t)width * height * 4);
    if (!out) {
        for (int i = 0; i < ncomp; i++)
            free(planes[i]);
        return 0;
    }
    for (unsigned y = 0; y < height; y++) {
        for (unsigned x = 0; x < width; x++) {
            int yy, cb, cr;
            if (ncomp == 1) {
                yy = planes[0][y * comp_w[0] + x];
                cb = cr = 128;
            } else {
                unsigned cw1 = (width * (unsigned)comp_h[1] + (unsigned)hs - 1) / (unsigned)hs;
                unsigned ch1 = (height * (unsigned)comp_v[1] + (unsigned)vs - 1) / (unsigned)vs;
                unsigned cx = cw1 >= width ? x : x * cw1 / width;
                unsigned cy = ch1 >= height ? y : y * ch1 / height;
                if (cx >= comp_w[1])
                    cx = comp_w[1] - 1;
                if (cy >= comp_hpx[1])
                    cy = comp_hpx[1] - 1;
                yy = planes[0][y * comp_w[0] + x];
                cb = planes[1][cy * comp_w[1] + cx];
                cr = planes[2][cy * comp_w[1] + cx];
            }
            int rr = (int)(yy + 1.402 * (cr - 128) + 0.5);
            int gg = (int)(yy - 0.344136 * (cb - 128) - 0.714136 * (cr - 128) + 0.5);
            int bb = (int)(yy + 1.772 * (cb - 128) + 0.5);
            if (rr < 0)
                rr = 0;
            if (rr > 255)
                rr = 255;
            if (gg < 0)
                gg = 0;
            if (gg > 255)
                gg = 255;
            if (bb < 0)
                bb = 0;
            if (bb > 255)
                bb = 255;
            size_t o = ((size_t)y * width + x) * 4;
            out[o] = (uint8_t)rr;
            out[o + 1] = (uint8_t)gg;
            out[o + 2] = (uint8_t)bb;
            out[o + 3] = 255;
        }
    }
    for (int i = 0; i < ncomp; i++)
        free(planes[i]);
    *w = width;
    *h = height;
    *rgba = out;
    return 1;
}
