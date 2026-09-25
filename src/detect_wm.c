#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "detect.h"

static const char *WMS[] = {
    "hyprland", "sway", "dwm", "i3", "bspwm", "openbox", "xmonad", "awesome",
    "qtile", "river", "wayfire", "kwin_wayland", "mutter", "weston", "xfwm4",
    "cinnamon", "muffin", "gnome-shell", "dwl", "labwc", "niri", "mango"
};

static void first_version_token(const char *out, char *res, size_t n) {
    res[0] = 0;
    char line[512];
    size_t i = 0;
    while (out[i] && out[i] != '\n' && i + 1 < sizeof line) {
        line[i] = out[i];
        i++;
    }
    line[i] = 0;
    char *s = line;
    while (*s == ' ' || *s == '\t')
        s++;
    size_t len = strlen(s);
    size_t p = 0;
    while (p < len) {
        if (s[p] >= '0' && s[p] <= '9') {
            size_t j = p;
            for (;;) {
                while (j < len && s[j] >= '0' && s[j] <= '9')
                    j++;
                if (j < len && s[j] == '.') {
                    size_t k = j + 1;
                    while (k < len && s[k] >= '0' && s[k] <= '9')
                        k++;
                    if (k == j + 1)
                        break;
                    j = k;
                } else {
                    break;
                }
            }
            if (j > p && memchr(s + p, '.', j - p)) {
                size_t l = j - p;
                if (l >= n)
                    l = n - 1;
                memcpy(res, s + p, l);
                res[l] = 0;
                return;
            }
            p = j > p ? j : p + 1;
        } else {
            p++;
        }
    }
}

static void detect_uncached(WmInfo *out) {
    memset(out, 0, sizeof *out);
    char *w = detect_scan_proc_comm(WMS, 22);
    if (w) {
        snprintf(out->name, sizeof out->name, "%s", w);
        free(w);
    } else {
        static const char *keys[] = {"DWMSESSION", "DESKTOP_SESSION", "GDMSESSION",
                                     "WM", "SWAY_DESKTOP_SESSION"};
        for (int i = 0; i < 5; i++) {
            char *v = detect_getenv(keys[i]);
            if (v && *v) {
                snprintf(out->name, sizeof out->name, "%s", v);
                free(v);
                break;
            }
            free(v);
        }
    }
    if (getenv("WAYLAND_DISPLAY"))
        snprintf(out->session_type, sizeof out->session_type, "Wayland");
    else if (getenv("DISPLAY"))
        snprintf(out->session_type, sizeof out->session_type, "X11");
    if (out->name[0]) {
        char lower[128];
        size_t i = 0;
        while (out->name[i] && i + 1 < sizeof lower) {
            char c = out->name[i];
            lower[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        lower[i] = 0;
        const char *bins[2] = {out->name, lower};
        for (int b = 0; b < 2; b++) {
            const char *args[] = {"--version", NULL};
            char *o = detect_run_capture_timeout(bins[b], args, 500);
            if (o) {
                first_version_token(o, out->version, sizeof out->version);
                free(o);
                if (out->version[0])
                    break;
            }
        }
    }
}

void detect_wm(WmInfo *out) {
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    static int have = 0;
    static WmInfo cached;
    pthread_mutex_lock(&mu);
    if (have) {
        *out = cached;
        pthread_mutex_unlock(&mu);
        return;
    }
    pthread_mutex_unlock(&mu);
    WmInfo info;
    detect_uncached(&info);
    pthread_mutex_lock(&mu);
    cached = info;
    have = 1;
    *out = info;
    pthread_mutex_unlock(&mu);
}
