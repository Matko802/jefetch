#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "detect.h"

BrightnessInfo *detect_brightness(size_t *n) {
    BrightnessInfo *out = NULL;
    size_t m = 0;
    *n = 0;
    DIR *dp = opendir("/sys/class/backlight");
    if (!dp)
        return NULL;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        char base[320];
        snprintf(base, sizeof base, "/sys/class/backlight/%s", de->d_name);
        char p[384];
        snprintf(p, sizeof p, "%s/brightness", base);
        char *vs = detect_read_file(p);
        snprintf(p, sizeof p, "%s/max_brightness", base);
        char *ms = detect_read_file(p);
        if (!vs || !ms) {
            free(vs);
            free(ms);
            continue;
        }
        unsigned long long value = strtoull(vs, NULL, 10);
        unsigned long long max = strtoull(ms, NULL, 10);
        free(vs);
        free(ms);
        if (max == 0)
            continue;
        double pct = round((double)value / (double)max * 100.0);
        if (pct < 0.0)
            pct = 0.0;
        if (pct > 100.0)
            pct = 100.0;
        size_t dl = strlen(de->d_name);
        if (dl >= 128)
            continue;
        out = realloc(out, (m + 1) * sizeof(BrightnessInfo));
        memcpy(out[m].name, de->d_name, dl + 1);
        out[m].value = value;
        out[m].max = max;
        out[m].percentage = (unsigned)pct;
        m++;
    }
    closedir(dp);
    *n = m;
    return out;
}

void detect_brightness_free(BrightnessInfo *b, size_t n) {
    (void)n;
    free(b);
}
