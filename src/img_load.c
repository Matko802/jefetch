#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "img.h"

int img_decode_png(const uint8_t *d, size_t n, unsigned *w, unsigned *h,
                   uint8_t **rgba, char *err, size_t errn);
int img_decode_jpg(const uint8_t *d, size_t n, unsigned *w, unsigned *h,
                   uint8_t **rgba, char *err, size_t errn);
int img_decode_bmp(const uint8_t *d, size_t n, unsigned *w, unsigned *h,
                   uint8_t **rgba, char *err, size_t errn);
int img_decode_gif(const uint8_t *d, size_t n, unsigned *w, unsigned *h,
                   uint8_t **rgba, char *err, size_t errn);

void img_free(uint8_t *rgba) {
    free(rgba);
}

int img_load(const char *path, unsigned *w, unsigned *h, uint8_t **rgba,
             char *err, size_t errn) {
    const char *p = path;
    if (p[0] == '~' && p[1] == '/') {
        static char full[2048];
        const char *home = getenv("HOME");
        if (home) {
            snprintf(full, sizeof full, "%s/%s", home, p + 2);
            p = full;
        }
    }
    FILE *f = fopen(p, "rb");
    if (!f) {
        snprintf(err, errn, "cannot open image '%s'", path);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 64 * 1024 * 1024) {
        fclose(f);
        snprintf(err, errn, "cannot sniff image '%s'", path);
        return 0;
    }
    uint8_t *d = malloc((size_t)sz);
    if (!d) {
        fclose(f);
        return 0;
    }
    if (fread(d, 1, (size_t)sz, f) != (size_t)sz) {
        free(d);
        fclose(f);
        snprintf(err, errn, "cannot sniff image '%s'", path);
        return 0;
    }
    fclose(f);
    int ok = 0;
    if (sz >= 8 && !memcmp(d, "\x89PNG\r\n\x1a\n", 8))
        ok = img_decode_png(d, (size_t)sz, w, h, rgba, err, errn);
    else if (sz >= 2 && d[0] == 0xFF && d[1] == 0xD8)
        ok = img_decode_jpg(d, (size_t)sz, w, h, rgba, err, errn);
    else if (sz >= 6 && (!memcmp(d, "GIF87a", 6) || !memcmp(d, "GIF89a", 6)))
        ok = img_decode_gif(d, (size_t)sz, w, h, rgba, err, errn);
    else if (sz >= 2 && d[0] == 'B' && d[1] == 'M')
        ok = img_decode_bmp(d, (size_t)sz, w, h, rgba, err, errn);
    else
        snprintf(err, errn, "cannot decode image '%s'", path);
    free(d);
    if (!ok)
        return 0;
    if (*w == 0 || *h == 0) {
        free(*rgba);
        *rgba = NULL;
        snprintf(err, errn, "image '%s' is empty", path);
        return 0;
    }
    return 1;
}
