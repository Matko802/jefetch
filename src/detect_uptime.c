#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "detect.h"

void detect_uptime(UptimeInfo *out) {
    memset(out, 0, sizeof *out);
    char *v = detect_read_file("/proc/uptime");
    if (v) {
        char *end;
        double f = strtod(v, &end);
        if (end != v)
            out->uptime_secs = (unsigned long long)f;
        free(v);
    }
    char *b = detect_read_file("/proc/sys/kernel/btime");
    if (b) {
        out->boot_time_secs = strtoull(b, NULL, 10);
        free(b);
    }
    if (out->boot_time_secs == 0 && out->uptime_secs > 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        unsigned long long now = (unsigned long long)ts.tv_sec;
        out->boot_time_secs = now >= out->uptime_secs ? now - out->uptime_secs : 0;
    }
}
