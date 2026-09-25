#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "detect.h"

BatteryInfo *detect_battery(size_t *n) {
    BatteryInfo *out = NULL;
    size_t m = 0;
    *n = 0;
    DIR *dp = opendir("/sys/class/power_supply");
    if (!dp)
        return NULL;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (strncmp(de->d_name, "BAT", 3) != 0)
            continue;
        char base[320];
        snprintf(base, sizeof base, "/sys/class/power_supply/%s", de->d_name);
        char p[384];
        char *cap_s;
        snprintf(p, sizeof p, "%s/capacity", base);
        cap_s = detect_read_file(p);
        char *cap_t = cap_s;
        while (cap_t && (*cap_t == ' ' || *cap_t == '\t' || *cap_t == '\n'))
            cap_t++;
        unsigned cap = cap_s ? (unsigned)strtoul(cap_t, NULL, 10) : 0;
        int have_cap = cap_s && *cap_t;
        if (cap == 0 && !have_cap) {
            free(cap_s);
            continue;
        }
        size_t dl = strlen(de->d_name);
        if (dl >= 128)
            continue;
        out = realloc(out, (m + 1) * sizeof(BatteryInfo));
        BatteryInfo *b = &out[m];
        memset(b, 0, sizeof *b);
        memcpy(b->name, de->d_name, dl + 1);
        snprintf(p, sizeof p, "%s/manufacturer", base);
        char *t = detect_read_file(p);
        if (t) {
            char *e = t + strlen(t);
            while (e > t && (e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'))
                *--e = 0;
            snprintf(b->manufacturer, sizeof b->manufacturer, "%s", t);
            free(t);
        }
        snprintf(p, sizeof p, "%s/model_name", base);
        t = detect_read_file(p);
        if (t) {
            char *e = t + strlen(t);
            while (e > t && (e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'))
                *--e = 0;
            snprintf(b->model, sizeof b->model, "%s", t);
            free(t);
        }
        snprintf(p, sizeof p, "%s/technology", base);
        t = detect_read_file(p);
        if (t) {
            char *e = t + strlen(t);
            while (e > t && (e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'))
                *--e = 0;
            snprintf(b->technology, sizeof b->technology, "%s", t);
            free(t);
        }
        b->capacity_percent = (unsigned char)cap;
        snprintf(p, sizeof p, "%s/status", base);
        t = detect_read_file(p);
        if (t) {
            char *e = t + strlen(t);
            while (e > t && (e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'))
                *--e = 0;
            snprintf(b->status, sizeof b->status, "%s", t);
            free(t);
        }
        snprintf(p, sizeof p, "%s/energy_now", base);
        t = detect_read_file(p);
        b->energy_now = t ? strtod(t, NULL) / 3600000.0 : 0.0;
        free(t);
        snprintf(p, sizeof p, "%s/energy_full", base);
        t = detect_read_file(p);
        b->energy_full = t ? strtod(t, NULL) / 3600000.0 : 0.0;
        free(t);
        snprintf(p, sizeof p, "%s/temp", base);
        t = detect_read_file(p);
        b->temp_c = t ? strtod(t, NULL) / 10.0 : 0.0;
        free(t);
        snprintf(p, sizeof p, "%s/voltage_now", base);
        t = detect_read_file(p);
        b->voltage_mv = t ? strtoull(t, NULL, 10) / 1000 : 0;
        free(t);
        free(cap_s);
        m++;
    }
    closedir(dp);
    *n = m;
    return out;
}

void detect_battery_free(BatteryInfo *b, size_t n) {
    (void)n;
    free(b);
}
