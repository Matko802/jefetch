#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

#include "detect.h"

static void normalize_arch(const char *a, char *out, size_t n) {
    const char *v = a;
    if (!strcmp(a, "x86_64") || !strcmp(a, "x86-64") || !strcmp(a, "amd64"))
        v = "x86_64";
    else if (!strcmp(a, "aarch64") || !strcmp(a, "arm64") || !strcmp(a, "aarch64_be"))
        v = "aarch64";
    else if (!strcmp(a, "armv7l") || !strcmp(a, "armv7b") || !strcmp(a, "armv7hl"))
        v = "armv7l";
    else if (!strcmp(a, "armv6l"))
        v = "armv6l";
    else if (!strcmp(a, "arm") || !strcmp(a, "armv5tel") || !strcmp(a, "armv5tejl") ||
             !strcmp(a, "armv8l"))
        v = "arm";
    else if (!strcmp(a, "i386") || !strcmp(a, "i486") || !strcmp(a, "i586") ||
             !strcmp(a, "i686") || !strcmp(a, "x86"))
        v = "i686";
    else if (!strcmp(a, "riscv64"))
        v = "riscv64";
    else if (!strcmp(a, "loongarch64"))
        v = "loongarch64";
    else if (!strcmp(a, "ppc64le") || !strcmp(a, "ppc64") || !strcmp(a, "powerpc64le"))
        v = "ppc64le";
    else if (!strcmp(a, "s390x"))
        v = "s390x";
    snprintf(out, n, "%s", v);
}

static void copy_field(char *dst, size_t n, const char *v) {
    snprintf(dst, n, "%.*s", (int)(n - 1), v);
}

void detect_arch(char *out, size_t n) {
    struct utsname u;
    if (uname(&u) == 0) {
        normalize_arch(u.machine, out, n);
        return;
    }
#if defined(__x86_64__)
    snprintf(out, n, "x86_64");
#elif defined(__aarch64__)
    snprintf(out, n, "aarch64");
#else
    snprintf(out, n, "unknown");
#endif
}

static void detect_uncached(OsInfo *out) {
    memset(out, 0, sizeof *out);
    detect_arch(out->arch, sizeof out->arch);
    size_t nkv = 0;
    DetectKV *kv = detect_parse_kv_file("/etc/os-release", &nkv);
    if (nkv == 0) {
        detect_free_kv(kv, nkv);
        kv = detect_parse_kv_file("/usr/lib/os-release", &nkv);
    }
    for (size_t i = 0; i < nkv; i++) {
        char v[128];
        snprintf(v, sizeof v, "%s", kv[i].val);
        detect_unquote(v);
        if (!strcmp(kv[i].key, "NAME"))
            copy_field(out->name, sizeof out->name, v);
        else if (!strcmp(kv[i].key, "VERSION"))
            copy_field(out->version, sizeof out->version, v);
        else if (!strcmp(kv[i].key, "VERSION_ID"))
            copy_field(out->version_id, sizeof out->version_id, v);
        else if (!strcmp(kv[i].key, "ID"))
            copy_field(out->id, sizeof out->id, v);
        else if (!strcmp(kv[i].key, "ID_LIKE"))
            copy_field(out->id_like, sizeof out->id_like, v);
        else if (!strcmp(kv[i].key, "PRETTY_NAME"))
            copy_field(out->pretty_name, sizeof out->pretty_name, v);
        else if (!strcmp(kv[i].key, "BUILD_ID"))
            copy_field(out->build_id, sizeof out->build_id, v);
        else if (!strcmp(kv[i].key, "CODENAME") || !strcmp(kv[i].key, "VERSION_CODENAME"))
            copy_field(out->codename, sizeof out->codename, v);
        else if (!strcmp(kv[i].key, "VARIANT"))
            copy_field(out->variant, sizeof out->variant, v);
        else if (!strcmp(kv[i].key, "VARIANT_ID"))
            copy_field(out->variant_id, sizeof out->variant_id, v);
    }
    detect_free_kv(kv, nkv);
    if (!out->name[0]) {
        size_t nl = 0;
        char **lines = detect_read_file_lines("/etc/issue", &nl);
        for (size_t i = 0; i < nl; i++) {
            char *line = lines[i];
            size_t l = strlen(line);
            while (l > 0 && (line[l - 1] == ' ' || line[l - 1] == '\t' || line[l - 1] == '\n'))
                line[--l] = 0;
            if (l > 0) {
                while (l >= 2 && line[l - 2] == '\\' && line[l - 1] == 'n') {
                    l -= 2;
                    line[l] = 0;
                }
                while (l >= 1 && line[l - 1] == '\\') {
                    l -= 1;
                    line[l] = 0;
                }
                if (l > 0) {
                    snprintf(out->name, sizeof out->name, "%s", line);
                    break;
                }
            }
        }
        detect_free_lines(lines, nl);
    }
    if (!out->name[0])
        snprintf(out->name, sizeof out->name, "Linux");
}

void detect_os(OsInfo *out) {
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    static int have = 0;
    static OsInfo cached;
    pthread_mutex_lock(&mu);
    if (have) {
        *out = cached;
        pthread_mutex_unlock(&mu);
        return;
    }
    pthread_mutex_unlock(&mu);
    OsInfo info;
    detect_uncached(&info);
    pthread_mutex_lock(&mu);
    cached = info;
    have = 1;
    *out = info;
    pthread_mutex_unlock(&mu);
}
