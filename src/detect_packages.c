#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "detect.h"

static void push_amount(PackagesInfo *info, const char *name, size_t n) {
    info->amounts = realloc(info->amounts, (info->n + 1) * sizeof(PkgAmount));
    info->amounts[info->n].name = strdup(name);
    info->amounts[info->n].count = n;
    info->n++;
}

static size_t count_lines_nonempty(const char *s) {
    size_t n = 0;
    char *dup = strdup(s);
    char *save = NULL;
    char *line = strtok_r(dup, "\n", &save);
    while (line) {
        while (*line == ' ' || *line == '\t')
            line++;
        char *e = line + strlen(line);
        while (e > line && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
            *--e = 0;
        if (*line)
            n++;
        line = strtok_r(NULL, "\n", &save);
    }
    free(dup);
    return n;
}

static void cut_hash(const char *path, char *out, size_t n) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const char *dash = strchr(base, '-');
    snprintf(out, n, "%s", dash ? dash + 1 : base);
}

static int has_dotted_version(const char *name) {
    size_t i = 0, len = strlen(name);
    while (i < len) {
        if (name[i] >= '0' && name[i] <= '9') {
            size_t j = i;
            while (j < len && name[j] >= '0' && name[j] <= '9')
                j++;
            size_t k = j;
            int groups = 0;
            while (k < len && name[k] == '.') {
                size_t m = k + 1;
                while (m < len && name[m] >= '0' && name[m] <= '9')
                    m++;
                if (m == k + 1)
                    break;
                groups++;
                k = m;
            }
            if (groups >= 1)
                return 1;
            i = j > i ? j : i + 1;
        } else {
            i++;
        }
    }
    return 0;
}

static int keep_nix_name(const char *name) {
    if (!strncmp(name, "nixos-system-nixos-", 20))
        return 0;
    size_t l = strlen(name);
    if ((l >= 4 && !strcmp(name + l - 4, "-doc")) ||
        (l >= 4 && !strcmp(name + l - 4, "-man")) ||
        (l >= 5 && !strcmp(name + l - 5, "-info")) ||
        (l >= 4 && !strcmp(name + l - 4, "-dev")) ||
        (l >= 4 && !strcmp(name + l - 4, "-bin")))
        return 0;
    return has_dotted_version(name);
}

