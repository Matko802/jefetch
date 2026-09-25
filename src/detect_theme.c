#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "detect.h"

static void gsettings_lookup(const char *key, char *out, size_t n) {
    out[0] = 0;
    const char *args[] = {"get", "org.gnome.desktop.interface", key, NULL};
    char *v = detect_run_capture_timeout("gsettings", args, 500);
    if (!v)
        return;
    char *s = v;
    while (*s == ' ' || *s == '\t')
        s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
        *--e = 0;
    if (e - s >= 2 && s[0] == '\'' && e[-1] == '\'') {
        e[-1] = 0;
        s++;
    }
    if (*s)
        snprintf(out, n, "%s", s);
    free(v);
}

static void parse_settings_file(GtkThemeInfo *info) {
    char *home = detect_getenv("HOME");
    if (!home)
        return;
    const char *names[] = {"/.config/gtk-3.0/settings.ini", "/.gtkrc-2.0",
                           "/.config/gtk-4.0/settings.ini"};
    for (int i = 0; i < 3; i++) {
        char path[1152];
        snprintf(path, sizeof path, "%s%s", home, names[i]);
        char *text = detect_read_file(path);
        if (!text)
            continue;
        char *save = NULL;
        char *line = strtok_r(text, "\n", &save);
        while (line) {
            while (*line == ' ' || *line == '\t')
                line++;
            if (*line == '#' || *line == ';' || !*line) {
                line = strtok_r(NULL, "\n", &save);
                continue;
            }
            char *eq = strchr(line, '=');
            if (!eq) {
                line = strtok_r(NULL, "\n", &save);
                continue;
            }
            *eq = 0;
            char *k = line;
            char *ke = k + strlen(k);
            while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t'))
                *--ke = 0;
            char *v = eq + 1;
            while (*v == ' ' || *v == '\t')
                v++;
            char *ve = v + strlen(v);
            while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t' || ve[-1] == '\r'))
                *--ve = 0;
            if (ve - v >= 2 && v[0] == '"' && ve[-1] == '"') {
                v++;
                ve[-1] = 0;
            }
            if (!strcmp(k, "gtk-theme-name"))
                snprintf(info->gtk_theme, sizeof info->gtk_theme, "%s", v);
            else if (!strcmp(k, "gtk-icon-theme-name"))
                snprintf(info->icon_theme, sizeof info->icon_theme, "%s", v);
            else if (!strcmp(k, "gtk-cursor-theme-name"))
                snprintf(info->cursor_theme, sizeof info->cursor_theme, "%s", v);
            else if (!strcmp(k, "gtk-font-name")) {
                if (!info->font[0])
                    snprintf(info->font, sizeof info->font, "%s", v);
            } else if (!strcmp(k, "gtk-font-size")) {
                info->font_size = (unsigned)strtoul(v, NULL, 10);
            } else if (!strcmp(k, "gtk-color-scheme")) {
                snprintf(info->color_scheme, sizeof info->color_scheme, "%s", v);
            }
            line = strtok_r(NULL, "\n", &save);
        }
        free(text);
    }
    free(home);
}

static void desktop_name(char *out, size_t n) {
    out[0] = 0;
    const char *keys[] = {"XDG_CURRENT_DESKTOP", "XDG_SESSION_DESKTOP"};
    for (int i = 0; i < 2; i++) {
        char *v = detect_getenv(keys[i]);
        if (v && *v) {
            snprintf(out, n, "%s", v);
            free(v);
            return;
        }
        free(v);
    }
}

static void detect_uncached(GtkThemeInfo *info) {
    char tmp[128];
    memset(info, 0, sizeof *info);
    desktop_name(tmp, sizeof tmp);
    snprintf(info->desktop, sizeof info->desktop, "%s", tmp);
    parse_settings_file(info);
    gsettings_lookup("gtk-theme", tmp, sizeof tmp);
    if (tmp[0])
        snprintf(info->gtk_theme, sizeof info->gtk_theme, "%s", tmp);
    gsettings_lookup("icon-theme", tmp, sizeof tmp);
    if (tmp[0])
        snprintf(info->icon_theme, sizeof info->icon_theme, "%s", tmp);
    gsettings_lookup("cursor-theme", tmp, sizeof tmp);
    if (tmp[0])
        snprintf(info->cursor_theme, sizeof info->cursor_theme, "%s", tmp);
    gsettings_lookup("font-name", tmp, sizeof tmp);
    if (tmp[0])
        snprintf(info->font, sizeof info->font, "%s", tmp);
    gsettings_lookup("color-scheme", tmp, sizeof tmp);
    if (tmp[0])
        snprintf(info->color_scheme, sizeof info->color_scheme, "%s", tmp);
}

void detect_theme(GtkThemeInfo *out) {
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    static int have = 0;
    static GtkThemeInfo cached;
    static char k1[64], k2[64], k3[64], k4[64];
    char *home = detect_getenv("HOME");
    char n1[1152] = "", n2[1152] = "", n3[1152] = "", n4[1152] = "";
    if (home) {
        struct stat st;
        snprintf(n1, sizeof n1, "%s/.config/gtk-3.0/settings.ini", home);
        snprintf(n2, sizeof n2, "%s/.gtkrc-2.0", home);
        snprintf(n3, sizeof n3, "%s/.config/gtk-4.0/settings.ini", home);
        snprintf(n4, sizeof n4, "%s/.config/dconf/user", home);
        free(home);
        (void)st;
    }
    char m1[64] = "", m2[64] = "", m3[64] = "", m4[64] = "";
    struct stat st;
    if (!stat(n1, &st))
        snprintf(m1, sizeof m1, "%lld.%ld", (long long)st.st_mtime, (long)st.st_mtim.tv_nsec);
    if (!stat(n2, &st))
        snprintf(m2, sizeof m2, "%lld.%ld", (long long)st.st_mtime, (long)st.st_mtim.tv_nsec);
    if (!stat(n3, &st))
        snprintf(m3, sizeof m3, "%lld.%ld", (long long)st.st_mtime, (long)st.st_mtim.tv_nsec);
    if (!stat(n4, &st))
        snprintf(m4, sizeof m4, "%lld.%ld", (long long)st.st_mtime, (long)st.st_mtim.tv_nsec);
    pthread_mutex_lock(&mu);
    if (have && !strcmp(k1, m1) && !strcmp(k2, m2) && !strcmp(k3, m3) && !strcmp(k4, m4)) {
        *out = cached;
        pthread_mutex_unlock(&mu);
        return;
    }
    pthread_mutex_unlock(&mu);
    GtkThemeInfo info;
    detect_uncached(&info);
    pthread_mutex_lock(&mu);
    cached = info;
    snprintf(k1, sizeof k1, "%s", m1);
    snprintf(k2, sizeof k2, "%s", m2);
    snprintf(k3, sizeof k3, "%s", m3);
    snprintf(k4, sizeof k4, "%s", m4);
    have = 1;
    *out = info;
    pthread_mutex_unlock(&mu);
}
