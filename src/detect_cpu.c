#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "detect.h"

static int has_flag(const char *flags, const char *f) {
    size_t fl = strlen(f);
    const char *p = flags;
    while ((p = strstr(p, f)) != NULL) {
        if ((p == flags || p[-1] == ' ' || p[-1] == '\t') &&
            (p[fl] == 0 || p[fl] == ' ' || p[fl] == '\t' || p[fl] == '\n'))
            return 1;
        p += fl;
    }
    return 0;
}

static int has_flag_ci(const char *flags, const char *f) {
    char a[256], b[64];
    snprintf(a, sizeof a, "%s", flags);
    for (char *p = a; *p; p++) {
        if (*p >= 'A' && *p <= 'Z')
            *p += 32;
    }
    snprintf(b, sizeof b, "%s", f);
    for (char *p = b; *p; p++) {
        if (*p >= 'A' && *p <= 'Z')
            *p += 32;
    }
    return has_flag(a, b);
}

static void march_from_text(const char *text, char *out, size_t n) {
    char flags[8192] = "", features[4096] = "", arch[64] = "";
    char *dup = strdup(text);
    char *save = NULL;
    char *line = strtok_r(dup, "\n", &save);
    while (line) {
        char *t = line;
        while (*t == ' ' || *t == '\t')
            t++;
        char *colon = strchr(t, ':');
        if (colon) {
            *colon = 0;
            char *k = t;
            char *ke = k + strlen(k);
            while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t'))
                *--ke = 0;
            char *v = colon + 1;
            while (*v == ' ' || *v == '\t')
                v++;
            if (!strcmp(k, "flags") && !flags[0])
                snprintf(flags, sizeof flags, "%s", v);
            else if ((!strcmp(k, "Features") || !strcmp(k, "features")) && !features[0])
                snprintf(features, sizeof features, "%s", v);
            else if (!strcmp(k, "CPU architecture") && !arch[0])
                snprintf(arch, sizeof arch, "%s", v);
        }
        line = strtok_r(NULL, "\n", &save);
    }
    free(dup);
    out[0] = 0;
    if (flags[0]) {
        if (has_flag(flags, "avx512f") && has_flag(flags, "avx512bw") &&
            has_flag(flags, "avx512cd") && has_flag(flags, "avx512dq") &&
            has_flag(flags, "avx512vl")) {
            snprintf(out, n, "x86_64-v4");
            return;
        }
        if (has_flag(flags, "avx2") && has_flag(flags, "bmi1") && has_flag(flags, "bmi2") &&
            has_flag(flags, "f16c") && has_flag(flags, "fma") && has_flag(flags, "lzcnt") &&
            has_flag(flags, "movbe") && has_flag(flags, "osxsave")) {
            snprintf(out, n, "x86_64-v3");
            return;
        }
        if (has_flag(flags, "cx16") && has_flag(flags, "lahf_lm") &&
            has_flag(flags, "popcnt") && has_flag(flags, "sse4_1") &&
            has_flag(flags, "sse4_2") && has_flag(flags, "ssse3")) {
            snprintf(out, n, "x86_64-v2");
            return;
        }
        if (has_flag(flags, "cmpxchg8b") && has_flag(flags, "fxsr") &&
            has_flag(flags, "mmx") && has_flag(flags, "sse") && has_flag(flags, "sse2")) {
            snprintf(out, n, "x86_64");
            return;
        }
    }
    if (features[0] || arch[0]) {
        char al[64];
        snprintf(al, sizeof al, "%s", arch);
        char *p = al;
        while (*p == ' ' || *p == '\t')
            p++;
        int is64 = has_flag_ci(features, "asimd") ||
                   (has_flag_ci(features, "fp") &&
                    (strstr(arch, "8") || strstr(arch, "aarch64") || strstr(arch, "AARCH64")));
        {
            int fp = has_flag_ci(features, "fp");
            char al2[64];
            snprintf(al2, sizeof al2, "%s", p);
            for (char *q = al2; *q; q++) {
                if (*q >= 'A' && *q <= 'Z')
                    *q += 32;
            }
            if (!is64)
                is64 = fp && (!strcmp(al2, "8") || strstr(al2, "aarch64") != NULL);
        }
        if (!is64) {
            if (has_flag_ci(features, "half") || has_flag_ci(features, "thumb") ||
                has_flag_ci(features, "edsp") || has_flag_ci(features, "neon") ||
                has_flag_ci(features, "tls")) {
                snprintf(out, n, "armv7-a");
                return;
            }
            if (arch[0]) {
                char *end;
                unsigned long v = strtoul(p, &end, 10);
                if (end != p && *end == 0) {
                    if (v == 8)
                        snprintf(out, n, "armv8-a");
                    else if (v == 7)
                        snprintf(out, n, "armv7-a");
                    else if (v == 6)
                        snprintf(out, n, "armv6");
                    else if (v == 5)
                        snprintf(out, n, "armv5");
                    else
                        snprintf(out, n, "armv%lu-a", v);
                    return;
                }
                char ll[64];
                snprintf(ll, sizeof ll, "%.63s", p);
                for (char *q = ll; *q; q++) {
                    if (*q >= 'A' && *q <= 'Z')
                        *q += 32;
                }
                if (strstr(ll, "aarch64")) {
                    snprintf(out, n, "armv8-a");
                    return;
                }
            }
            return;
        }
        if (has_flag_ci(features, "sve2") || has_flag_ci(features, "sve2p1") ||
            has_flag_ci(features, "mops") || has_flag_ci(features, "hbc")) {
            snprintf(out, n, "armv9-a");
            return;
        }
        if (has_flag_ci(features, "sve")) {
            snprintf(out, n, "armv8.2-a+sve");
            return;
        }
        if (has_flag_ci(features, "asimddp") || has_flag_ci(features, "sha3") ||
            has_flag_ci(features, "sm3") || has_flag_ci(features, "sm4")) {
            snprintf(out, n, "armv8.2-a");
            return;
        }
        if (has_flag_ci(features, "asimdrdm") || has_flag_ci(features, "lrcpc") ||
            has_flag_ci(features, "dcpop") || has_flag_ci(features, "sha512")) {
            snprintf(out, n, "armv8.1-a");
            return;
        }
        if (arch[0]) {
            if (!strcmp(p, "8")) {
                snprintf(out, n, "armv8-a");
                return;
            }
            {
                char ll[64];
                snprintf(ll, sizeof ll, "%.63s", p);
                for (char *q = ll; *q; q++) {
                    if (*q >= 'A' && *q <= 'Z')
                        *q += 32;
                }
                if (strstr(ll, "aarch64")) {
                    snprintf(out, n, "armv8-a");
                    return;
                }
                char *end;
                unsigned long v = strtoul(p, &end, 10);
                if (end != p && *end == 0) {
                    if (v >= 9)
                        snprintf(out, n, "armv9-a");
                    else if (v == 8)
                        snprintf(out, n, "armv8-a");
                    return;
                }
            }
        }
        snprintf(out, n, "armv8-a");
    }
}

