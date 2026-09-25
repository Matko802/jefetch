#ifndef JEFETCH_IMG_B64_H
#define JEFETCH_IMG_B64_H

#include <stddef.h>
#include <stdint.h>

void jf_b64_encode(const uint8_t *in, size_t n, char *out);
size_t jf_b64_len(size_t n);
uint8_t *jf_png_encode(const uint8_t *rgba, unsigned w, unsigned h, size_t *n);

#endif
