#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "detect.h"

static void cstr_of(const uint8_t *b, size_t n, char *out, size_t outn) {
    size_t i = 0;
    while (i < n && b[i])
        i++;
    if (i >= outn)
        i = outn - 1;
    memcpy(out, b, i);
    out[i] = 0;
}

LoggedUser *detect_users(size_t *n) {
    static const char *paths[] = {"/run/utmp", "/var/run/utmp", "/var/adm/utmpx"};
    static const size_t strides[] = {384, 400, 380};
    const char *path = NULL;
    *n = 0;
    for (int i = 0; i < 3; i++) {
        FILE *t = fopen(paths[i], "r");
        if (t) {
            fclose(t);
            path = paths[i];
            break;
        }
    }
    if (!path)
        return NULL;
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(f);
        return NULL;
    }
    uint8_t *buf = malloc((size_t)sz);
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    LoggedUser *best = NULL;
    size_t best_n = 0;
    for (int s = 0; s < 3; s++) {
        size_t stride = strides[s];
        LoggedUser *out = NULL;
        size_t m = 0;
        size_t off = 0;
        while (off + stride <= (size_t)sz) {
            const uint8_t *e = buf + off;
            int16_t type = (int16_t)(e[0] | (e[1] << 8));
            if (type == 7) {
                char user[64], line[64], host[256];
                cstr_of(e + 44, 32, user, sizeof user);
                cstr_of(e + 8, 32, line, sizeof line);
                cstr_of(e + 76, 256, host, sizeof host);
                if (user[0]) {
                    out = realloc(out, (m + 1) * sizeof(LoggedUser));
                    snprintf(out[m].user, sizeof out[m].user, "%s", user);
                    snprintf(out[m].tty, sizeof out[m].tty, "%s", line);
                    snprintf(out[m].host, sizeof out[m].host, "%s", host);
                    m++;
                }
            }
            off += stride;
        }
        if (m > best_n) {
            free(best);
            best = out;
            best_n = m;
        } else {
            free(out);
        }
    }
    free(buf);
    *n = best_n;
    return best;
}

void detect_users_free(LoggedUser *u, size_t n) {
    (void)n;
    free(u);
}

void detect_users_hint(char *out, size_t n) {
    size_t m = 0;
    LoggedUser *u = detect_users(&m);
    if (m > 0) {
        snprintf(out, n, "%s", u[0].user);
    } else {
        char *e = detect_getenv("USER");
        snprintf(out, n, "%s", e ? e : "");
        free(e);
    }
    free(u);
}
