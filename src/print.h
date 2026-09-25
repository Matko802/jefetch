#ifndef JEFETCH_PRINT_H
#define JEFETCH_PRINT_H

#include <stddef.h>

#include "config.h"

extern const char *JF_RESET;
extern const char *JF_SHARKVIS_PLACEHOLDER_START;

int jf_is_sharkvis_color_name(const char *s);
const char *jf_live_text_suffix(int text_live);

typedef struct {
    int is_ansi;
    char *start;
    char *end;
} ApplyResult;

void apply_result_free(ApplyResult *r);
ApplyResult jf_color_code_to_ansi(const char *color);
char *jf_named_color_sgr(const char *name);
char *jf_expand_dollar_code(const char *code);

typedef struct {
    char *(*get_placeholder)(void *ctx, const char *name);
    const char *(*key)(void *ctx);
    char *(*get_color)(void *ctx, const char *name);
    char *(*get_constant)(void *ctx, unsigned idx);
    void *ctx;
} JfResolver;

typedef struct {
    char *text;
    size_t length;
} JfFormatResult;

JfFormatResult jf_format(const char *fmt, const JfResolver *r);
void jf_format_free(JfFormatResult *fr);

size_t jf_visible_len(const char *s);
void jf_truncate_visible(const char *s, size_t max, char *out, size_t n);
void jf_strip_sgr(const char *s, char *out, size_t n);

typedef struct {
    const char *key;
    const char *value;
} JfPair;

char *jf_format_map(const char *fmt, const char *key, const JfPair *pairs, size_t n);

typedef struct {
    const char *key;
    const char **value_lines;
    size_t nlines;
    const DisplayConfig *display;
    const ModuleArgs *args;
    size_t pad_right;
} ModuleRender;

char **module_render_ansi_lines(const ModuleRender *r, size_t *n);
void module_render_free_lines(char **lines, size_t n);

#endif
