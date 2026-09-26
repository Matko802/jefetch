#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include "common.h"
#include "detect.h"

static const char *SHELLS[] = {
    "sh", "bash", "zsh", "fish", "csh", "tcsh", "ksh", "dash", "ash",
    "posh", "elvish", "oil", "nushell", "pwsh", "yash", "busybox",
    "login", "systemd", "init", "sshd", "sudo", "doas", "su",
    "tmux", "screen", "byobu", "git", "ssh", NULL
};

static void lower_str(const char *s, char *out, size_t n) {
    size_t i = 0;
    while (s[i] && i + 1 < n) {
        char c = s[i];
        out[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    out[i] = 0;
}

static void pretty_font(const char *family, const char *size, char *out, size_t n) {
    char f[256], s[64];
    snprintf(f, sizeof f, "%s", family);
    char *fs = f;
    while (*fs == ' ' || *fs == '\t')
        fs++;
    char *fe = fs + strlen(fs);
    while (fe > fs && (fe[-1] == ' ' || fe[-1] == '\t'))
        *--fe = 0;
    snprintf(s, sizeof s, "%s", size);
    char *sp = s;
    while (*sp == ' ' || *sp == '\t')
        sp++;
    char *se = sp + strlen(sp);
    while (se > sp && (se[-1] == ' ' || se[-1] == '\t'))
        *--se = 0;
    size_t l = strlen(sp);
    while (l > 2 && sp[l - 2] == '.' && sp[l - 1] == '0') {
        sp[l - 2] = 0;
        l -= 2;
    }
    if (!sp[0] || !strcmp(sp, "0"))
        snprintf(out, n, "%s", fs);
    else
        snprintf(out, n, "%s (%spt)", fs, sp);
}

static void version_cache_path(const char *bin, char *out, size_t n) {
    char *xdg = detect_getenv("XDG_CACHE_HOME");
    char *home = detect_getenv("HOME");
    if (xdg && *xdg)
        snprintf(out, n, "%s/jefetch/terminal-%s.version", xdg, bin);
    else if (home)
        snprintf(out, n, "%s/.cache/jefetch/terminal-%s.version", home, bin);
    else
        snprintf(out, n, "/tmp/jefetch-terminal-%s.version", bin);
    free(xdg);
    free(home);
}

static int read_version_cache(const char *path, unsigned ttl_secs, char *out, size_t n) {
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    long age = (long)now.tv_sec - (long)st.st_mtime;
    if (age < 0 || (unsigned)age > ttl_secs)
        return 0;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    char bin[128] = "";
    if (!strncmp(base, "terminal-", 9)) {
        size_t l = strlen(base + 9);
        if (l > 8)
            l -= 8;
        if (l >= sizeof bin)
            l = sizeof bin - 1;
        memcpy(bin, base + 9, l);
        bin[l] = 0;
    }
    if (bin[0]) {
        char sysbin[160];
        snprintf(sysbin, sizeof sysbin, "/run/current-system/sw/bin/%s", bin);
        struct stat bst;
        if (stat(sysbin, &bst) == 0 && bst.st_mtime > st.st_mtime)
            return 0;
    }
    char *t = detect_read_file(path);
    if (!t)
        return 0;
    char *s = t;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
        s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r'))
        *--e = 0;
    int ok = *s != 0;
    if (ok)
        snprintf(out, n, "%s", s);
    free(t);
    return ok;
}

static void write_version_cache(const char *path, const char *ver) {
    char *slash = strrchr(path, '/');
    if (slash) {
        size_t dl = (size_t)(slash - path);
        char dir[1024];
        if (dl >= sizeof dir)
            dl = sizeof dir - 1;
        memcpy(dir, path, dl);
        dir[dl] = 0;
        char cur[1024];
        size_t cn = 0;
        if (dir[0] == '/')
            cur[cn++] = '/';
        char *save = NULL;
        char *dup = strdup(dir + (dir[0] == '/' ? 1 : 0));
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
    }
    FILE *f = fopen(path, "w");
    if (f) {
        fwrite(ver, 1, strlen(ver), f);
        fclose(f);
    }
}

static void kitty_version(char *out, size_t n) {
    out[0] = 0;
    char cp[1024];
    version_cache_path("kitty", cp, sizeof cp);
    char cached[128];
    if (read_version_cache(cp, 3600 * 24, cached, sizeof cached)) {
        snprintf(out, n, "%s", cached);
        return;
    }
    const char *args[] = {"--version", NULL};
    char *o = detect_run_capture_timeout("kitty", args, 400);
    if (!o)
        return;
    char *save = NULL;
    char *first = strtok_r(o, " \t\n\r", &save);
    char *ver = strtok_r(NULL, " \t\n\r", &save);
    if (first && ver) {
        char fl[64];
        lower_str(first, fl, sizeof fl);
        if (!strcmp(fl, "kitty") && ver[0] >= '0' && ver[0] <= '9') {
            write_version_cache(cp, ver);
            snprintf(out, n, "%s", ver);
        }
    }
    free(o);
}

static void generic_version(const char *name, char *out, size_t n) {
    out[0] = 0;
    char bin[64];
    char l[64];
    lower_str(name, l, sizeof l);
    if (!strcmp(l, "alacritty"))
        strcpy(bin, "alacritty");
    else if (!strcmp(l, "ghostty"))
        strcpy(bin, "ghostty");
    else if (!strcmp(l, "foot"))
        strcpy(bin, "foot");
    else if (!strcmp(l, "wezterm"))
        strcpy(bin, "wezterm");
    else
        return;
    char cp[1024];
    version_cache_path(bin, cp, sizeof cp);
    char cached[128];
    if (read_version_cache(cp, 3600 * 24, cached, sizeof cached)) {
        snprintf(out, n, "%s", cached);
        return;
    }
    const char *args[] = {"--version", NULL};
    char *o = detect_run_capture_timeout(bin, args, 400);
    if (!o)
        return;
    char *save = NULL;
    char *tok = strtok_r(o, " \t\n\r", &save);
    while (tok) {
        if (tok[0] >= '0' && tok[0] <= '9' && strchr(tok, '.')) {
            char *e = tok + strlen(tok);
            while (e > tok && (e[-1] == ',' || e[-1] == ')'))
                *--e = 0;
            write_version_cache(cp, tok);
            snprintf(out, n, "%s", tok);
            break;
        }
        tok = strtok_r(NULL, " \t\n\r", &save);
    }
    free(o);
}

static void hex_encode(const char *s, char *out, size_t n) {
    size_t pos = 0;
    while (*s && pos + 3 < n) {
        pos += (size_t)snprintf(out + pos, n - pos, "%02x", (unsigned char)*s);
        s++;
    }
    out[pos] = 0;
}

static int hex_decode(const char *s, char *out, size_t n) {
    size_t l = strlen(s);
    if (l % 2 != 0 || l == 0)
        return 0;
    size_t pos = 0;
    for (size_t i = 0; i < l && pos + 1 < n; i += 2) {
        char h[3] = {s[i], s[i + 1], 0};
        char *end;
        unsigned long v = strtoul(h, &end, 16);
        if (end != h + 2)
            return 0;
        out[pos++] = (char)v;
    }
    out[pos] = 0;
    return 1;
}

static int kitty_tty_parse(const uint8_t *buf, size_t len, const char *fkey,
                           const char *skey, char *fam, size_t famn,
                           char *siz, size_t sizn) {
    char *text = malloc(len + 1);
    memcpy(text, buf, len);
    text[len] = 0;
    fam[0] = 0;
    siz[0] = 0;
    char *p = text;
    while ((p = strstr(p, "\x1bP1+r")) != NULL) {
        p += 5;
        char *end = strstr(p, "\x1b\\");
        if (!end)
            break;
        *end = 0;
        char *eq = strchr(p, '=');
        if (eq) {
            *eq = 0;
            char k[256], v[1024];
            if (hex_decode(p, k, sizeof k) && hex_decode(eq + 1, v, sizeof v)) {
                if (!strcmp(k, fkey))
                    snprintf(fam, famn, "%.255s", v);
                else if (!strcmp(k, skey))
                    snprintf(siz, sizn, "%.63s", v);
            }
        }
        p = end + 1;
    }
    free(text);
    return fam[0] != 0;
}

static int kitty_tty_font(char *fam, size_t famn, char *siz, size_t sizn) {
    char fk[128], sk[128];
    hex_encode("kitty-query-font_family", fk, sizeof fk);
    hex_encode("kitty-query-font_size", sk, sizeof sk);
    char req[512];
    snprintf(req, sizeof req, "\x1bP+q%s;%s\x1b\\", fk, sk);
    int fd = open("/dev/tty", O_RDWR);
    if (fd < 0)
        return 0;
    struct termios orig, raw;
    if (tcgetattr(fd, &orig) != 0) {
        close(fd);
        return 0;
    }
    raw = orig;
    raw.c_lflag &= (unsigned)(~(ICANON | ECHO));
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(fd, TCSANOW, &raw) != 0) {
        close(fd);
        return 0;
    }
    size_t rl = strlen(req);
    size_t written = 0;
    while (written < rl) {
        ssize_t k = write(fd, req + written, rl - written);
        if (k <= 0) {
            tcsetattr(fd, TCSANOW, &orig);
            close(fd);
            return 0;
        }
        written += (size_t)k;
    }
    uint8_t buf[2048];
    size_t bl = 0;
    for (int i = 0; i < 4; i++) {
        ssize_t k = read(fd, buf + bl, sizeof buf - bl);
        if (k <= 0)
            break;
        bl += (size_t)k;
        int terms = 0;
        for (size_t j = 0; j + 1 < bl; j++) {
            if (buf[j] == 0x1b && buf[j + 1] == '\\')
                terms++;
        }
        if (terms >= 2)
            break;
    }
    tcsetattr(fd, TCSANOW, &orig);
    close(fd);
    return kitty_tty_parse(buf, bl, "kitty-query-font_family", "kitty-query-font_size",
                           fam, famn, siz, sizn);
}

static int kitty_kitten_font(char *fam, size_t famn, char *siz, size_t sizn) {
    const char *args[] = {"+kitten", "query-terminal", NULL};
    char *o = detect_run_capture_timeout("kitty", args, 800);
    if (!o)
        return 0;
    fam[0] = 0;
    siz[0] = 0;
    char *save = NULL;
    char *line = strtok_r(o, "\n", &save);
    while (line) {
        while (*line == ' ' || *line == '\t')
            line++;
        if (!strncmp(line, "font_family:", 12)) {
            const char *v = line + 12;
            while (*v == ' ' || *v == '\t')
                v++;
            snprintf(fam, famn, "%s", v);
        } else if (!strncmp(line, "font_size:", 10)) {
            const char *v = line + 10;
            while (*v == ' ' || *v == '\t')
                v++;
            snprintf(siz, sizn, "%s", v);
        }
        line = strtok_r(NULL, "\n", &save);
    }
    free(o);
    return fam[0] != 0;
}

static void kitty_conf_font(char *fam, size_t famn, char *siz, size_t sizn) {
    fam[0] = 0;
    siz[0] = 0;
    char *home = detect_getenv("HOME");
    if (!home)
        return;
    char path[1152];
    snprintf(path, sizeof path, "%s/.config/kitty/kitty.conf", home);
    free(home);
    char *text = detect_read_file(path);
    if (!text)
        return;
    char *save = NULL;
    char *line = strtok_r(text, "\n", &save);
    while (line) {
        while (*line == ' ' || *line == '\t')
            line++;
        if (!strncmp(line, "font_family", 11) && (line[11] == ' ' || line[11] == '\t')) {
            const char *v = line + 11;
            while (*v == ' ' || *v == '\t')
                v++;
            char tmp[256];
            snprintf(tmp, sizeof tmp, "%s", v);
            char *e = tmp + strlen(tmp);
            while (e > tmp && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '"'))
                *--e = 0;
            char *s2 = tmp;
            while (*s2 == '"')
                s2++;
            snprintf(fam, famn, "%s", s2);
        } else if (!strncmp(line, "font_size", 9) && (line[9] == ' ' || line[9] == '\t')) {
            const char *v = line + 9;
            while (*v == ' ' || *v == '\t')
                v++;
            char tmp[64];
            snprintf(tmp, sizeof tmp, "%s", v);
            char *e = tmp + strlen(tmp);
            while (e > tmp && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '"'))
                *--e = 0;
            char *s2 = tmp;
            while (*s2 == '"')
                s2++;
            if (*s2 && !siz[0])
                snprintf(siz, sizn, "%s", s2);
        }
        if (fam[0] && siz[0])
            break;
        line = strtok_r(NULL, "\n", &save);
    }
    free(text);
}