void detect_cpu_march(char *out, size_t n) {
    char *text = detect_read_file("/proc/cpuinfo");
    out[0] = 0;
    if (text) {
        march_from_text(text, out, n);
        free(text);
    }
}

unsigned long long detect_numa_nodes(void) {
    DIR *dp = opendir("/sys/devices/system/node");
    if (!dp)
        return 1;
    unsigned long long n = 0;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        n++;
    }
    closedir(dp);
    return n;
}

static size_t count_cpus_sysfs(void) {
    DIR *dp = opendir("/sys/devices/system/cpu");
    size_t n = 0;
    if (!dp)
        return 0;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        const char *nm = de->d_name;
        if (strlen(nm) > 3 && !strncmp(nm, "cpu", 3)) {
            int ok = 1;
            for (size_t i = 3; nm[i]; i++) {
                if (nm[i] < '0' || nm[i] > '9') {
                    ok = 0;
                    break;
                }
            }
            if (ok)
                n++;
        }
    }
    closedir(dp);
    return n;
}

static void arm_vendor_name(const char *imp, char *out, size_t n) {
    char l[32];
    snprintf(l, sizeof l, "%s", imp);
    char *s = l;
    while (*s == ' ' || *s == '\t')
        s++;
    char ll[32];
    size_t i = 0;
    while (s[i] && i + 1 < sizeof ll) {
        char c = s[i];
        ll[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    ll[i] = 0;
    const char *v = "";
    if (!strcmp(ll, "0x41"))
        v = "ARM";
    else if (!strcmp(ll, "0x42"))
        v = "Broadcom";
    else if (!strcmp(ll, "0x43"))
        v = "Cavium";
    else if (!strcmp(ll, "0x44"))
        v = "DEC";
    else if (!strcmp(ll, "0x46"))
        v = "Fujitsu";
    else if (!strcmp(ll, "0x48"))
        v = "HiSilicon";
    else if (!strcmp(ll, "0x49"))
        v = "Infineon";
    else if (!strcmp(ll, "0x4d"))
        v = "Motorola";
    else if (!strcmp(ll, "0x4e"))
        v = "NVIDIA";
    else if (!strcmp(ll, "0x50"))
        v = "APM";
    else if (!strcmp(ll, "0x51"))
        v = "Qualcomm";
    else if (!strcmp(ll, "0x53"))
        v = "Samsung";
    else if (!strcmp(ll, "0x56"))
        v = "Marvell";
    else if (!strcmp(ll, "0x61"))
        v = "Apple";
    else if (!strcmp(ll, "0x69"))
        v = "Intel";
    else if (!strcmp(ll, "0xc0"))
        v = "Ampere";
    snprintf(out, n, "%s", v);
}

static int arm_part_name(const char *imp, const char *part, char *out, size_t n) {
    char il[32], pl[32];
    snprintf(il, sizeof il, "%s", imp);
    for (char *p = il; *p; p++) {
        if (*p >= 'A' && *p <= 'Z')
            *p += 32;
    }
    char *s = il;
    while (*s == ' ' || *s == '\t')
        s++;
    snprintf(pl, sizeof pl, "%.31s", part);
    for (char *p = pl; *p; p++) {
        if (*p >= 'A' && *p <= 'Z')
            *p += 32;
    }
    s = pl;
    while (*s == ' ' || *s == '\t')
        s++;
    const char *h = s;
    if (!strncmp(h, "0x", 2))
        h += 2;
    while (*h == '0')
        h++;
    char *end;
    unsigned long num = strtoul(*h ? h : "0", &end, 16);
    if (end == h) {
        num = strtoul(s, &end, 16);
        if (end == s)
            return 0;
    }
    const char *v = NULL;
    if (!strcmp(il, "0x41")) {
        switch (num) {
        case 0xd00: v = "Cortex-A32"; break;
        case 0xd02: v = "Cortex-A34"; break;
        case 0xd03: v = "Cortex-A53"; break;
        case 0xd04: v = "Cortex-A35"; break;
        case 0xd05: v = "Cortex-A55"; break;
        case 0xd06: v = "Cortex-A65"; break;
        case 0xd07: v = "Cortex-A57"; break;
        case 0xd08: v = "Cortex-A72"; break;
        case 0xd09: v = "Cortex-A73"; break;
        case 0xd0a: v = "Cortex-A75"; break;
        case 0xd0b: v = "Cortex-A76"; break;
        case 0xd0c: v = "Neoverse-N1"; break;
        case 0xd0d: v = "Cortex-A77"; break;
        case 0xd0e: v = "Cortex-A76AE"; break;
        case 0xd13: v = "Cortex-R52"; break;
        case 0xd20: v = "Cortex-M23"; break;
        case 0xd21: v = "Cortex-M33"; break;
        case 0xd22: v = "Cortex-M55"; break;
        case 0xd40: v = "Neoverse-V1"; break;
        case 0xd41: v = "Cortex-A78"; break;
        case 0xd44: v = "Cortex-X1"; break;
        case 0xd46: v = "Cortex-A510"; break;
        case 0xd47: v = "Cortex-A710"; break;
        case 0xd48: v = "Cortex-X2"; break;
        case 0xd49: v = "Neoverse-N2"; break;
        case 0xd4a: v = "Neoverse-E1"; break;
        case 0xd4b: v = "Cortex-A78C"; break;
        case 0xd4d: v = "Cortex-A715"; break;
        case 0xd4e: v = "Cortex-X3"; break;
        case 0xd4f: v = "Neoverse-V2"; break;
        case 0xc05: v = "Cortex-A5"; break;
        case 0xc07: v = "Cortex-A7"; break;
        case 0xc08: v = "Cortex-A8"; break;
        case 0xc09: v = "Cortex-A9"; break;
        case 0xc0d: v = "Cortex-A12"; break;
        case 0xc0e: v = "Cortex-A17"; break;
        case 0xc0f: v = "Cortex-A15"; break;
        default: return 0;
        }
    } else if (!strcmp(il, "0x51")) {
        switch (num) {
        case 0x001: v = "Oryon"; break;
        case 0x800: v = "Kryo"; break;
        case 0x801: v = "Kryo 2xx"; break;
        case 0x802:
        case 0x803: v = "Kryo 385"; break;
        case 0x804:
        case 0x805: v = "Kryo 485"; break;
        case 0xc00: v = "Kryo 585"; break;
        default: return 0;
        }
    } else if (!strcmp(il, "0x61")) {
        switch (num) {
        case 0x020:
        case 0x021:
        case 0x022:
        case 0x023: v = "M1"; break;
        case 0x030:
        case 0x031:
        case 0x032:
        case 0x033: v = "M2"; break;
        case 0x034:
        case 0x035:
        case 0x036: v = "M3"; break;
        default: v = "Silicon"; break;
        }
    } else {
        return 0;
    }
    snprintf(out, n, "%s", v);
    return 1;
}

static void dt_model(char *out, size_t n) {
    static const char *paths[] = {"/proc/device-tree/model",
                                  "/sys/firmware/devicetree/base/model"};
    out[0] = 0;
    for (int i = 0; i < 2; i++) {
        FILE *f = fopen(paths[i], "r");
        if (!f)
            continue;
        size_t m = fread(out, 1, n - 1, f);
        fclose(f);
        size_t k = 0;
        while (k < m && out[k])
            k++;
        out[k] = 0;
        char *s = out;
        while (*s == ' ' || *s == '\t')
            s++;
        char *e = s + strlen(s);
        while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
            *--e = 0;
        if (*s) {
            if (s != out)
                memmove(out, s, strlen(s) + 1);
            return;
        }
    }
    out[0] = 0;
}

static unsigned long long max_freq_sysfs(const char *file) {
    unsigned long long best = 0;
    size_t ncpus = count_cpus_sysfs();
    if (ncpus < 1)
        ncpus = 1;
    for (size_t cpu = 0; cpu < ncpus; cpu++) {
        char p[128];
        snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%zu/cpufreq/%s", cpu, file);
        char *s = detect_read_file(p);
        if (s) {
            unsigned long long khz = strtoull(s, NULL, 10);
            if (khz / 1000 > best)
                best = khz / 1000;
            free(s);
        }
        if (cpu > 4096)
            break;
    }
    return best;
}

void detect_cpu(CpuInfo *out) {
    char *text = detect_read_file("/proc/cpuinfo");
    if (!text)
        text = strdup("");
    memset(out, 0, sizeof *out);
    char (*cores)[192] = NULL;
    size_t ncores = 0, ccores = 0;
    char (*phys)[64] = NULL;
    size_t nphys = 0, cphys = 0;
    char phys_id[64] = "", core_id[64] = "";
    unsigned long long *mhz = NULL;
    size_t nmhz = 0;
    char arm_model_line[256] = "", arm_hardware[256] = "", arm_implementer[64] = "";
    char arm_parts[16][64];
    size_t narm_parts = 0;
    char arm_arch[64] = "";
    char *dup = strdup(text);
    char *cur = dup;
    while (1) {
        char *nl = strchr(cur, '\n');
        char linebuf[1024];
        if (nl) {
            size_t ll = (size_t)(nl - cur);
            if (ll >= sizeof linebuf)
                ll = sizeof linebuf - 1;
            memcpy(linebuf, cur, ll);
            linebuf[ll] = 0;
            cur = nl + 1;
        } else {
            snprintf(linebuf, sizeof linebuf, "%s", cur);
            cur += strlen(cur);
        }
        char *t = linebuf;
        while (*t == ' ' || *t == '\t')
            t++;
        int is_last = nl == NULL;
        if (!*t) {
            if (phys_id[0] || core_id[0]) {
                char key[192];
                snprintf(key, sizeof key, "%s|%s", phys_id, core_id);
                int seen = 0;
                for (size_t i = 0; i < ncores; i++) {
                    if (!strcmp(cores[i], key)) {
                        seen = 1;
                        break;
                    }
                }
                if (!seen) {
                    if (ncores >= ccores) {
                        ccores = ccores ? ccores * 2 : 16;
                        cores = realloc(cores, ccores * 192);
                    }
                    snprintf(cores[ncores++], 192, "%s", key);
                }
                phys_id[0] = 0;
                core_id[0] = 0;
            }
            if (is_last)
                break;
            continue;
        }
        char *colon = strchr(t, ':');
        if (!colon) {
            if (is_last)
                break;
            continue;
        }
        *colon = 0;
        char *k = t;
        char *ke = k + strlen(k);
        while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t'))
            *--ke = 0;
        char *v = colon + 1;
        while (*v == ' ' || *v == '\t')
            v++;
        if (!strcmp(k, "processor")) {
            out->logical_cores++;
        } else if (!strcmp(k, "model name")) {
            if (!out->model[0])
                snprintf(out->model, sizeof out->model, "%s", v);
        } else if (!strcmp(k, "vendor_id")) {
            if (!out->vendor[0])
                snprintf(out->vendor, sizeof out->vendor, "%s", v);
        } else if (!strcmp(k, "physical id")) {
            snprintf(phys_id, sizeof phys_id, "%s", v);
            if (v[0]) {
                int seen = 0;
                for (size_t i = 0; i < nphys; i++) {
                    if (!strcmp(phys[i], v)) {
                        seen = 1;
                        break;
                    }
                }
                if (!seen) {
                    if (nphys >= cphys) {
                        cphys = cphys ? cphys * 2 : 8;
                        phys = realloc(phys, cphys * 64);
                    }
                    snprintf(phys[nphys++], 64, "%s", v);
                }
            }
        } else if (!strcmp(k, "core id")) {
            snprintf(core_id, sizeof core_id, "%s", v);
        } else if (!strcmp(k, "cpu MHz")) {
            mhz = realloc(mhz, (nmhz + 1) * sizeof(unsigned long long));
            mhz[nmhz++] = (unsigned long long)strtod(v, NULL);
        } else if (!strcmp(k, "Model")) {
            if (!arm_model_line[0] && v[0])
                snprintf(arm_model_line, sizeof arm_model_line, "%s", v);
        } else if (!strcmp(k, "Hardware")) {
            if (!arm_hardware[0] && v[0])
                snprintf(arm_hardware, sizeof arm_hardware, "%s", v);
        } else if (!strcmp(k, "CPU implementer")) {
            if (!arm_implementer[0] && v[0])
                snprintf(arm_implementer, sizeof arm_implementer, "%s", v);
        } else if (!strcmp(k, "CPU part")) {
            if (v[0] && narm_parts < 16) {
                int seen = 0;
                for (size_t i = 0; i < narm_parts; i++) {
                    if (!strcmp(arm_parts[i], v)) {
                        seen = 1;
                        break;
                    }
                }
                if (!seen)
                    snprintf(arm_parts[narm_parts++], 64, "%s", v);
            }
        } else if (!strcmp(k, "CPU architecture")) {
            if (!arm_arch[0] && v[0])
                snprintf(arm_arch, sizeof arm_arch, "%s", v);
        }
        if (is_last)
            break;
    }
    free(dup);
    if (phys_id[0] || core_id[0]) {
        char key[192];
        snprintf(key, sizeof key, "%s|%s", phys_id, core_id);
        int seen = 0;
        for (size_t i = 0; i < ncores; i++) {
            if (!strcmp(cores[i], key)) {
                seen = 1;
                break;
            }
        }
        if (!seen) {
            if (ncores >= ccores) {
                ccores = ccores ? ccores * 2 : 16;
                cores = realloc(cores, ccores * 192);
            }
            snprintf(cores[ncores++], 192, "%s", key);
        }
    }
    if (out->logical_cores == 0)
        out->logical_cores = count_cpus_sysfs();
    out->packages = nphys;
    if (out->packages == 0 && out->logical_cores > 0)
        out->packages = 1;
    size_t pcnt = 0;
    for (size_t i = 0; i < ncores; i++) {
        if (strcmp(cores[i], "|"))
            pcnt++;
    }
    out->physical_cores = pcnt;
    if (out->physical_cores == 0) {
        DIR *dp = opendir("/sys/devices/system/cpu");
        if (dp) {
            struct dirent *de;
            char (*set)[192] = NULL;
            size_t nset = 0, cset = 0;
            int any = 0;
            while ((de = readdir(dp)) != NULL) {
                size_t dnl = strlen(de->d_name);
                if (dnl >= 24)
                    continue;
                char nm[24];
                memcpy(nm, de->d_name, dnl + 1);
                if (strlen(nm) <= 3 || strncmp(nm, "cpu", 3) != 0)
                    continue;
                int ok = 1;
                for (size_t i = 3; nm[i]; i++) {
                    if (nm[i] < '0' || nm[i] > '9') {
                        ok = 0;
                        break;
                    }
                }
                if (!ok)
                    continue;
                char bp[160], cp2[160];
                snprintf(bp, sizeof bp, "/sys/devices/system/cpu/%s/topology/physical_package_id", nm);
                snprintf(cp2, sizeof cp2, "/sys/devices/system/cpu/%s/topology/core_id", nm);
                char *pkg = detect_read_file(bp);
                char *core = detect_read_file(cp2);
                char pk[64] = "", cr[64] = "";
                if (pkg) {
                    char *e = pkg + strlen(pkg);
                    while (e > pkg && (e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'))
                        *--e = 0;
                    snprintf(pk, sizeof pk, "%s", pkg);
                    free(pkg);
                }
                if (core) {
                    char *e = core + strlen(core);
                    while (e > core && (e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'))
                        *--e = 0;
                    snprintf(cr, sizeof cr, "%s", core);
                    free(core);
                }
                if (!pk[0] && !cr[0])
                    continue;
                any = 1;
                char key[192];
                snprintf(key, sizeof key, "%s|%s", pk, cr);
                int seen = 0;
                for (size_t i = 0; i < nset; i++) {
                    if (!strcmp(set[i], key)) {
                        seen = 1;
                        break;
                    }
                }
                if (!seen) {
                    if (nset >= cset) {
                        cset = cset ? cset * 2 : 16;
                        set = realloc(set, cset * 192);
                    }
                    snprintf(set[nset++], 192, "%s", key);
                }
            }
            closedir(dp);
            if (any)
                out->physical_cores = nset;
            free(set);
        }
    }
    if (out->physical_cores == 0)
        out->physical_cores = out->logical_cores;
    if (!out->model[0]) {
        if (arm_model_line[0]) {
            snprintf(out->model, sizeof out->model, "%s", arm_model_line);
        } else if (arm_implementer[0]) {
            char vendor[64];
            arm_vendor_name(arm_implementer, vendor, sizeof vendor);
            char names[16][64];
            size_t nnames = 0;
            for (size_t i = 0; i < narm_parts; i++) {
                char nm[64];
                if (arm_part_name(arm_implementer, arm_parts[i], nm, sizeof nm)) {
                    int seen = 0;
                    for (size_t j = 0; j < nnames; j++) {
                        if (!strcmp(names[j], nm)) {
                            seen = 1;
                            break;
                        }
                    }
                    if (!seen && nnames < 16)
                        snprintf(names[nnames++], 64, "%s", nm);
                }
            }
            if (nnames > 0) {
                char cores[1024] = "";
                for (size_t i = 0; i < nnames; i++) {
                    if (i > 0)
                        strcat(cores, " + ");
                    strcat(cores, names[i]);
                }
                if (!vendor[0] || !strcmp(vendor, "ARM")) {
                    if (!strncmp(cores, "Cortex", 6) || !strncmp(cores, "Neoverse", 8))
                        snprintf(out->model, sizeof out->model, "ARM %.250s", cores);
                    else
                        snprintf(out->model, sizeof out->model, "%.255s", cores);
                } else if (!strcmp(vendor, "Apple")) {
                    if (!strcmp(cores, "Silicon"))
                        snprintf(out->model, sizeof out->model, "Apple Silicon");
                    else
                        snprintf(out->model, sizeof out->model, "Apple %.249s", cores);
                } else if (!strncmp(cores, vendor, strlen(vendor))) {
                    snprintf(out->model, sizeof out->model, "%.255s", cores);
                } else {
                    snprintf(out->model, sizeof out->model, "%.16s %.238s", vendor, cores);
                }
            } else if (vendor[0]) {
                if (arm_arch[0]) {
                    char *end;
                    unsigned long av = strtoul(arm_arch, &end, 10);
                    if (end != arm_arch && *end == 0)
                        snprintf(out->model, sizeof out->model, "%s ARMv%lu Processor", vendor, av);
                    else {
                        char ll[64];
                        snprintf(ll, sizeof ll, "%s", arm_arch);
                        for (char *p = ll; *p; p++) {
                            if (*p >= 'A' && *p <= 'Z')
                                *p += 32;
                        }
                        if (strstr(ll, "aarch64"))
                            snprintf(out->model, sizeof out->model, "%s ARMv8 Processor", vendor);
                        else
                            snprintf(out->model, sizeof out->model, "%s Processor", vendor);
                    }
                } else {
                    snprintf(out->model, sizeof out->model, "%s Processor", vendor);
                }
            }
        }
        if (!out->model[0] && arm_hardware[0])
            snprintf(out->model, sizeof out->model, "%s", arm_hardware);
        if (!out->model[0])
            dt_model(out->model, sizeof out->model);
    }
    if (!out->vendor[0] && arm_implementer[0]) {
        char vendor[64];
        arm_vendor_name(arm_implementer, vendor, sizeof vendor);
        if (vendor[0])
            snprintf(out->vendor, sizeof out->vendor, "%s", vendor);
    }
    if (!out->vendor[0] && out->model[0]) {
        char l[256];
        snprintf(l, sizeof l, "%s", out->model);
        for (char *p = l; *p; p++) {
            if (*p >= 'A' && *p <= 'Z')
                *p += 32;
        }
        static const char *pfx[][2] = {
            {"apple ", "Apple"}, {"qualcomm ", "Qualcomm"}, {"samsung ", "Samsung"},
            {"broadcom ", "Broadcom"}, {"nvidia ", "NVIDIA"}, {"ampere ", "Ampere"},
            {"cavium ", "Cavium"}, {"fujitsu ", "Fujitsu"}, {"huawei ", "Huawei"},
            {"mediatek ", "MediaTek"}, {"rockchip ", "Rockchip"}
        };
        for (int i = 0; i < 11; i++) {
            if (!strncmp(l, pfx[i][0], strlen(pfx[i][0]))) {
                snprintf(out->vendor, sizeof out->vendor, "%s", pfx[i][1]);
                break;
            }
        }
    }
    out->freq_max_mhz = max_freq_sysfs("cpuinfo_max_freq");
    out->freq_cur_mhz = max_freq_sysfs("scaling_cur_freq");
    if (out->freq_cur_mhz == 0 && nmhz > 0)
        out->freq_cur_mhz = mhz[0];
    {
        char *hl = detect_read_file("/sys/devices/system/cpu/hybrid_cpu_list");
        if (hl) {
            char *e = hl + strlen(hl);
            while (e > hl && (e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'))
                *--e = 0;
            char (*hset)[64] = NULL;
            size_t nhset = 0, chset = 0;
            char *save = NULL;
            char *part = strtok_r(hl, ",", &save);
            while (part) {
                char *dash = strchr(part, '-');
                unsigned a, b;
                if (dash) {
                    *dash = 0;
                    a = (unsigned)strtoul(part, NULL, 10);
                    b = (unsigned)strtoul(dash + 1, NULL, 10);
                } else if (*part) {
                    a = b = (unsigned)strtoul(part, NULL, 10);
                } else {
                    part = strtok_r(NULL, ",", &save);
                    continue;
                }
                for (unsigned cpu = a; cpu <= b; cpu++) {
                    char p[160];
                    snprintf(p, sizeof p,
                             "/sys/devices/system/cpu/cpu%u/topology/core_id", cpu);
                    char *t = detect_read_file(p);
                    if (!t)
                        continue;
                    char *te = t + strlen(t);
                    while (te > t && (te[-1] == '\n' || te[-1] == ' ' || te[-1] == '\t'))
                        *--te = 0;
                    int seen = 0;
                    for (size_t i = 0; i < nhset; i++) {
                        if (!strcmp(hset[i], t)) {
                            seen = 1;
                            break;
                        }
                    }
                    if (!seen) {
                        if (nhset >= chset) {
                            chset = chset ? chset * 2 : 16;
                            hset = realloc(hset, chset * 64);
                        }
                        snprintf(hset[nhset++], 64, "%s", t);
                    }
                    free(t);
                    if (cpu == b)
                        break;
                }
                part = strtok_r(NULL, ",", &save);
            }
            free(hl);
            if (nhset > 0) {
                out->has_pe_cores = 1;
                out->pe_cores = nhset;
                if (out->physical_cores < nhset)
                    out->physical_cores = nhset;
                out->has_ee_cores = 1;
                out->ee_cores = out->physical_cores - nhset;
            }
            free(hset);
        }
    }
    if (out->freq_max_mhz == 0) {
        out->freq_max_mhz = out->freq_cur_mhz;
        if (out->freq_max_mhz == 0 && nmhz > 0)
            out->freq_max_mhz = mhz[0];
    }
    free(cores);
    free(phys);
    free(mhz);
    free(text);
}
