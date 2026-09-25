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
    int failed;
} BitReader;

static unsigned br_bits(BitReader *r, int count) {
    while (r->bits < count) {
        if (r->pos >= r->n) {
            r->failed = 1;
            return 0;
        }
        r->buf |= (unsigned)r->d[r->pos++] << r->bits;
        r->bits += 8;
    }
    unsigned v = r->buf & ((count == 32 ? 0xFFFFFFFFu : ((1u << count) - 1)));
    r->buf >>= count;
    r->bits -= count;
    return v;
}

typedef struct {
    uint16_t counts[16];
    uint16_t symbols[288];
} Huffman;

static int huff_build(Huffman *h, const uint8_t *lengths, int n) {
    uint16_t offs[16];
    memset(h->counts, 0, sizeof h->counts);
    for (int i = 0; i < n; i++) {
        if (lengths[i] >= 16)
            return 0;
        h->counts[lengths[i]]++;
    }
    h->counts[0] = 0;
    offs[0] = 0;
    offs[1] = 0;
    for (int len = 1; len < 15; len++)
        offs[len + 1] = (uint16_t)(offs[len] + h->counts[len]);
    for (int i = 0; i < n; i++) {
        if (lengths[i] != 0)
            h->symbols[offs[lengths[i]]++] = (uint16_t)i;
    }
    return 1;
}

