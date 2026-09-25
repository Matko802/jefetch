#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "detect.h"

static int wireless_signal(const char *ifname, unsigned *out) {
    char *text = detect_read_file("/proc/net/wireless");
    if (!text)
        return 0;
    char *save = NULL;
    char *line = strtok_r(text, "\n", &save);
    while (line) {
        while (*line == ' ' || *line == '\t')
            line++;
        char *colon = strchr(line, ':');
        if (!colon) {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        *colon = 0;
        char *name = line;
        char *e = name + strlen(name);
        while (e > name && (e[-1] == ' ' || e[-1] == '\t'))
            *--e = 0;
        if (strcmp(name, ifname)) {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        char *rest = colon + 1;
        while (*rest == ' ' || *rest == '\t')
            rest++;
        char *s2 = NULL;
        char *dup = strdup(rest);
        char *tok = strtok_r(dup, " \t", &s2);
        int idx = 0;
        char qual[32] = "";
        while (tok) {
            if (idx == 1)
                snprintf(qual, sizeof qual, "%s", tok);
            idx++;
            tok = strtok_r(NULL, " \t", &s2);
        }
        free(dup);
        char *slash = strchr(qual, '/');
        int ok = 0;
        if (slash) {
            *slash = 0;
            double cur = strtod(qual, NULL);
            double max = strtod(slash + 1, NULL);
            if (max > 0.0) {
                double pct = round(cur / max * 100.0);
                if (pct < 0.0)
                    pct = 0.0;
                if (pct > 100.0)
                    pct = 100.0;
                *out = (unsigned)pct;
                ok = 1;
            }
        }
        free(text);
        return ok;
    }
    free(text);
    return 0;
}

WifiInfo *detect_wifi(size_t *n) {
    WifiInfo *out = NULL;
    size_t m = 0;
    *n = 0;
    DIR *dp = opendir("/sys/class/net");
    if (!dp)
        return NULL;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        size_t el = strlen(de->d_name);
        if (el == 0 || el >= 64)
            continue;
        char ifname[64];
        memcpy(ifname, de->d_name, el + 1);
        char up[320];
        snprintf(up, sizeof up, "/sys/class/net/%s/uevent", ifname);
        char *uevent = detect_read_file(up);
        int is_wlan = uevent && strstr(uevent, "DEVTYPE=wlan");
        free(uevent);
        if (!is_wlan)
            continue;
        unsigned sig = 0;
        if (!wireless_signal(ifname, &sig))
            continue;
        const char *args[] = {"-r", ifname, NULL};
        char *ssid = detect_run_capture_timeout("iwgetid", args, 500);
        out = realloc(out, (m + 1) * sizeof(WifiInfo));
        snprintf(out[m].protocol, sizeof out[m].protocol, "802.11");
        snprintf(out[m].name, sizeof out[m].name, "%s", ifname);
        snprintf(out[m].ssid, sizeof out[m].ssid, "%s", ssid ? ssid : "");
        out[m].signal_quality = (unsigned char)sig;
        out[m].security[0] = 0;
        m++;
        free(ssid);
    }
    closedir(dp);
    *n = m;
    return out;
}

void detect_wifi_free(WifiInfo *w, size_t n) {
    (void)n;
    free(w);
}
