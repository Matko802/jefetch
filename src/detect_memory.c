#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "detect.h"

void detect_memory(MemoryInfo *out) {
    memset(out, 0, sizeof *out);
    size_t n = 0;
    char **lines = detect_read_file_lines("/proc/meminfo", &n);
    for (size_t i = 0; i < n; i++) {
        char *line = lines[i];
        char *colon = strchr(line, ':');
        if (!colon)
            continue;
        *colon = 0;
        unsigned long long kb = strtoull(colon + 1, NULL, 10);
        unsigned long long bytes = kb * 1024;
        if (!strcmp(line, "MemTotal"))
            out->mem_total = bytes;
        else if (!strcmp(line, "MemFree"))
            out->mem_free = bytes;
        else if (!strcmp(line, "MemAvailable"))
            out->mem_available = bytes;
        else if (!strcmp(line, "Buffers"))
            out->mem_buffers = bytes;
        else if (!strcmp(line, "Cached"))
            out->mem_cached = bytes;
        else if (!strcmp(line, "SwapTotal"))
            out->swap_total = bytes;
        else if (!strcmp(line, "SwapFree"))
            out->swap_free_val = bytes;
    }
    detect_free_lines(lines, n);
    out->mem_used = out->mem_available >= out->mem_total ? 0 : out->mem_total - out->mem_available;
    out->swap_used = out->swap_total >= out->swap_free_val ? out->swap_total - out->swap_free_val : 0;
}
