#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "detect.h"

static int is_block_device(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    return (st.st_mode & S_IFMT) == S_IFBLK;
}

static int wants_mount(const char *from, const char *mp, const char *fs, int is_block) {
    if (!strcmp(mp, "/"))
        return 1;
    if (!strcmp(from, "none"))
        return 0;
    if (!strcmp(fs, "zfs") || !strcmp(fs, "fuse.sshfs"))
        return 1;
    if (strncmp(from, "/dev/", 5) != 0)
        return 0;
    const char *base = from + 5;
    if (!strncmp(base, "loop", 4) || !strncmp(base, "ram", 3) || !strncmp(base, "fd", 2))
        return 0;
    if (!strcmp(mp, "/boot") || !strcmp(mp, "/boot/efi") || !strcmp(mp, "/efi"))
        return 0;
    return is_block;
}

static void unescape_mp(const char *s, char *out, size_t n) {
    size_t pos = 0;
    while (*s && pos + 1 < n) {
        if (!strncmp(s, "\\040", 4)) {
            out[pos++] = ' ';
            s += 4;
        } else {
            out[pos++] = *s++;
        }
    }
    out[pos] = 0;
}

static void mounts_info(const char *mp, char *fs, size_t fsn, char *from, size_t fromn,
                        char *opts, size_t optsn) {
    fs[0] = 0;
    from[0] = 0;
    opts[0] = 0;
    char *text = detect_read_file("/proc/self/mounts");
    if (!text)
        return;
    char *save = NULL;
    char *line = strtok_r(text, "\n", &save);
    while (line) {
        char *p0 = line;
        char *s1 = strchr(p0, ' ');
        if (!s1) {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        *s1 = 0;
        char *p1 = s1 + 1;
        char *s2 = strchr(p1, ' ');
        if (!s2) {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        *s2 = 0;
        char *p2 = s2 + 1;
        char *s3 = strchr(p2, ' ');
        if (!s3) {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        *s3 = 0;
        char mpoint[1024];
        unescape_mp(p1, mpoint, sizeof mpoint);
        if (!strcmp(mpoint, mp)) {
            snprintf(from, fromn, "%s", p0);
            snprintf(fs, fsn, "%s", p2);
            snprintf(opts, optsn, "%s", s3 + 1);
            break;
        }
        line = strtok_r(NULL, "\n", &save);
    }
    free(text);
}

static void label_for(const char *mount_from, char *out, size_t n) {
    out[0] = 0;
    if (!mount_from[0])
        return;
    DIR *dp = opendir("/dev/disk/by-label");
    if (dp) {
        struct dirent *de;
        while ((de = readdir(dp)) != NULL) {
            char full[512];
            snprintf(full, sizeof full, "/dev/disk/by-label/%s", de->d_name);
            char target[1024];
            ssize_t l = readlink(full, target, sizeof target - 1);
            if (l <= 0)
                continue;
            target[l] = 0;
            const char *t = target;
            if (!strncmp(t, "../../", 6))
                t += 6;
            if (!strcmp(t, mount_from)) {
                snprintf(out, n, "%s", de->d_name);
                closedir(dp);
                return;
            }
            char canon[1024];
            ssize_t c = readlink(full, canon, sizeof canon - 1);
            (void)c;
            char resolved[1024];
            if (realpath(full, resolved) && !strcmp(resolved, mount_from)) {
                snprintf(out, n, "%s", de->d_name);
                closedir(dp);
                return;
            }
        }
        closedir(dp);
    }
    const char *b = strrchr(mount_from, '/');
    snprintf(out, n, "%s", b ? b + 1 : mount_from);
}

static int stat_one(const char *mp, DiskInfo *out) {
    struct statvfs st;
    if (statvfs(mp, &st) != 0)
        return 0;
    unsigned long long frsize = st.f_frsize ? st.f_frsize : 1;
    unsigned long long total = (unsigned long long)st.f_blocks * frsize;
    unsigned long long freeb = (unsigned long long)st.f_bfree * frsize;
    unsigned long long avail = (unsigned long long)st.f_bavail * frsize;
    memset(out, 0, sizeof *out);
    snprintf(out->mountpoint, sizeof out->mountpoint, "%s", mp);
    out->total = total;
    out->used = total >= freeb ? total - freeb : 0;
    out->available = avail;
    mounts_info(mp, out->filesystem, sizeof out->filesystem, out->mount_from,
                sizeof out->mount_from, out->options, sizeof out->options);
    label_for(out->mount_from, out->name, sizeof out->name);
    return 1;
}

DiskInfo *detect_disk(const char *const *folders, size_t nfolders, size_t *n) {
    DiskInfo *out = NULL;
    size_t m = 0;
    *n = 0;
    if (nfolders > 0) {
        for (size_t i = 0; i < nfolders; i++) {
            DiskInfo di;
            if (stat_one(folders[i], &di)) {
                out = realloc(out, (m + 1) * sizeof(DiskInfo));
                out[m++] = di;
            }
        }
        *n = m;
        return out;
    }
    char *text = detect_read_file("/proc/self/mounts");
    if (!text)
        return NULL;
    char seen[64][1024];
    size_t nseen = 0;
    char *save = NULL;
    char *line = strtok_r(text, "\n", &save);
    while (line) {
        char *p0 = line;
        char *s1 = strchr(p0, ' ');
        if (!s1) {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        *s1 = 0;
        char *p1 = s1 + 1;
        char *s2 = strchr(p1, ' ');
        if (!s2) {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        *s2 = 0;
        char *fs = s2 + 1;
        char *se = strchr(fs, ' ');
        if (se)
            *se = 0;
        char mp[1024];
        unescape_mp(p1, mp, sizeof mp);
        int dup = 0;
        for (size_t i = 0; i < nseen; i++) {
            if (!strcmp(seen[i], p0)) {
                dup = 1;
                break;
            }
        }
        if (!dup && wants_mount(p0, mp, fs, is_block_device(p0))) {
            DiskInfo di;
            if (stat_one(mp, &di)) {
                if (nseen < 64) {
                    snprintf(seen[nseen], sizeof seen[0], "%s", p0);
                    nseen++;
                }
                out = realloc(out, (m + 1) * sizeof(DiskInfo));
                out[m++] = di;
            }
        }
        line = strtok_r(NULL, "\n", &save);
    }
    free(text);
    *n = m;
    return out;
}

void detect_disk_free(DiskInfo *d, size_t n) {
    (void)n;
    free(d);
}
