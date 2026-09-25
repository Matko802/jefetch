#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "detect.h"

static const char *KNOWN[] = {
    "gdm", "gdm3", "sddm", "sddm-greeter", "lightdm", "lxdm", "ly", "greetd",
    "agreety", "emptty", "slim", "xdm", "wdm", "tdm", "entrance", "nodm"
};

static void normalize(const char *raw, char *out, size_t n) {
    char l[128];
    size_t i = 0;
    while (raw[i] && i + 1 < sizeof l) {
        char c = raw[i];
        l[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    l[i] = 0;
    const char *v = "";
    if (!strcmp(l, "gdm") || !strcmp(l, "gdm3"))
        v = "GDM";
    else if (!strcmp(l, "sddm") || !strcmp(l, "sddm-greeter") || !strcmp(l, "sddm-helper"))
        v = "SDDM";
    else if (!strcmp(l, "lightdm"))
        v = "LightDM";
    else if (!strcmp(l, "lxdm"))
        v = "LXDM";
    else if (!strcmp(l, "ly"))
        v = "ly";
    else if (!strcmp(l, "greetd") || !strcmp(l, "agreety"))
        v = "greetd";
    else if (!strcmp(l, "emptty"))
        v = "emptty";
    else if (!strcmp(l, "slim"))
        v = "SLiM";
    else if (!strcmp(l, "xdm"))
        v = "XDM";
    else if (!strcmp(l, "wdm"))
        v = "WDM";
    else if (!strcmp(l, "tdm"))
        v = "TDM";
    else if (!strcmp(l, "entrance"))
        v = "Entrance";
    else if (!strcmp(l, "nodm"))
        v = "nodm";
    else if (!l[0])
        v = "";
    else
        v = raw;
    snprintf(out, n, "%s", v);
}

static void accept_version_line(const char *out, const char *name, char *res, size_t n) {
    res[0] = 0;
    char first[128];
    size_t i = 0;
    while (out[i] && out[i] != '\n' && i + 1 < sizeof first) {
        first[i] = out[i];
        i++;
    }
    first[i] = 0;
    char *s = first;
    while (*s == ' ' || *s == '\t')
        s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
        *--e = 0;
    if (!*s || strlen(s) > 64)
        return;
    char ll[128], nl[128];
    size_t k = 0;
    while (s[k] && k + 1 < sizeof ll) {
        char c = s[k];
        ll[k++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    ll[k] = 0;
    k = 0;
    while (name[k] && k + 1 < sizeof nl) {
        char c = name[k];
        nl[k++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    nl[k] = 0;
    if (strstr(ll, nl)) {
        size_t sl = strlen(s);
        if (sl >= n)
            sl = n - 1;
        memcpy(res, s, sl + 1);
        return;
    }
    char *save = NULL;
    char *dup = strdup(ll);
    char *tok = strtok_r(dup, " \t", &save);
    while (tok) {
        if (tok[0] >= '0' && tok[0] <= '9' && strchr(tok, '.')) {
            size_t sl = strlen(s);
            if (sl >= n)
                sl = n - 1;
            memcpy(res, s, sl + 1);
            break;
        }
        tok = strtok_r(NULL, " \t", &save);
    }
    free(dup);
}

static void detect_uncached(LoginManagerInfo *out) {
    char link[512];
    ssize_t l = readlink("/etc/systemd/system/display-manager.service", link, sizeof link - 1);
    char raw[256] = "";
    if (l > 0) {
        link[l] = 0;
        char *base = strrchr(link, '/');
        base = base ? base + 1 : link;
        char *dot = strstr(base, ".service");
        if (dot)
            *dot = 0;
        if (base[0] && strcmp(base, "display-manager")) {
            size_t bl = strlen(base);
            if (bl < sizeof raw)
                memcpy(raw, base, bl + 1);
        }
    }
    if (!raw[0]) {
        ProcInfo pi;
        const char *names[16];
        for (int i = 0; i < 16; i++)
            names[i] = KNOWN[i];
        if (detect_proc_by_comm(names, 16, &pi)) {
            const char *b = strrchr(pi.exe_path, '/');
            b = b ? b + 1 : pi.exe_path;
            if (b[0] && strlen(b) < sizeof raw)
                memcpy(raw, b, strlen(b) + 1);
            else if (!b[0])
                snprintf(raw, sizeof raw, "%s", pi.comm);
            detect_proc_free(&pi);
        }
    }
    memset(out, 0, sizeof *out);
    if (!raw[0])
        return;
    normalize(raw, out->name, sizeof out->name);
    if (out->name[0]) {
        const char *args[] = {"--version", NULL};
        char *o = detect_run_capture_timeout(raw, args, 500);
        if (o) {
            accept_version_line(o, out->name, out->version, sizeof out->version);
            free(o);
        }
    }
}

void detect_lm(LoginManagerInfo *out) {
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    static int have = 0;
    static LoginManagerInfo cached;
    static char cached_key[512];
    static int have_key = 0;
    char link[512];
    ssize_t l = readlink("/etc/systemd/system/display-manager.service", link, sizeof link - 1);
    char key[512] = "";
    int has_key = 0;
    if (l > 0) {
        link[l] = 0;
        snprintf(key, sizeof key, "%s", link);
        has_key = 1;
    }
    pthread_mutex_lock(&mu);
    if (have && have_key == has_key && (!has_key || !strcmp(cached_key, key))) {
        *out = cached;
        pthread_mutex_unlock(&mu);
        return;
    }
    pthread_mutex_unlock(&mu);
    LoginManagerInfo info;
    detect_uncached(&info);
    pthread_mutex_lock(&mu);
    cached = info;
    snprintf(cached_key, sizeof cached_key, "%s", key);
    have_key = has_key;
    have = 1;
    *out = info;
    pthread_mutex_unlock(&mu);
}
