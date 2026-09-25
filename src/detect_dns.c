#include <stdlib.h>
#include <string.h>

#include "detect.h"

int detect_dns(DnsInfo *out) {
    char *text = detect_read_file("/etc/resolv.conf");
    if (!text)
        return 0;
    memset(out, 0, sizeof *out);
    char *save = NULL;
    char *line = strtok_r(text, "\n", &save);
    while (line) {
        while (*line == ' ' || *line == '\t')
            line++;
        if (!*line || *line == '#' || *line == ';') {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        char *e = line;
        while (*e && *e != ' ' && *e != '\t')
            e++;
        size_t kl = (size_t)(e - line);
        char *val = e;
        while (*val == ' ' || *val == '\t')
            val++;
        char *ve = val + strlen(val);
        while (ve > val && (ve[-1] == ' ' || ve[-1] == '\t' || ve[-1] == '\r'))
            *--ve = 0;
        if (kl == 10 && !memcmp(line, "nameserver", 10)) {
            if (*val) {
                out->servers = realloc(out->servers, (out->nservers + 1) * sizeof(char *));
                out->servers[out->nservers++] = strdup(val);
            }
        } else if ((kl == 6 && !memcmp(line, "search", 6)) ||
                   (kl == 6 && !memcmp(line, "domain", 6))) {
            if (!out->domain[0]) {
                char *sp = strchr(val, ' ');
                char *tb = strchr(val, '\t');
                size_t l = strlen(val);
                if (sp && (size_t)(sp - val) < l)
                    l = (size_t)(sp - val);
                if (tb && (size_t)(tb - val) < l)
                    l = (size_t)(tb - val);
                if (l >= sizeof out->domain)
                    l = sizeof out->domain - 1;
                memcpy(out->domain, val, l);
                out->domain[l] = 0;
            }
        }
        line = strtok_r(NULL, "\n", &save);
    }
    free(text);
    if (out->nservers == 0) {
        free(out->servers);
        out->servers = NULL;
        return 0;
    }
    return 1;
}

void detect_dns_free(DnsInfo *d) {
    for (size_t i = 0; i < d->nservers; i++)
        free(d->servers[i]);
    free(d->servers);
    d->servers = NULL;
    d->nservers = 0;
}
