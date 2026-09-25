#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "detect.h"

char *detect_read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    size_t k;
    while ((k = fread(buf + len, 1, cap - len - 1, f)) > 0) {
        len += k;
        if (len + 1 >= cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
    }
    fclose(f);
    buf[len] = 0;
    return buf;
}

char **detect_read_file_lines(const char *path, size_t *n) {
    char *text = detect_read_file(path);
    char **out = NULL;
    size_t m = 0;
    *n = 0;
    if (!text)
        return NULL;
    char *save = NULL;
    char *line = strtok_r(text, "\n", &save);
    while (line) {
        size_t l = strlen(line);
        if (l > 0 && line[l - 1] == '\r')
            line[l - 1] = 0;
        out = realloc(out, (m + 1) * sizeof(char *));
        out[m++] = strdup(line);
        line = strtok_r(NULL, "\n", &save);
    }
    free(text);
    *n = m;
    return out;
}

void detect_free_lines(char **lines, size_t n) {
    size_t i;
    if (!lines)
        return;
    for (i = 0; i < n; i++)
        free(lines[i]);
    free(lines);
}

DetectKV *detect_parse_kv_file(const char *path, size_t *n) {
    size_t nl = 0;
    char **lines = detect_read_file_lines(path, &nl);
    DetectKV *out = NULL;
    size_t m = 0;
    *n = 0;
    for (size_t i = 0; i < nl; i++) {
        char *line = lines[i];
        while (*line == ' ' || *line == '\t')
            line++;
        if (!*line || *line == '#')
            continue;
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        char *k = line;
        char *ke = k + strlen(k);
        while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t'))
            *--ke = 0;
        char *v = eq + 1;
        while (*v == ' ' || *v == '\t')
            v++;
        char *ve = v + strlen(v);
        while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t' || ve[-1] == '\n' || ve[-1] == '\r'))
            *--ve = 0;
        out = realloc(out, (m + 1) * sizeof(DetectKV));
        out[m].key = strdup(k);
        out[m].val = strdup(v);
        m++;
    }
    detect_free_lines(lines, nl);
    *n = m;
    return out;
}

void detect_free_kv(DetectKV *kv, size_t n) {
    for (size_t i = 0; i < n; i++) {
        free(kv[i].key);
        free(kv[i].val);
    }
    free(kv);
}

void detect_unquote(char *v) {
    size_t n = strlen(v);
    if (n >= 2 && ((v[0] == '"' && v[n - 1] == '"') || (v[0] == '\'' && v[n - 1] == '\''))) {
        memmove(v, v + 1, n - 2);
        v[n - 2] = 0;
    }
}

char *detect_getenv(const char *name) {
    const char *v = getenv(name);
    return v ? strdup(v) : NULL;
}

