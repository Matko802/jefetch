#ifndef JEFETCH_LOGO_IMAGE_H
#define JEFETCH_LOGO_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#define LOGO_ALPHA_CUT 128
#define LOGO_DEFAULT_COLS 48
#define LOGO_MAX_COLS 128
#define LOGO_MAX_ROWS 64

typedef struct {
    unsigned width;
    unsigned height;
    uint8_t *rgba;
} RawImage;

typedef struct {
    size_t cols;
    size_t rows;
    uint8_t *rgba;
} LogoImage;

typedef struct {
    char **lines;
    size_t nlines;
    char **colors;
    size_t ncolors;
    size_t width;
    size_t padding_right;
} ResolvedLogo;

void resolved_logo_free(ResolvedLogo *r);

void logo_expand_tilde(const char *path, char *out, size_t n);
int logo_looks_like_image(const char *path);
int logo_image_load(const char *path, RawImage *out, char *err, size_t errn);
void raw_image_free(RawImage *r);
void logo_target_cells(size_t src_w, size_t src_h, int has_w, unsigned cfg_w,
                       int has_h, unsigned cfg_h, size_t *cols, size_t *rows);
uint8_t *logo_resize_box(const uint8_t *src, size_t src_w, size_t src_h,
                         size_t dst_w, size_t dst_h);
LogoImage *logo_image_from_raw(const RawImage *raw, int has_w, unsigned cfg_w,
                               int has_h, unsigned cfg_h);
void logo_image_free(LogoImage *img);
ResolvedLogo *logo_image_to_resolved(const LogoImage *img, size_t padding_right);

#endif
