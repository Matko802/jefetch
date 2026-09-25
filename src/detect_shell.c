#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "detect.h"

static const char *KNOWN_SHELLS[] = {
    "sh", "bash", "zsh", "fish", "csh", "tcsh", "ksh", "dash", "ash", "posh",
    "elvish", "oil", "nushell", "pwsh", "yash", "busybox", "nu", "xonsh",
    "oil.ovm", NULL
};

static const char *SKIP[] = {
    "sudo", "su", "doas", "strace", "gdb", "lldb", "login", "ltrace", "perf",
    "time", "script", "proot", "fastfetch", "jefetch", "flatpak", NULL
};

static void lower_base(const char *path, char *out, size_t n) {
    const char *b = strrchr(path, '/');
    b = b ? b + 1 : path;
    while (*b == '-')
        b++;
    size_t i = 0;
    while (b[i] && i + 1 < n) {
        char c = b[i];
        out[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    out[i] = 0;
}

static int in_list(const char *base, const char *const *list) {
    for (int i = 0; list[i]; i++) {
        if (!strcmp(base, list[i]))
            return 1;
    }
    return 0;
}

static void resolve_path(const char *path, char *out, size_t n) {
    if (strchr(path, '/')) {
        if (!realpath(path, out))
            snprintf(out, n, "%s", path);
    } else {
        snprintf(out, n, "%s", path);
    }
}

static void find_shell_via_proc(char *out, size_t n) {
    out[0] = 0;
    unsigned pid = (unsigned)getppid();
    for (int depth = 0; depth < 20; depth++) {
        if (pid == 0 || pid == 1)
            break;
        char p[64], exe[1024] = "", comm[256] = "";
        snprintf(p, sizeof p, "/proc/%u/exe", pid);
        ssize_t l = readlink(p, exe, sizeof exe - 1);
        if (l > 0)
            exe[l] = 0;
        snprintf(p, sizeof p, "/proc/%u/comm", pid);
        char *c = detect_read_file(p);
        if (c) {
            char *e = c + strlen(c);
            while (e > c && (e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'))
                *--e = 0;
            snprintf(comm, sizeof comm, "%s", c);
            free(c);
        }
        char base[256], base_exe[256];
        lower_base(comm, base, sizeof base);
        lower_base(exe, base_exe, sizeof base_exe);
        int is_shell = in_list(base, KNOWN_SHELLS) || in_list(base_exe, KNOWN_SHELLS);
        int is_skip = in_list(base, SKIP) || in_list(base_exe, SKIP) ||
                      !strcmp(base, "sh") || !strcmp(comm, "sh");
        if (is_shell && !is_skip) {
            if (exe[0] && !strstr(exe, " (deleted)")) {
                snprintf(out, n, "%s", exe);
                return;
            }
            snprintf(out, n, "%s", comm);
            return;
        }
        if (is_skip || !strcmp(base, "sh")) {
            snprintf(p, sizeof p, "/proc/%u/status", pid);
            size_t nsl = 0;
            char **sl = detect_read_file_lines(p, &nsl);
            int moved = 0;
            for (size_t i = 0; i < nsl; i++) {
                if (!strncmp(sl[i], "PPid:", 5)) {
                    pid = (unsigned)strtoul(sl[i] + 5, NULL, 10);
                    moved = 1;
                    break;
                }
            }
            detect_free_lines(sl, nsl);
            if (!moved)
                break;
            continue;
        }
        snprintf(p, sizeof p, "/proc/%u/status", pid);
        size_t nsl = 0;
        char **sl = detect_read_file_lines(p, &nsl);
        unsigned ppid = 0;
        for (size_t i = 0; i < nsl; i++) {
            if (!strncmp(sl[i], "PPid:", 5)) {
                ppid = (unsigned)strtoul(sl[i] + 5, NULL, 10);
                break;
            }
        }
        detect_free_lines(sl, nsl);
        if (!ppid)
            break;
        char pc[256] = "";
        snprintf(p, sizeof p, "/proc/%u/comm", ppid);
        char *pcf = detect_read_file(p);
        if (pcf) {
            char *e = pcf + strlen(pcf);
            while (e > pcf && (e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'))
                *--e = 0;
            snprintf(pc, sizeof pc, "%s", pcf);
            free(pcf);
        }
        char pbase[256];
        lower_base(pc, pbase, sizeof pbase);
        if (in_list(pbase, KNOWN_SHELLS)) {
            char pexe[1024] = "";
            snprintf(p, sizeof p, "/proc/%u/exe", ppid);
            ssize_t pl = readlink(p, pexe, sizeof pexe - 1);
            if (pl > 0) {
                pexe[pl] = 0;
                snprintf(out, n, "%s", pexe);
            } else {
                snprintf(out, n, "%s", pc);
            }
            return;
        }
        pid = ppid;
    }
}

static void parse_version(const char *raw, const char *base, char *out, size_t n) {
    out[0] = 0;
    char first[512] = "";
    size_t i = 0;
    while (raw[i] && raw[i] != '\n' && i + 1 < sizeof first) {
        first[i] = raw[i];
        i++;
    }
    first[i] = 0;
    char *s = first;
    while (*s == ' ' || *s == '\t')
        s++;
    if (!*s)
        return;
    char words[32][128];
    int nw = 0;
    char *save = NULL;
    char *dup = strdup(s);
    char *tok = strtok_r(dup, " \t", &save);
    while (tok && nw < 32) {
        snprintf(words[nw], sizeof words[0], "%s", tok);
        nw++;
        tok = strtok_r(NULL, " \t", &save);
    }
    for (int k = 0; k < nw; k++) {
        char low[128];
        size_t q = 0;
        while (words[k][q] && q + 1 < sizeof low) {
            char c = words[k][q];
            low[q++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        low[q] = 0;
        if (!strcmp(low, "version") && k + 1 < nw) {
            char *v = words[k + 1];
            size_t l = strlen(v);
            while (l > 0 && v[l - 1] == ',')
                v[--l] = 0;
            snprintf(out, n, "%s", v);
            free(dup);
            return;
        }
    }
    for (int k = 0; k < nw; k++) {
        if (words[k][0] >= '0' && words[k][0] <= '9') {
            snprintf(out, n, "%s", words[k]);
            free(dup);
            return;
        }
    }
    snprintf(out, n, "%s", s);
    free(dup);
    (void)base;
}

static void detect_uncached(const char *shell_path, ShellInfo *out) {
    char resolved[1024];
    memset(out, 0, sizeof *out);
    if (!shell_path[0])
        return;
    snprintf(out->shell_path, sizeof out->shell_path, "%s", shell_path);
    const char *b = strrchr(shell_path, '/');
    b = b ? b + 1 : shell_path;
    while (*b == '-')
        b++;
    snprintf(out->shell_base_name, sizeof out->shell_base_name, "%s", b);
    snprintf(out->shell_name, sizeof out->shell_name, "%s", b);
    resolve_path(shell_path, resolved, sizeof resolved);
    if (resolved[0]) {
        const char *args[] = {"--version", NULL};
        char *raw = detect_run_capture_timeout(resolved, args, 500);
        if (raw) {
            parse_version(raw, b, out->shell_version, sizeof out->shell_version);
            free(raw);
        }
    }
}

void detect_shell(ShellInfo *out) {
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    static int have = 0;
    static ShellInfo cached;
    static char cached_path[1024];
    char path[1024] = "";
    find_shell_via_proc(path, sizeof path);
    if (!path[0]) {
        char *e = detect_getenv("SHELL");
        if (e) {
            snprintf(path, sizeof path, "%s", e);
            free(e);
        }
    }
    pthread_mutex_lock(&mu);
    if (have && !strcmp(cached_path, path)) {
        *out = cached;
        pthread_mutex_unlock(&mu);
        return;
    }
    pthread_mutex_unlock(&mu);
    ShellInfo info;
    detect_uncached(path, &info);
    pthread_mutex_lock(&mu);
    cached = info;
    snprintf(cached_path, sizeof cached_path, "%s", path);
    have = 1;
    *out = info;
    pthread_mutex_unlock(&mu);
}