char *detect_run_capture(const char *cmd, const char *const *args) {
    int pipefd[2];
    if (pipe(pipefd) != 0)
        return NULL;
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return NULL;
    }
    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        int dn = open("/dev/null", O_RDWR);
        (void)dn;
        size_t n = 0;
        while (args[n])
            n++;
        char **argv = malloc((n + 2) * sizeof(char *));
        argv[0] = (char *)cmd;
        for (size_t i = 0; i < n; i++)
            argv[i + 1] = (char *)args[i];
        argv[n + 1] = NULL;
        execvp(cmd, argv);
        _exit(127);
    }
    close(pipefd[1]);
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    ssize_t k;
    while ((k = read(pipefd[0], buf + len, cap - len - 1)) > 0) {
        len += (size_t)k;
        if (len + 1 >= cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
    }
    close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        free(buf);
        return NULL;
    }
    buf[len] = 0;
    while (len > 0 && (buf[len - 1] == ' ' || buf[len - 1] == '\t' ||
                       buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = 0;
    return buf;
}

char **detect_run_capture_lines(const char *cmd, const char *const *args, size_t *n) {
    char *text = detect_run_capture(cmd, args);
    char **out = NULL;
    size_t m = 0;
    *n = 0;
    if (!text)
        return NULL;
    char *save = NULL;
    char *line = strtok_r(text, "\n", &save);
    while (line) {
        out = realloc(out, (m + 1) * sizeof(char *));
        out[m++] = strdup(line);
        line = strtok_r(NULL, "\n", &save);
    }
    free(text);
    *n = m;
    return out;
}

char *detect_run_capture_timeout(const char *cmd, const char *const *args,
                                 unsigned timeout_ms) {
    int pipefd[2];
    if (pipe(pipefd) != 0)
        return NULL;
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return NULL;
    }
    if (pid == 0) {
        setpgid(0, 0);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        int dn = open("/dev/null", O_RDWR);
        if (dn >= 0) {
            dup2(dn, STDIN_FILENO);
            dup2(dn, STDERR_FILENO);
            if (dn > 2)
                close(dn);
        }
        size_t n = 0;
        while (args[n])
            n++;
        char **argv = malloc((n + 2) * sizeof(char *));
        argv[0] = (char *)cmd;
        for (size_t i = 0; i < n; i++)
            argv[i + 1] = (char *)args[i];
        argv[n + 1] = NULL;
        execvp(cmd, argv);
        _exit(127);
    }
    close(pipefd[1]);
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    uint64_t deadline = jf_now_ms() + timeout_ms;
    int killed = 0, eof = 0;
    while (!eof) {
        uint64_t now = jf_now_ms();
        if (now >= deadline) {
            kill(-pid, SIGKILL);
            killed = 1;
            break;
        }
        struct pollfd pfd;
        pfd.fd = pipefd[0];
        pfd.events = POLLIN;
        pfd.revents = 0;
        int pr = poll(&pfd, 1, (int)(deadline - now));
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (pr == 0) {
            kill(-pid, SIGKILL);
            killed = 1;
            break;
        }
        if (len + 4096 > cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
        ssize_t k = read(pipefd[0], buf + len, cap - len - 1);
        if (k > 0) {
            len += (size_t)k;
        } else if (k == 0) {
            eof = 1;
        } else {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            eof = 1;
        }
    }
    close(pipefd[0]);
    int status = 0;
    pid_t got;
    do {
        got = waitpid(pid, &status, WNOHANG);
        if (got == 0) {
            if (!killed && jf_now_ms() >= deadline) {
                kill(-pid, SIGKILL);
                killed = 1;
            }
            struct timespec ts = {0, 5000000};
            nanosleep(&ts, NULL);
        }
    } while (got == 0);
    if (killed || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        free(buf);
        return NULL;
    }
    buf[len] = 0;
    while (len > 0 && (buf[len - 1] == ' ' || buf[len - 1] == '\t' ||
                       buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = 0;
    if (!buf[0]) {
        free(buf);
        return NULL;
    }
    return buf;
}

char *detect_scan_proc_comm(const char *const *names, size_t n) {
    DIR *dp = opendir("/proc");
    if (!dp)
        return NULL;
    struct dirent *de;
    char *found = NULL;
    while ((de = readdir(dp)) != NULL && !found) {
        char pid[16];
        size_t pl = strlen(de->d_name);
        if (pl >= sizeof pid)
            continue;
        memcpy(pid, de->d_name, pl + 1);
        size_t i = 0;
        int ok = 1;
        if (!pid[0])
            continue;
        while (pid[i]) {
            if (pid[i] < '0' || pid[i] > '9') {
                ok = 0;
                break;
            }
            i++;
        }
        if (!ok)
            continue;
        char path[64];
        snprintf(path, sizeof path, "/proc/%s/comm", pid);
        char *comm = detect_read_file(path);
        if (!comm)
            continue;
        char *e = comm + strlen(comm);
        while (e > comm && (e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'))
            *--e = 0;
        char *base = strrchr(comm, '/');
        base = base ? base + 1 : comm;
        char low[256];
        size_t k = 0;
        while (base[k] && k + 1 < sizeof low) {
            char c = base[k];
            low[k++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        low[k] = 0;
        for (size_t j = 0; j < n; j++) {
            if (!strcmp(low, names[j])) {
                found = strdup(comm);
                break;
            }
        }
        free(comm);
    }
    closedir(dp);
    return found;
}

int detect_proc_by_comm(const char *const *names, size_t n, ProcInfo *out) {
    DIR *dp = opendir("/proc");
    if (!dp)
        return 0;
    struct dirent *de;
    int found = 0;
    memset(out, 0, sizeof *out);
    while ((de = readdir(dp)) != NULL && !found) {
        char pid[16];
        size_t pl = strlen(de->d_name);
        if (pl >= sizeof pid)
            continue;
        memcpy(pid, de->d_name, pl + 1);
        size_t i = 0;
        int ok = 1;
        if (!pid[0])
            continue;
        while (pid[i]) {
            if (pid[i] < '0' || pid[i] > '9') {
                ok = 0;
                break;
            }
            i++;
        }
        if (!ok)
            continue;
        char path[64];
        snprintf(path, sizeof path, "/proc/%s/comm", pid);
        char *comm = detect_read_file(path);
        if (!comm)
            continue;
        char *e = comm + strlen(comm);
        while (e > comm && (e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'))
            *--e = 0;
        char *base = strrchr(comm, '/');
        base = base ? base + 1 : comm;
        char low[256];
        size_t k = 0;
        while (base[k] && k + 1 < sizeof low) {
            char c = base[k];
            low[k++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        low[k] = 0;
        int match = 0;
        for (size_t j = 0; j < n; j++) {
            if (!strcmp(low, names[j])) {
                match = 1;
                break;
            }
        }
        if (!match) {
            free(comm);
            continue;
        }
        out->pid = (unsigned)strtoul(pid, NULL, 10);
        snprintf(out->comm, sizeof out->comm, "%s", comm);
        free(comm);
        snprintf(path, sizeof path, "/proc/%s/exe", pid);
        ssize_t l = readlink(path, out->exe_path, sizeof out->exe_path - 1);
        if (l > 0)
            out->exe_path[l] = 0;
        snprintf(path, sizeof path, "/proc/%s/cmdline", pid);
        FILE *f = fopen(path, "r");
        if (f) {
            char tmp[2048];
            size_t r = fread(tmp, 1, sizeof tmp - 1, f);
            fclose(f);
            JfBuf b;
            memset(&b, 0, sizeof b);
            size_t p = 0;
            while (p < r) {
                if (tmp[p]) {
                    size_t s = p;
                    while (p < r && tmp[p])
                        p++;
                    if (b.len > 0)
                        jf_buf_putc(&b, ' ');
                    jf_buf_putn(&b, tmp + s, p - s);
                }
                p++;
            }
            out->cmdline = b.data ? b.data : strdup("");
        } else {
            out->cmdline = strdup("");
        }
        snprintf(path, sizeof path, "/proc/%s/status", pid);
        size_t nsl = 0;
        char **sl = detect_read_file_lines(path, &nsl);
        for (size_t j = 0; j < nsl; j++) {
            if (!strncmp(sl[j], "PPid:", 5)) {
                out->ppid = (unsigned)strtoul(sl[j] + 5, NULL, 10);
                break;
            }
        }
        detect_free_lines(sl, nsl);
        found = 1;
    }
    closedir(dp);
    return found;
}

void detect_proc_free(ProcInfo *p) {
    free(p->cmdline);
    p->cmdline = NULL;
}
