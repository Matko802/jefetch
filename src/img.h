#ifndef JEFETCH_IMG_H
#define JEFETCH_IMG_H

#include <stddef.h>
#include <stdint.h>

int img_load(const char *path, unsigned *w, unsigned *h, uint8_t **rgba,
             char *err, size_t errn);
void img_free(uint8_t *rgba);

int img_inflate(const uint8_t *in, size_t n, uint8_t **out, size_t *outlen);
int img_unzlib(const uint8_t *in, size_t n, uint8_t **out, size_t *outlen);

#endif