static void kitty_font(char *out, size_t n) {
    char fam[256] = "", siz[64] = "";
    if (!kitty_tty_font(fam, sizeof fam, siz, sizeof siz))
        if (!kitty_kitten_font(fam, sizeof fam, siz, sizeof siz))
            kitty_conf_font(fam, sizeof fam, siz, sizeof siz);
    if (strchr(fam, '$'))
        return;
    if (!fam[0])
        return;
    pretty_font(fam, siz, out, n);
}

static void terminal_name_from_proc(const char *base, const char *cmdline,
                                    char *out, size_t n) {
    if (!strcmp(base, "electron")) {
        const char *idx = strstr(cmdline, "--user-data-dir=");
        if (idx) {
            const char *rest = idx + 16;
            const char *end = rest;
            while (*end && *end != ' ' && *end != '\n')
                end++;
            const char *slash = end;
            while (slash > rest && *(slash - 1) != '/')
                slash--;
            size_t l = (size_t)(end - slash);
            if (l > 0 && l < n) {
                int alpha = slash[0] == '.' || (slash[0] >= 'A' && slash[0] <= 'Z') ||
                            (slash[0] >= 'a' && slash[0] <= 'z');
                if (alpha) {
                    memcpy(out, slash, l);
                    out[l] = 0;
                    return;
                }
            }
        }
        idx = strstr(cmdline, "app.asar");
        if (idx) {
            const char *slash = idx;
            while (slash > cmdline && *(slash - 1) != '/')
                slash--;
            const char *slash2 = slash - 1;
            while (slash2 > cmdline && *(slash2 - 1) != '/')
                slash2--;
            size_t l = (size_t)(slash - 1 - slash2);
            if (l > 0 && l < n) {
                memcpy(out, slash2, l);
                out[l] = 0;
                return;
            }
        }
        snprintf(out, n, "electron");
        return;
    }
    const char *b = base;
    char tmp[256];
    if (b[0] == '.')
        b++;
    snprintf(tmp, sizeof tmp, "%s", b);
    size_t l = strlen(tmp);
    if (l > 8 && !strcmp(tmp + l - 8, "-wrapped"))
        tmp[l - 8] = 0;
    l = strlen(tmp);
    if (l > 8 && !strcmp(tmp + l - 8, ".wrapped"))
        tmp[l - 8] = 0;
    snprintf(out, n, "%s", tmp);
}

