#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "detect.h"

void detect_kernel(KernelInfo *out) {
    memset(out, 0, sizeof *out);
    snprintf(out->sysname, sizeof out->sysname, "Linux");
    char *r = detect_read_file("/proc/sys/kernel/osrelease");
    if (r) {
        char *e = r + strlen(r);
        while (e > r && (e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'))
            *--e = 0;
        snprintf(out->release, sizeof out->release, "%s", r);
        free(r);
    }
    char *v = detect_read_file("/proc/sys/kernel/version");
    if (v) {
        char *e = v + strlen(v);
        while (e > v && (e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'))
            *--e = 0;
        snprintf(out->version, sizeof out->version, "%s", v);
        free(v);
    }
}
