#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "img.h"
#include "logo_image.h"

void resolved_logo_free(ResolvedLogo *r) {
    size_t i;
    if (!r)
        return;
    for (i = 0; i < r->nlines; i++)
        free(r->lines[i]);
    free(r->lines);
    for (i = 0; i < r->ncolors; i++)
        free(r->colors[i]);
    free(r->colors);
    free(r);
}

void logo_expand_tilde(const char *path, char *out, size_t n) {
    if (path[0] == '~' && path[1] == '/') {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(out, n, "%s/%s", home, path + 2);
            return;
        }
    }
    snprintf(out, n, "%s", path);
}

int logo_looks_like_image(const char *path) {
    char lower[1024];
    size_t i = 0;
    while (path[i] && i + 1 < sizeof lower) {
        char c = path[i];
        lower[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    lower[i] = 0;
    const char *base = strrchr(lower, '/');
    base = base ? base + 1 : lower;
    const char *ext = strrchr(base, '.');
    if (!ext)
        return 0;
    ext++;
    return !strcmp(ext, "png") || !strcmp(ext, "jpg") || !strcmp(ext, "jpeg") ||
           !strcmp(ext, "gif") || !strcmp(ext, "bmp");
}

int logo_image_load(const char *path, RawImage *out, char *err, size_t errn) {
    char expanded[2048];
    logo_expand_tilde(path, expanded, sizeof expanded);
    uint8_t *rgba = NULL;
    unsigned w = 0, h = 0;
    if (!img_load(expanded, &w, &h, &rgba, err, errn))
        return 0;
    out->width = w;
    out->height = h;
    out->rgba = rgba;
    return 1;
}

void raw_image_free(RawImage *r) {
    free(r->rgba);
    r->rgba = NULL;
}

void logo_target_cells(size_t src_w, size_t src_h, int has_w, unsigned cfg_w,
                       int has_h, unsigned cfg_h, size_t *cols, size_t *rows) {
    double aspect = (double)(src_w > 1 ? src_w : 1) / (double)(src_h > 1 ? src_h : 1);
    size_t c, r;
    if (has_w && has_h) {
        c = cfg_w;
        r = cfg_h;
    } else if (has_w) {
        c = cfg_w;
        r = (size_t)round((double)c / aspect / 2.0);
        if (r < 1)
            r = 1;
    } else if (has_h) {
        r = cfg_h;
        c = (size_t)round((double)r * aspect * 2.0);
        if (c < 1)
            c = 1;
    } else {
        c = src_w < LOGO_DEFAULT_COLS ? src_w : LOGO_DEFAULT_COLS;
        if (c < 1)
            c = 1;
        r = (size_t)round((double)c / aspect / 2.0);
        if (r < 1)
            r = 1;
    }
    if (c < 1)
        c = 1;
    if (c > LOGO_MAX_COLS)
        c = LOGO_MAX_COLS;
    if (r < 1)
        r = 1;
    if (r > LOGO_MAX_ROWS)
        r = LOGO_MAX_ROWS;
    *cols = c;
    *rows = r;
}

uint8_t *logo_resize_box(const uint8_t *src, size_t src_w, size_t src_h,
                         size_t dst_w, size_t dst_h) {
    size_t total = dst_w * dst_h * 4;
    if (total == 0)
        total = 1;
    uint8_t *out = calloc(total, 1);
    if (!out)
        return NULL;
    if (src_w == 0 || src_h == 0 || dst_w == 0 || dst_h == 0)
        return out;
    double x_scale = (double)src_w / (double)dst_w;
    double y_scale = (double)src_h / (double)dst_h;
    for (size_t dy = 0; dy < dst_h; dy++) {
        double y0 = (double)dy * y_scale;
        double y1 = y0 + y_scale;
        if (y1 > (double)src_h)
            y1 = (double)src_h;
        for (size_t dx = 0; dx < dst_w; dx++) {
            double x0 = (double)dx * x_scale;
            double x1 = x0 + x_scale;
            if (x1 > (double)src_w)
                x1 = (double)src_w;
            double rs = 0, gs = 0, bs = 0, asum = 0, area = 0;
            size_t sy0 = (size_t)y0;
            size_t sy1 = (size_t)ceil(y1);
            if (sy1 > src_h)
                sy1 = src_h;
            size_t sx0 = (size_t)x0;
            size_t sx1 = (size_t)ceil(x1);
            if (sx1 > src_w)
                sx1 = src_w;
            for (size_t sy = sy0; sy < sy1; sy++) {
                double oy0 = y0 > (double)sy ? y0 : (double)sy;
                double oy1 = y1 < (double)sy + 1.0 ? y1 : (double)sy + 1.0;
                if (oy1 <= oy0)
                    continue;
                for (size_t sx = sx0; sx < sx1; sx++) {
                    double ox0 = x0 > (double)sx ? x0 : (double)sx;
                    double ox1 = x1 < (double)sx + 1.0 ? x1 : (double)sx + 1.0;
                    if (ox1 <= ox0)
                        continue;
                    double wgt = (ox1 - ox0) * (oy1 - oy0);
                    size_t p = (sy * src_w + sx) * 4;
                    double a = src[p + 3] / 255.0;
                    rs += src[p] * a * wgt;
                    gs += src[p + 1] * a * wgt;
                    bs += src[p + 2] * a * wgt;
                    asum += a * wgt;
                    area += wgt;
                }
            }
            size_t o = (dy * dst_w + dx) * 4;
            if (asum > 0.0) {
                double v;
                v = round(rs / asum);
                out[o] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
                v = round(gs / asum);
                out[o + 1] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
                v = round(bs / asum);
                out[o + 2] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
                double aa = area > 1e-9 ? area : 1e-9;
                v = round(asum / aa * 255.0);
                out[o + 3] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
            }
        }
    }
    return out;
}

LogoImage *logo_image_from_raw(const RawImage *raw, int has_w, unsigned cfg_w,
                               int has_h, unsigned cfg_h) {
    size_t cols, rows;
    logo_target_cells(raw->width, raw->height, has_w, cfg_w, has_h, cfg_h, &cols,
                      &rows);
    uint8_t *rgba = logo_resize_box(raw->rgba, raw->width, raw->height, cols, rows * 2);
    if (!rgba)
        return NULL;
    LogoImage *img = malloc(sizeof *img);
    if (!img) {
        free(rgba);
        return NULL;
    }
    img->cols = cols;
    img->rows = rows;
    img->rgba = rgba;
    return img;
}

void logo_image_free(LogoImage *img) {
    if (!img)
        return;
    free(img->rgba);
    free(img);
}

ResolvedLogo *logo_image_to_resolved(const LogoImage *img, size_t padding_right) {
    ResolvedLogo *r = calloc(1, sizeof *r);
    if (!r)
        return NULL;
    r->padding_right = padding_right;
    r->lines = calloc(img->rows ? img->rows : 1, sizeof(char *));
    r->colors = calloc(img->rows ? img->rows : 1, sizeof(char *));
    for (size_t rr = 0; rr < img->rows; rr++) {
        size_t last = img->cols;
        while (last > 0) {
            size_t p0 = (((rr * 2) * img->cols + (last - 1)) * 4) + 3;
            size_t p1 = (((rr * 2 + 1) * img->cols + (last - 1)) * 4) + 3;
            if (img->rgba[p0] > LOGO_ALPHA_CUT || img->rgba[p1] > LOGO_ALPHA_CUT)
                break;
            last--;
        }
        size_t cap = 128;
        char *line = malloc(cap);
        size_t len = 0;
        for (size_t c = 0; c < last; c++) {
            size_t p0 = (((rr * 2) * img->cols + c) * 4);
            size_t p1 = (((rr * 2 + 1) * img->cols + c) * 4);
            uint8_t r0 = img->rgba[p0], g0 = img->rgba[p0 + 1], b0 = img->rgba[p0 + 2],
                    a0 = img->rgba[p0 + 3];
            uint8_t r1 = img->rgba[p1], g1 = img->rgba[p1 + 1], b1 = img->rgba[p1 + 2],
                    a1 = img->rgba[p1 + 3];
            int top = a0 > LOGO_ALPHA_CUT;
            int bot = a1 > LOGO_ALPHA_CUT;
            char tmp[64];
            if (!top && !bot) {
                snprintf(tmp, sizeof tmp, " ");
            } else if (top && !bot) {
                snprintf(tmp, sizeof tmp, "\x1b[38;2;%u;%u;%um\xe2\x96\x80", r0, g0, b0);
            } else if (!top && bot) {
                snprintf(tmp, sizeof tmp, "\x1b[38;2;%u;%u;%um\xe2\x96\x84", r1, g1, b1);
            } else {
                snprintf(tmp, sizeof tmp, "\x1b[38;2;%u;%u;%um\x1b[48;2;%u;%u;%um\xe2\x96\x80",
                         r0, g0, b0, r1, g1, b1);
            }
            size_t tl = strlen(tmp);
            while (len + tl + 5 > cap) {
                cap *= 2;
                line = realloc(line, cap);
            }
            memcpy(line + len, tmp, tl);
            len += tl;
        }
        if (last > 0) {
            memcpy(line + len, "\x1b[0m", 4);
            len += 4;
        }
        line[len] = 0;
        r->lines[rr] = line;
        r->colors[rr] = strdup("");
        r->nlines++;
        r->ncolors++;
        if (last > r->width)
            r->width = last;
    }
    return r;
}
