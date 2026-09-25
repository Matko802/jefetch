#ifndef JEFETCH_COMMON_H
#define JEFETCH_COMMON_H

#include <stddef.h>
#include <stdint.h>

void jf_format_bytes(unsigned long long bytes, char *out, size_t n);
void jf_format_uptime(unsigned long long secs, char *out, size_t n);
int jf_percent(unsigned long long used, unsigned long long total, unsigned *out);
void jf_percent_bar(unsigned long long used, unsigned long long total, char *out, size_t n);
void jf_truncate_to_width(const char *s, size_t width, int ellipsis, char *out, size_t n);
void jf_terminal_size(size_t *cols, size_t *rows);
int jf_colors_enabled(void);
int jf_utf8_supported(void);

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} JfBuf;

void jf_buf_put(JfBuf *b, const char *s);
void jf_buf_putn(JfBuf *b, const char *s, size_t n);
void jf_buf_putc(JfBuf *b, char c);
void jf_buf_free(JfBuf *b);

uint64_t jf_now_ms(void);

#endif
