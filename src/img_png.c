#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "img.h"

static uint32_t rd32be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc)
        return a;
    if (pb <= pc)
        return b;
    return c;
}

static void unfilter_row(uint8_t *row, const uint8_t *prev, size_t len, int bpp, int filter) {
    size_t i;
    switch (filter) {
    case 0:
        break;
    case 1:
        for (i = (size_t)bpp; i < len; i++)
            row[i] = (uint8_t)(row[i] + row[i - (size_t)bpp]);
        break;
    case 2:
        if (prev) {
            for (i = 0; i < len; i++)
                row[i] = (uint8_t)(row[i] + prev[i]);
        }
        break;
    case 3:
        for (i = 0; i < len; i++) {
            int a = i >= (size_t)bpp ? row[i - bpp] : 0;
            int b = prev ? prev[i] : 0;
            row[i] = (uint8_t)(row[i] + ((a + b) >> 1));
        }
        break;
    case 4:
        for (i = 0; i < len; i++) {
            int a = i >= (size_t)bpp ? row[i - bpp] : 0;
            int b = prev ? prev[i] : 0;
            int c = (i >= (size_t)bpp && prev) ? prev[i - bpp] : 0;
            row[i] = (uint8_t)(row[i] + paeth(a, b, c));
        }
        break;
    default:
        break;
    }
}

static void put_pixel(uint8_t *out, unsigned w, unsigned h, unsigned x, unsigned y,
                      uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (x >= w || y >= h)
        return;
    size_t o = ((size_t)y * w + x) * 4;
    out[o] = r;
    out[o + 1] = g;
    out[o + 2] = b;
    out[o + 3] = a;
}

static int px_val(const uint8_t *row, int depth, unsigned x, int *v) {
    if (depth == 8) {
        *v = row[x];
        return 1;
    }
    if (depth == 16) {
        *v = row[x * 2];
        return 1;
    }
    if (depth == 4) {
        *v = (x & 1) ? row[x / 2] & 0xF : (row[x / 2] >> 4) & 0xF;
        *v = (*v << 4) | *v;
        return 1;
    }
    if (depth == 2) {
        int s = 6 - (x % 4) * 2;
        *v = (row[x / 4] >> s) & 3;
        *v = (*v << 6) | (*v << 4) | (*v << 2) | *v;
        return 1;
    }
    if (depth == 1) {
        *v = (row[x / 8] >> (7 - (x % 8))) & 1;
        *v = *v ? 255 : 0;
        return 1;
    }
    return 0;
}