static size_t filter_nix_requisites(const char *out) {
    size_t count = 0;
    char last[512] = "";
    char *dup = strdup(out);
    char *save = NULL;
    char *line = strtok_r(dup, "\n", &save);
    while (line) {
        while (*line == ' ' || *line == '\t')
            line++;
        char *e = line + strlen(line);
        while (e > line && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
            *--e = 0;
        if (*line) {
            struct stat st;
            if (stat(line, &st) == 0 && S_ISDIR(st.st_mode)) {
                char name[512];
                cut_hash(line, name, sizeof name);
                if (keep_nix_name(name) && strcmp(name, last)) {
                    snprintf(last, sizeof last, "%s", name);
                    count++;
                }
            }
        }
        line = strtok_r(NULL, "\n", &save);
    }
    free(dup);
    return count;
}

static size_t count_nix_profile(const char *path) {
    const char *args[] = {"-q", "--requisites", path, NULL};
    char *out = detect_run_capture_timeout("nix-store", args, 2500);
    if (!out)
        return 0;
    size_t n = filter_nix_requisites(out);
    free(out);
    return n;
}

static int path_exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

void detect_packages_free(PackagesInfo *p) {
    for (size_t i = 0; i < p->n; i++)
        free(p->amounts[i].name);
    free(p->amounts);
    p->amounts = NULL;
    p->n = 0;
}

static void detect_uncached(PackagesInfo *info) {
    memset(info, 0, sizeof *info);
    size_t nix_system = 0, nix_user = 0, nix_default = 0;
    if (path_exists("/nix/var/nix/profiles/system")) {
        nix_system = count_nix_profile("/run/current-system");
        nix_default = count_nix_profile("/nix/var/nix/profiles/default");
        char *home = detect_getenv("HOME");
        char *user = detect_getenv("USER");
        const char *cands[4];
        char b0[1152], b1[1152], b2[1152], b3[1152];
        int nc = 0;
        if (home) {
            snprintf(b0, sizeof b0, "%s/.nix-profile", home);
            snprintf(b1, sizeof b1, "%s/.local/state/nix/profiles/profile", home);
            cands[nc++] = b0;
            cands[nc++] = b1;
        }
        if (user && *user) {
            snprintf(b2, sizeof b2, "/etc/profiles/per-user/%s", user);
            snprintf(b3, sizeof b3, "/nix/var/nix/profiles/per-user/%s/profile", user);
            cands[nc++] = b2;
            cands[nc++] = b3;
        }
        for (int i = 0; i < nc; i++) {
            size_t n = count_nix_profile(cands[i]);
            if (n > 0) {
                nix_user = n;
                break;
            }
        }
        free(home);
        free(user);
    }
    size_t flat_system = 0, flat_user = 0;
    {
        const char *a1[] = {"list", "--system", "--columns=application", NULL};
        char *o = detect_run_capture_timeout("flatpak", a1, 400);
        if (o) {
            flat_system = count_lines_nonempty(o);
            free(o);
        }
    }
    {
        const char *a2[] = {"list", "--user", "--columns=application", NULL};
        char *o = detect_run_capture_timeout("flatpak", a2, 400);
        if (o) {
            flat_user = count_lines_nonempty(o);
            free(o);
        }
    }
    if (flat_system > 0)
        push_amount(info, "flatpak-system", flat_system);
    if (flat_user > 0)
        push_amount(info, "flatpak-user", flat_user);
    if (nix_system > 0)
        push_amount(info, "nix-system", nix_system);
    if (nix_user > 0)
        push_amount(info, "nix-user", nix_user);
    if (nix_default > 0)
        push_amount(info, "nix-default", nix_default);
    if (info->n == 0) {
        unsigned uid = (unsigned)getuid();
        char cache_path[1152], tmp_cache[128];
        char *xdg = detect_getenv("XDG_CACHE_HOME");
        char *home = detect_getenv("HOME");
        if (xdg && *xdg)
            snprintf(cache_path, sizeof cache_path, "%s/jefetch/packages.count", xdg);
        else if (home)
            snprintf(cache_path, sizeof cache_path, "%s/.cache/jefetch/packages.count", home);
        else
            snprintf(cache_path, sizeof cache_path, "/tmp/jefetch-packages.%u.cache", uid);
        snprintf(tmp_cache, sizeof tmp_cache, "/tmp/jefetch-packages.%u.cache", uid);
        free(xdg);
        free(home);
        const char *args[] = {"-q", "--requisites", "/nix/var/nix/profiles/system", NULL};
        char *out = detect_run_capture_timeout("nix-store", args, 2500);
        if (out) {
            size_t n = filter_nix_requisites(out);
            free(out);
            char num[32];
            snprintf(num, sizeof num, "%zu", n);
            char *slash = strrchr(cache_path, '/');
            if (slash) {
                *slash = 0;
                char cur[1152];
                size_t cn = 0;
                if (cache_path[0] == '/')
                    cur[cn++] = '/';
                char *save = NULL;
                char *dup = strdup(cache_path + (cache_path[0] == '/' ? 1 : 0));
                char *tok = strtok_r(dup, "/", &save);
                while (tok) {
                    size_t l = strlen(tok);
                    if (cn + l + 1 < sizeof cur) {
                        memcpy(cur + cn, tok, l);
                        cn += l;
                        cur[cn] = 0;
                        mkdir(cur, 0755);
                        cur[cn++] = '/';
                        cur[cn] = 0;
                    }
                    tok = strtok_r(NULL, "/", &save);
                }
                free(dup);
                *slash = '/';
            }
            FILE *f = fopen(cache_path, "w");
            if (f) {
                fwrite(num, 1, strlen(num), f);
                fclose(f);
            }
            f = fopen(tmp_cache, "w");
            if (f) {
                fwrite(num, 1, strlen(num), f);
                fclose(f);
            }
            push_amount(info, "nix", n);
        }
    }
}

void detect_packages(PackagesInfo *out) {
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    static int have = 0;
    static PackagesInfo cached;
    static uint64_t cached_at = 0;
    pthread_mutex_lock(&mu);
    if (have && jf_now_ms() - cached_at < 60000) {
        out->amounts = NULL;
        out->n = 0;
        for (size_t i = 0; i < cached.n; i++)
            push_amount(out, cached.amounts[i].name, cached.amounts[i].count);
        pthread_mutex_unlock(&mu);
        return;
    }
    pthread_mutex_unlock(&mu);
    PackagesInfo info;
    detect_uncached(&info);
    pthread_mutex_lock(&mu);
    detect_packages_free(&cached);
    cached.amounts = NULL;
    cached.n = 0;
    for (size_t i = 0; i < info.n; i++)
        push_amount(&cached, info.amounts[i].name, info.amounts[i].count);
    cached_at = jf_now_ms();
    have = 1;
    *out = info;
    pthread_mutex_unlock(&mu);
}
