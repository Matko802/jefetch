#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "detect.h"

void detect_user(UserInfo *out) {
    char user[256] = "";
    char host[256] = "";
    char *v = detect_getenv("LOGNAME");
    if (!v)
        v = detect_getenv("USER");
    if (!v) {
        struct passwd *pwd = getpwuid(geteuid());
        if (pwd && pwd->pw_name)
            v = strdup(pwd->pw_name);
    }
    if (v) {
        snprintf(user, sizeof user, "%s", v);
        free(v);
    } else {
        snprintf(user, sizeof user, "unknown");
    }
    v = detect_getenv("HOSTNAME");
    if (!v) {
        char *h = detect_read_file("/proc/sys/kernel/hostname");
        if (h) {
            char *e = h + strlen(h);
            while (e > h && (e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'))
                *--e = 0;
            v = h;
        }
    }
    if (!v) {
        char buf[256];
        if (gethostname(buf, sizeof buf) == 0)
            v = strdup(buf);
    }
    if (v) {
        snprintf(host, sizeof host, "%s", v);
        free(v);
    } else {
        snprintf(host, sizeof host, "unknown");
    }
    memset(out, 0, sizeof *out);
    snprintf(out->user_name, sizeof out->user_name, "%s", user);
    snprintf(out->host_name, sizeof out->host_name, "%s", host);
    char *at = strchr(user, '@');
    if (at)
        *at = 0;
    snprintf(out->user_name_part, sizeof out->user_name_part, "%s", user);
    char *dot = strchr(host, '.');
    if (dot)
        *dot = 0;
    snprintf(out->host_name_part, sizeof out->host_name_part, "%s", host);
}