static int huff_decode(BitReader *r, const Huffman *h) {
    unsigned code = 0;
    unsigned first = 0;
    unsigned index = 0;
    for (int len = 1; len < 16; len++) {
        if (r->pos >= r->n && r->bits == 0) {
            r->failed = 1;
            return -1;
        }
        while (r->bits == 0) {
            if (r->pos >= r->n) {
                r->failed = 1;
                return -1;
            }
            r->buf |= (unsigned)r->d[r->pos++] << r->bits;
            r->bits += 8;
        }
        code |= (r->buf & 1);
        r->buf >>= 1;
        r->bits--;
        unsigned count = h->counts[len];
        if (code - first < count)
            return h->symbols[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    r->failed = 1;
    return -1;
}

static const uint8_t FIXED_LITLEN[288] = {0};
static const uint8_t FIXED_DIST[32] = {0};

static void fixed_tables(Huffman *lit, Huffman *dist) {
    static uint8_t ll[288], dd[32];
    static int init = 0;
    if (!init) {
        for (int i = 0; i < 288; i++)
            ll[i] = i < 144 ? 8 : (i < 256 ? 9 : (i < 280 ? 7 : 8));
        for (int i = 0; i < 32; i++)
            dd[i] = 5;
        init = 1;
    }
    huff_build(lit, ll, 288);
    huff_build(dist, dd, 32);
    (void)FIXED_LITLEN;
    (void)FIXED_DIST;
}

static const unsigned LEN_BASE[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const unsigned LEN_EXTRA[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const unsigned DIST_BASE[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};
static const unsigned DIST_EXTRA[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static int ogrow(uint8_t **out, size_t *pos, size_t *cap, size_t need) {
    if (*pos + need <= *cap)
        return 1;
    size_t c = *cap ? *cap : 256;
    while (c < *pos + need)
        c *= 2;
    uint8_t *p = realloc(*out, c);
    if (!p)
        return 0;
    *out = p;
    *cap = c;
    return 1;
}

static int inflate_block(BitReader *r, Huffman *lit, Huffman *dist, uint8_t **out,
                         size_t *pos, size_t *cap) {
    for (;;) {
        int sym = huff_decode(r, lit);
        if (sym < 0)
            return 0;
        if (sym < 256) {
            if (!ogrow(out, pos, cap, 1))
                return 0;
            (*out)[(*pos)++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 1;
        } else {
            int li = sym - 257;
            if (li < 0 || li > 28)
                return 0;
            unsigned len = LEN_BASE[li] + br_bits(r, LEN_EXTRA[li]);
            int dsym = huff_decode(r, dist);
            if (dsym < 0 || dsym > 29)
                return 0;
            unsigned distv = DIST_BASE[dsym] + br_bits(r, DIST_EXTRA[dsym]);
            if (distv == 0 || distv > *pos)
                return 0;
            if (!ogrow(out, pos, cap, len))
                return 0;
            for (unsigned i = 0; i < len; i++) {
                (*out)[*pos] = (*out)[*pos - distv];
                (*pos)++;
            }
        }
        if (r->failed)
            return 0;
    }
}

int img_inflate(const uint8_t *in, size_t n, uint8_t **out, size_t *outlen) {
    size_t cap = n * 3 + 64;
    uint8_t *o = malloc(cap);
    size_t pos = 0;
    BitReader r = {in, n, 0, 0, 0, 0};
    int final = 0;
    if (!o)
        return 0;
    while (!final) {
        if (r.pos >= r.n && r.bits < 3) {
            free(o);
            return 0;
        }
        final = (int)br_bits(&r, 1);
        int type = (int)br_bits(&r, 2);
        if (r.failed) {
            free(o);
            return 0;
        }
        if (type == 0) {
            r.buf = 0;
            r.bits = 0;
            if (r.pos + 4 > r.n) {
                free(o);
                return 0;
            }
            unsigned len = r.d[r.pos] | ((unsigned)r.d[r.pos + 1] << 8);
            unsigned nlen = r.d[r.pos + 2] | ((unsigned)r.d[r.pos + 3] << 8);
            r.pos += 4;
            if ((len ^ nlen) != 0xFFFF || r.pos + len > r.n) {
                free(o);
                return 0;
            }
            if (!ogrow(&o, &pos, &cap, len)) {
                free(o);
                return 0;
            }
            memcpy(o + pos, r.d + r.pos, len);
            r.pos += len;
            pos += len;
        } else if (type == 1) {
            Huffman lit, dist;
            fixed_tables(&lit, &dist);
            if (!inflate_block(&r, &lit, &dist, &o, &pos, &cap)) {
                free(o);
                return 0;
            }
        } else if (type == 2) {
            unsigned hlit = br_bits(&r, 5) + 257;
            unsigned hdist = br_bits(&r, 5) + 1;
            unsigned hclen = br_bits(&r, 4) + 4;
            if (r.failed || hlit > 288 || hdist > 32) {
                free(o);
                return 0;
            }
            static const unsigned order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5,
                                               11, 4, 12, 3, 13, 2, 14, 1, 15};
            uint8_t cl_lens[19] = {0};
            for (unsigned i = 0; i < hclen; i++)
                cl_lens[order[i]] = (uint8_t)br_bits(&r, 3);
            Huffman cl;
            if (!huff_build(&cl, cl_lens, 19)) {
                free(o);
                return 0;
            }
            uint8_t lens[330];
            memset(lens, 0, sizeof lens);
            unsigned total = hlit + hdist;
            unsigned i = 0;
            while (i < total) {
                int sym = huff_decode(&r, &cl);
                if (sym < 0) {
                    free(o);
                    return 0;
                }
                if (sym < 16) {
                    lens[i++] = (uint8_t)sym;
                } else if (sym == 16) {
                    unsigned rep = br_bits(&r, 2) + 3;
                    if (i == 0) {
                        free(o);
                        return 0;
                    }
                    uint8_t prev = lens[i - 1];
                    while (rep-- > 0 && i < total)
                        lens[i++] = prev;
                } else if (sym == 17) {
                    unsigned rep = br_bits(&r, 3) + 3;
                    while (rep-- > 0 && i < total)
                        lens[i++] = 0;
                } else {
                    unsigned rep = br_bits(&r, 7) + 11;
                    while (rep-- > 0 && i < total)
                        lens[i++] = 0;
                }
            }
            Huffman lit, dist;
            if (!huff_build(&lit, lens, (int)hlit) ||
                !huff_build(&dist, lens + hlit, (int)hdist)) {
                free(o);
                return 0;
            }
            if (!inflate_block(&r, &lit, &dist, &o, &pos, &cap)) {
                free(o);
                return 0;
            }
        } else {
            free(o);
            return 0;
        }
        if (r.failed) {
            free(o);
            return 0;
        }
        while (pos + 300 > cap) {
            cap *= 2;
            o = realloc(o, cap);
        }
    }
    *out = o;
    *outlen = pos;
    return 1;
}

int img_unzlib(const uint8_t *in, size_t n, uint8_t **out, size_t *outlen) {
    if (n < 6)
        return 0;
    if ((in[0] & 0x0F) != 8)
        return 0;
    if (((in[0] << 8) | in[1]) % 31 != 0)
        return 0;
    if (in[1] & 0x20)
        return 0;
    uint8_t *raw = NULL;
    size_t rawlen = 0;
    if (!img_inflate(in + 2, n - 6, &raw, &rawlen))
        return 0;
    uint32_t adler = ((uint32_t)in[n - 4] << 24) | ((uint32_t)in[n - 3] << 16) |
                     ((uint32_t)in[n - 2] << 8) | in[n - 1];
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < rawlen; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    if ((b << 16 | a) != adler) {
        free(raw);
        return 0;
    }
    *out = raw;
    *outlen = rawlen;
    return 1;
}