int img_decode_png(const uint8_t *d, size_t n, unsigned *w, unsigned *h,
                   uint8_t **rgba, char *err, size_t errn) {
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (n < 8 || memcmp(d, sig, 8) != 0) {
        snprintf(err, errn, "not a png");
        return 0;
    }
    size_t pos = 8;
    unsigned width = 0, height = 0;
    int depth = 0, ctype = -1, interlace = 0;
    uint8_t *idat = NULL;
    size_t idat_len = 0, idat_cap = 0;
    uint8_t palette[256][3];
    uint8_t pal_alpha[256];
    int npal = 0, has_trns = 0;
    uint16_t trns_gray = 0, trns_r = 0, trns_g = 0, trns_b = 0;
    int trns_kind = -1;
    memset(pal_alpha, 255, sizeof pal_alpha);
    int have_ihdr = 0;
    while (pos + 8 <= n) {
        uint32_t len = rd32be(d + pos);
        const uint8_t *type = d + pos + 4;
        pos += 8;
        if (pos + len + 4 > n) {
            free(idat);
            snprintf(err, errn, "png truncated");
            return 0;
        }
        if (!memcmp(type, "IHDR", 4)) {
            if (len != 13 || have_ihdr) {
                free(idat);
                snprintf(err, errn, "bad png ihdr");
                return 0;
            }
            width = rd32be(d + pos);
            height = rd32be(d + pos + 4);
            depth = d[pos + 8];
            ctype = d[pos + 9];
            interlace = d[pos + 12];
            if (width == 0 || height == 0 || width > 16384 || height > 16384 ||
                d[pos + 10] != 0 || d[pos + 11] != 0) {
                free(idat);
                snprintf(err, errn, "bad png ihdr");
                return 0;
            }
            have_ihdr = 1;
        } else if (!memcmp(type, "PLTE", 4)) {
            npal = (int)(len / 3);
            if (npal > 256)
                npal = 256;
            for (int i = 0; i < npal; i++) {
                palette[i][0] = d[pos + (size_t)i * 3];
                palette[i][1] = d[pos + (size_t)i * 3 + 1];
                palette[i][2] = d[pos + (size_t)i * 3 + 2];
            }
        } else if (!memcmp(type, "tRNS", 4)) {
            has_trns = 1;
            if (ctype == 3) {
                for (size_t i = 0; i < len && i < 256; i++)
                    pal_alpha[i] = d[pos + i];
            } else if (ctype == 0 && len >= 2) {
                trns_kind = 0;
                trns_gray = (uint16_t)((d[pos] << 8) | d[pos + 1]);
            } else if (ctype == 2 && len >= 6) {
                trns_kind = 2;
                trns_r = (uint16_t)((d[pos] << 8) | d[pos + 1]);
                trns_g = (uint16_t)((d[pos + 2] << 8) | d[pos + 3]);
                trns_b = (uint16_t)((d[pos + 4] << 8) | d[pos + 5]);
            }
        } else if (!memcmp(type, "IDAT", 4)) {
            if (len > 64 * 1024 * 1024 || idat_len + len > 64 * 1024 * 1024) {
                free(idat);
                snprintf(err, errn, "png too large");
                return 0;
            }
            if (idat_len + len > idat_cap) {
                size_t c = idat_cap ? idat_cap : 65536;
                while (c < idat_len + len) {
                    c *= 2;
                    if (c > 64 * 1024 * 1024) {
                        c = 64 * 1024 * 1024;
                        break;
                    }
                }
                uint8_t *nd = realloc(idat, c);
                if (!nd) {
                    free(idat);
                    snprintf(err, errn, "out of memory");
                    return 0;
                }
                idat = nd;
                idat_cap = c;
            }
            memcpy(idat + idat_len, d + pos, len);
            idat_len += len;
        } else if (!memcmp(type, "IEND", 4)) {
            pos += len + 4;
            break;
        }
        pos += len + 4;
    }
    if (!have_ihdr) {
        free(idat);
        snprintf(err, errn, "png without ihdr");
        return 0;
    }
    int channels;
    if (ctype == 0)
        channels = 1;
    else if (ctype == 2)
        channels = 3;
    else if (ctype == 3)
        channels = 1;
    else if (ctype == 4)
        channels = 2;
    else if (ctype == 6)
        channels = 4;
    else {
        free(idat);
        snprintf(err, errn, "unsupported png color type");
        return 0;
    }
    if (ctype == 3) {
        if (depth != 1 && depth != 2 && depth != 4 && depth != 8) {
            free(idat);
            snprintf(err, errn, "bad png depth");
            return 0;
        }
    } else if (ctype == 0 || ctype == 4) {
        if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16) {
            free(idat);
            snprintf(err, errn, "bad png depth");
            return 0;
        }
    } else if (depth != 8 && depth != 16) {
        free(idat);
        snprintf(err, errn, "bad png depth");
        return 0;
    }
    uint8_t *raw = NULL;
    size_t rawlen = 0;
    if (!img_unzlib(idat, idat_len, &raw, &rawlen)) {
        free(idat);
        snprintf(err, errn, "png zlib error");
        return 0;
    }
    free(idat);
    uint8_t *out = malloc((size_t)width * height * 4);
    if (!out) {
        free(raw);
        return 0;
    }
    for (size_t i = 0; i < (size_t)width * height; i++) {
        out[i * 4 + 3] = 0;
    }
    size_t rp = 0;
    int ok = 1;
    size_t rowbits = (size_t)width * channels * (size_t)depth;
    size_t rowbytes = (rowbits + 7) / 8;
    int bpp = (int)((channels * (size_t)depth + 7) / 8);
    if (bpp < 1)
        bpp = 1;
    static const int AX[7] = {0, 4, 0, 2, 0, 1, 0};
    static const int AY[7] = {0, 0, 4, 0, 2, 0, 1};
    static const int DX[7] = {8, 8, 4, 4, 2, 2, 1};
    static const int DY[7] = {8, 8, 8, 4, 4, 2, 2};
    int npasses = interlace ? 7 : 1;
    uint8_t *prev = calloc(rowbytes + 8, 1);
    for (int pass = 0; pass < npasses && ok; pass++) {
        unsigned pw, ph;
        if (interlace) {
            pw = (width - (unsigned)AX[pass] + (unsigned)DX[pass] - 1) / (unsigned)DX[pass];
            ph = (height - (unsigned)AY[pass] + (unsigned)DY[pass] - 1) / (unsigned)DY[pass];
            if (pw == 0 || ph == 0)
                continue;
        } else {
            pw = width;
            ph = height;
        }
        size_t prowbits = (size_t)pw * channels * (size_t)depth;
        size_t prowbytes = (prowbits + 7) / 8;
        int pbpp = (int)((channels * (size_t)depth + 7) / 8);
        if (pbpp < 1)
            pbpp = 1;
        free(prev);
        prev = calloc(prowbytes + 8, 1);
        if (!prev) {
            ok = 0;
            break;
        }
        uint8_t *row = malloc(prowbytes + 8);
        if (!row) {
            ok = 0;
            break;
        }
        for (unsigned y = 0; y < ph && ok; y++) {
            if (rp + 1 + prowbytes > rawlen) {
                ok = 0;
                break;
            }
            int filter = raw[rp++];
            if (filter < 0 || filter > 4) {
                ok = 0;
                break;
            }
            memcpy(row, raw + rp, prowbytes);
            rp += prowbytes;
            unfilter_row(row, prev, prowbytes, pbpp, filter);
            memcpy(prev, row, prowbytes);
            for (unsigned x = 0; x < pw; x++) {
                int c[4] = {0, 0, 0, 255};
                if (ctype == 3) {
                    int v = 0;
                    px_val(row, depth, x, &v);
                    if (v >= npal)
                        v = 0;
                    c[0] = palette[v][0];
                    c[1] = palette[v][1];
                    c[2] = palette[v][2];
                    c[3] = pal_alpha[v];
                } else if (ctype == 0) {
                    int v = 0;
                    px_val(row, depth, x, &v);
                    c[0] = c[1] = c[2] = v;
                    if (has_trns && trns_kind == 0 && v == (trns_gray >> (depth > 8 ? 8 : 0)))
                        c[3] = 0;
                    else
                        c[3] = 255;
                } else if (ctype == 4) {
                    int v = 0, a = 255;
                    if (depth == 8) {
                        v = row[x * 2];
                        a = row[x * 2 + 1];
                    } else {
                        v = row[x * 4];
                        a = row[x * 4 + 1];
                    }
                    c[0] = c[1] = c[2] = v;
                    c[3] = a;
                } else if (ctype == 2) {
                    if (depth == 8) {
                        c[0] = row[x * 3];
                        c[1] = row[x * 3 + 1];
                        c[2] = row[x * 3 + 2];
                    } else {
                        c[0] = row[x * 6];
                        c[1] = row[x * 6 + 2];
                        c[2] = row[x * 6 + 4];
                    }
                    c[3] = 255;
                    if (has_trns && trns_kind == 2) {
                        int rr = depth == 8 ? row[x * 3] : row[x * 6];
                        int gg = depth == 8 ? row[x * 3 + 1] : row[x * 6 + 2];
                        int bb = depth == 8 ? row[x * 3 + 2] : row[x * 6 + 4];
                        int sh = depth > 8 ? 8 : 0;
                        if (rr == (trns_r >> sh) && gg == (trns_g >> sh) && bb == (trns_b >> sh))
                            c[3] = 0;
                    }
                } else {
                    if (depth == 8) {
                        c[0] = row[x * 4];
                        c[1] = row[x * 4 + 1];
                        c[2] = row[x * 4 + 2];
                        c[3] = row[x * 4 + 3];
                    } else {
                        c[0] = row[x * 8];
                        c[1] = row[x * 8 + 2];
                        c[2] = row[x * 8 + 4];
                        c[3] = row[x * 8 + 6];
                    }
                }
                unsigned dx, dy;
                if (interlace) {
                    dx = (unsigned)AX[pass] + x * (unsigned)DX[pass];
                    dy = (unsigned)AY[pass] + y * (unsigned)DY[pass];
                } else {
                    dx = x;
                    dy = y;
                }
                put_pixel(out, width, height, dx, dy, (uint8_t)c[0], (uint8_t)c[1],
                          (uint8_t)c[2], (uint8_t)c[3]);
            }
        }
        free(row);
    }
    free(prev);
    free(raw);
    if (!ok) {
        free(out);
        snprintf(err, errn, "png data error");
        return 0;
    }
    *w = width;
    *h = height;
    *rgba = out;
    return 1;
}
