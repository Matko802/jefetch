#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "img_b64.h"

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void jf_b64_encode(const uint8_t *in, size_t n, char *out) {
    size_t i = 0, o = 0;
    while (i + 3 <= n) {
        unsigned v = ((unsigned)in[i] << 16) | ((unsigned)in[i + 1] << 8) | in[i + 2];
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = B64[(v >> 6) & 63];
        out[o++] = B64[v & 63];
        i += 3;
    }
    size_t rem = n - i;
    if (rem == 1) {
        unsigned v = (unsigned)in[i] << 16;
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = '=';
        out[o++] = '=';
    } else if (rem == 2) {
        unsigned v = ((unsigned)in[i] << 16) | ((unsigned)in[i + 1] << 8);
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = B64[(v >> 6) & 63];
        out[o++] = '=';
    }
    out[o] = 0;
}

size_t jf_b64_len(size_t n) {
    return ((n + 2) / 3) * 4 + 1;
}

static uint32_t crc_table[256];
static int crc_init = 0;

static void make_crc(void) {
    uint32_t c;
    int n, k;
    for (n = 0; n < 256; n++) {
        c = (uint32_t)n;
        for (k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
    crc_init = 1;
}

static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n) {
    if (!crc_init)
        make_crc();
    crc ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++)
        crc = crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

static void put32(uint8_t **o, size_t *len, uint32_t v) {
    (*o)[(*len)++] = (uint8_t)(v >> 24);
    (*o)[(*len)++] = (uint8_t)(v >> 16);
    (*o)[(*len)++] = (uint8_t)(v >> 8);
    (*o)[(*len)++] = (uint8_t)v;
}

static void put_chunk(uint8_t **o, size_t *len, const char *type, const uint8_t *d,
                      size_t n) {
    put32(o, len, (uint32_t)n);
    size_t tpos = *len;
    for (int i = 0; i < 4; i++)
        (*o)[(*len)++] = (uint8_t)type[i];
    memcpy(*o + *len, d, n);
    *len += n;
    uint32_t c = crc_update(0, (uint8_t *)(*o) + tpos, 4 + n);
    put32(o, len, c);
}

uint8_t *jf_png_encode(const uint8_t *rgba, unsigned w, unsigned h, size_t *n) {
    size_t rowbytes = (size_t)w * 4;
    size_t rawlen = ((size_t)h) * (rowbytes + 1);
    size_t nblocks = (rawlen + 65535 - 1) / 65535;
    size_t zlen = 2 + nblocks * 5 + rawlen + 4;
    size_t cap = 8 + 25 + zlen + 64 + 12;
    uint8_t *o = malloc(cap);
    size_t len = 0;
    if (!o)
        return NULL;
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    memcpy(o, sig, 8);
    len = 8;
    uint8_t ihdr[13];
    ihdr[0] = (uint8_t)(w >> 24);
    ihdr[1] = (uint8_t)(w >> 16);
    ihdr[2] = (uint8_t)(w >> 8);
    ihdr[3] = (uint8_t)w;
    ihdr[4] = (uint8_t)(h >> 24);
    ihdr[5] = (uint8_t)(h >> 16);
    ihdr[6] = (uint8_t)(h >> 8);
    ihdr[7] = (uint8_t)h;
    ihdr[8] = 8;
    ihdr[9] = 6;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    put_chunk(&o, &len, "IHDR", ihdr, 13);
    uint8_t *z = malloc(zlen);
    size_t zp = 0;
    z[zp++] = 0x78;
    z[zp++] = 0x01;
    size_t done = 0;
    uint32_t a = 1, b = 0;
    for (unsigned y = 0; y < h; y++) {
        const uint8_t *row = rgba + (size_t)y * rowbytes;
        size_t rpos = 0;
        uint8_t frow[70000];
        frow[0] = 0;
        memcpy(frow + 1, row, rowbytes);
        size_t rlen = rowbytes + 1;
        for (size_t i = 0; i < rlen; i++) {
            a = (a + frow[i]) % 65521;
            b = (b + a) % 65521;
        }
        while (rpos < rlen) {
            size_t blk = rlen - rpos;
            if (blk > 65535)
                blk = 65535;
            int last = (y + 1 == h && rpos + blk >= rlen) ? 1 : 0;
            z[zp++] = (uint8_t)last;
            z[zp++] = (uint8_t)blk;
            z[zp++] = (uint8_t)(blk >> 8);
            z[zp++] = (uint8_t)(~blk);
            z[zp++] = (uint8_t)(~blk >> 8);
            memcpy(z + zp, frow + rpos, blk);
            zp += blk;
            rpos += blk;
        }
        (void)done;
    }
    z[zp++] = (uint8_t)(b >> 8);
    z[zp++] = (uint8_t)b;
    z[zp++] = (uint8_t)(a >> 8);
    z[zp++] = (uint8_t)a;
    put_chunk(&o, &len, "IDAT", z, zp);
    free(z);
    put_chunk(&o, &len, "IEND", NULL, 0);
    *n = len;
    return o;
}
