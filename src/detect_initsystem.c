#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "detect.h"

static void normalize(const char *comm, const char *exe, char *out, size_t n) {
    const char *eb = strrchr(exe, '/');
    eb = eb ? eb + 1 : exe;
    if (!strcmp(comm, "systemd") || !strcmp(eb, "systemd") || strstr(exe, "systemd")) {
        snprintf(out, n, "systemd");
        return;
    }
    if (!strcmp(comm, "init")) {
        if (strstr(eb, "openrc"))
            snprintf(out, n, "OpenRC");
        else if (strstr(eb, "runit"))
            snprintf(out, n, "runit");
        else
            snprintf(out, n, "SysVinit");
        return;
    }
    if (!strcmp(comm, "openrc-init") || !strcmp(comm, "openrc"))
        snprintf(out, n, "OpenRC");
    else if (!strcmp(comm, "runit") || !strcmp(comm, "runsvdir"))
        snprintf(out, n, "runit");
    else if (!strcmp(comm, "s6-svscan") || !strcmp(comm, "s6"))
        snprintf(out, n, "s6");
    else if (!strcmp(comm, "dinit"))
        snprintf(out, n, "dinit");
    else if (!strcmp(comm, "sinit"))
        snprintf(out, n, "sinit");
    else if (!strcmp(comm, "shepherd"))
        snprintf(out, n, "GNU Shepherd");
    else if (!strcmp(comm, "launchd"))
        snprintf(out, n, "launchd");
    else if (!comm[0])
        out[0] = 0;
    else
        snprintf(out, n, "%s", comm);
}

static void detect_uncached(InitSystemInfo *out) {
    memset(out, 0, sizeof *out);
    char *comm = detect_read_file("/proc/1/comm");
    char c[128] = "";
    if (comm) {
        char *e = comm + strlen(comm);
        while (e > comm && (e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'))
            *--e = 0;
        snprintf(c, sizeof c, "%s", comm);
        free(comm);
    }
    char exe[1024] = "";
    ssize_t l = readlink("/proc/1/exe", exe, sizeof exe - 1);
    if (l > 0)
        exe[l] = 0;
    normalize(c, exe, out->name, sizeof out->name);
    if (!out->name[0])
        return;
    if (!strcmp(out->name, "systemd")) {
        const char *args[] = {"--version", NULL};
        char *o = detect_run_capture_timeout("systemctl", args, 500);
        if (o) {
            char *e = strchr(o, '\n');
            if (e)
                *e = 0;
            const char *s = o;
            if (!strncmp(s, "systemd", 7)) {
                s += 7;
                while (*s == ' ' || *s == '\t')
                    s++;
            }
            snprintf(out->version, sizeof out->version, "%s", s);
            free(o);
        }
    }
}

void detect_initsystem(InitSystemInfo *out) {
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    static int have = 0;
    static InitSystemInfo cached;
    pthread_mutex_lock(&mu);
    if (have) {
        *out = cached;
        pthread_mutex_unlock(&mu);
        return;
    }
    pthread_mutex_unlock(&mu);
    InitSystemInfo info;
    detect_uncached(&info);
    pthread_mutex_lock(&mu);
    cached = info;
    have = 1;
    *out = info;
    pthread_mutex_unlock(&mu);
}
