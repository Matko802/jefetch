#ifndef JEFETCH_LOGO_H
#define JEFETCH_LOGO_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;
    const char *const *aliases;
    size_t naliases;
    const char *color;
    const char *const *slots;
    size_t nslots;
    const char *color_keys;
    const char *color_title;
    const char *const *lines;
    size_t nlines;
} JfLogo;

extern const JfLogo JF_LOGOS[];
extern const size_t JF_LOGO_COUNT;

const JfLogo *logo_by_name(const char *name);
const char *logo_name_at(size_t i);

typedef enum {
    GFX_KITTY,
    GFX_SIXEL,
    GFX_ITERM2
} GraphicsProto;

int graphics_detect(GraphicsProto *out);
int graphics_kitty_response_ok(const uint8_t *buf, size_t n);
int graphics_sixel_response_ok(const uint8_t *buf, size_t n);
int graphics_parse_cpr(const uint8_t *buf, size_t n, unsigned *row, unsigned *col);
int graphics_parse_cell_size(const uint8_t *buf, size_t n, unsigned *w, unsigned *h);
int graphics_cursor_pos(unsigned *row, unsigned *col);
int graphics_cell_size(unsigned *w, unsigned *h);
char *graphics_kitty_transmit_seq(const uint8_t *rgba, unsigned w, unsigned h,
                                  unsigned id);
char *graphics_kitty_place_seq(unsigned id, unsigned cols, unsigned rows);
char *graphics_sixel_encode(const uint8_t *rgba, size_t w, size_t h);
char *graphics_iterm2_seq(const uint8_t *png, size_t pngn, const char *name,
                          unsigned cols, unsigned rows);

typedef struct {
    char *path;
    size_t cols;
    size_t rows;
    size_t gap;
    size_t pad_left;
    size_t pad_top;
    char **text;
    size_t ntext;
} NativeSpec;

int graphics_display_native(const NativeSpec *spec);

#endif