static void detect_uncached(TerminalInfo *out) {
    memset(out, 0, sizeof *out);
    char *v = detect_getenv("TERM_PROGRAM");
    if (v && *v) {
        snprintf(out->name, sizeof out->name, "%s", v);
        free(v);
    }
    if (!out->name[0]) {
        if (getenv("KONSOLE_VERSION")) {
            snprintf(out->name, sizeof out->name, "Konsole");
        } else if (getenv("WT_SESSION")) {
            snprintf(out->name, sizeof out->name, "Windows Terminal");
        } else {
            char *t = detect_getenv("TERMINAL");
            if (t && *t) {
                const char *b = strrchr(t, '/');
                snprintf(out->name, sizeof out->name, "%s", b ? b + 1 : t);
            }
            free(t);
        }
    }
    if (!out->name[0]) {
        unsigned pid = (unsigned)getppid();
        for (int i = 0; i < 20; i++) {
            if (pid == 0 || pid == 1)
                break;
            char p[64], comm[256] = "", cmdline[4096] = "";
            snprintf(p, sizeof p, "/proc/%u/comm", pid);
            char *c = detect_read_file(p);
            if (!c)
                break;
            char *e = c + strlen(c);
            while (e > c && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t'))
                *--e = 0;
            snprintf(comm, sizeof comm, "%.255s", c);
            free(c);
            if (!comm[0])
                break;
            snprintf(p, sizeof p, "/proc/%u/status", pid);
            size_t nsl = 0;
            char **sl = detect_read_file_lines(p, &nsl);
            unsigned ppid = 0;
            for (size_t k = 0; k < nsl; k++) {
                if (!strncmp(sl[k], "PPid:", 5)) {
                    ppid = (unsigned)strtoul(sl[k] + 5, NULL, 10);
                    break;
                }
            }
            detect_free_lines(sl, nsl);
            snprintf(p, sizeof p, "/proc/%u/cmdline", pid);
            FILE *f = fopen(p, "r");
            if (f) {
                char raw[4096];
                size_t nr = fread(raw, 1, sizeof raw - 1, f);
                fclose(f);
                JfBuf b;
                memset(&b, 0, sizeof b);
                size_t q = 0;
                while (q < nr) {
                    if (raw[q]) {
                        size_t s = q;
                        while (q < nr && raw[q])
                            q++;
                        if (b.len > 0)
                            jf_buf_putc(&b, ' ');
                        jf_buf_putn(&b, raw + s, q - s);
                    }
                    q++;
                }
                snprintf(cmdline, sizeof cmdline, "%s", b.data ? b.data : "");
                jf_buf_free(&b);
            }
            char base[256];
            lower_str(comm, base, sizeof base);
            const char *bs = strrchr(base, '/');
            char bl[256];
            snprintf(bl, sizeof bl, "%s", bs ? bs + 1 : base);
            int isshell = 0;
            for (int k = 0; SHELLS[k]; k++) {
                if (!strcmp(bl, SHELLS[k])) {
                    isshell = 1;
                    break;
                }
            }
            if (isshell) {
                pid = ppid;
                continue;
            }
            char nm[256];
            terminal_name_from_proc(bl, cmdline, nm, sizeof nm);
            snprintf(out->name, sizeof out->name, "%s", nm);
            if (!strcmp(nm, "ai.opencode.desktop") || !strcmp(bl, "electron"))
                snprintf(out->exe, sizeof out->exe, "%.*s",
                         (int)(sizeof out->exe - 1), cmdline);
            break;
        }
    }
    if (!out->name[0]) {
        char *t = detect_getenv("TERM");
        if (t) {
            if (!strncmp(t, "xterm", 5))
                snprintf(out->name, sizeof out->name, "xterm");
            else if (*t)
                snprintf(out->name, sizeof out->name, "%s", t);
            free(t);
        }
    }
    char low[256];
    lower_str(out->name, low, sizeof low);
    if (!strcmp(low, "kitty")) {
        char f[256];
        kitty_font(f, sizeof f);
        if (f[0])
            snprintf(out->font, sizeof out->font, "%s", f);
        char vv[128];
        kitty_version(vv, sizeof vv);
        if (vv[0])
            snprintf(out->version, sizeof out->version, "%s", vv);
    } else if (out->name[0]) {
        char vv[128];
        generic_version(out->name, vv, sizeof vv);
        if (vv[0])
            snprintf(out->version, sizeof out->version, "%s", vv);
    }
}

void detect_terminal(TerminalInfo *out) {
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    static int have = 0;
    static TerminalInfo cached;
    pthread_mutex_lock(&mu);
    if (have) {
        *out = cached;
        pthread_mutex_unlock(&mu);
        return;
    }
    pthread_mutex_unlock(&mu);
    TerminalInfo info;
    detect_uncached(&info);
    pthread_mutex_lock(&mu);
    cached = info;
    have = 1;
    *out = info;
    pthread_mutex_unlock(&mu);
}
