#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "detect.h"

void detect_de(DeInfo *out) {
    static const char *keys[] = {"XDG_CURRENT_DESKTOP", "XDG_SESSION_DESKTOP",
                                 "DESKTOP_SESSION", "GDMSESSION"};
    memset(out, 0, sizeof *out);
    for (int i = 0; i < 4; i++) {
        char *v = detect_getenv(keys[i]);
        if (!v)
            continue;
        char *s = v;
        while (*s == ' ' || *s == '\t')
            s++;
        char *e = s + strlen(s);
        while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
            *--e = 0;
        if (*s) {
            snprintf(out->name, sizeof out->name, "%s", s);
            free(v);
            break;
        }
        free(v);
    }
}
