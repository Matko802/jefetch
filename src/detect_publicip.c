#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "common.h"
#include "detect.h"

static int http_get(const char *host, char *out, size_t n) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, "80", &hints, &res) != 0)
        return 0;
    int fd = -1;
    for (struct addrinfo *a = res; a; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0)
            continue;
        struct timeval tv = {4, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        if (connect(fd, a->ai_addr, a->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0)
        return 0;
    char req[512];
    snprintf(req, sizeof req,
             "GET / HTTP/1.0\r\nHost: %s\r\nUser-Agent: jefetch/0.1\r\n"
             "Accept: text/plain\r\nConnection: close\r\n\r\n",
             host);
    size_t rl = strlen(req);
    size_t sent = 0;
    while (sent < rl) {
        ssize_t k = write(fd, req + sent, rl - sent);
        if (k < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            return 0;
        }
        sent += (size_t)k;
    }
    char buf[4096];
    size_t bl = 0;
    ssize_t k;
    while (bl + 1 < sizeof buf && (k = read(fd, buf + bl, sizeof buf - 1 - bl)) > 0)
        bl += (size_t)k;
    close(fd);
    buf[bl] = 0;
    char *save = NULL;
    char *line = strtok_r(buf, "\n", &save);
    while (line) {
        while (*line == ' ' || *line == '\t' || *line == '\r')
            line++;
        char *e = line + strlen(line);
        while (e > line && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
            *--e = 0;
        if (*line) {
            int dots = 0, ok = 1;
            for (char *p = line; *p; p++) {
                if (*p == '.') {
                    dots++;
                } else if (*p < '0' || *p > '9') {
                    ok = 0;
                    break;
                }
            }
            if (ok && dots == 3) {
                snprintf(out, n, "%s", line);
                return 1;
            }
        }
        line = strtok_r(NULL, "\n", &save);
    }
    return 0;
}

typedef struct {
    char *result;
    int done;
    pthread_mutex_t mu;
} RaceShared;

typedef struct {
    RaceShared *sh;
    const char *host;
} RaceArg;

static void *race_thread(void *arg) {
    RaceArg *a = arg;
    char ip[64];
    if (http_get(a->host, ip, sizeof ip)) {
        pthread_mutex_lock(&a->sh->mu);
        if (!a->sh->done) {
            a->sh->done = 1;
            a->sh->result = strdup(ip);
        }
        pthread_mutex_unlock(&a->sh->mu);
    }
    free(a);
    return NULL;
}

int detect_publicip(char *out, size_t n, unsigned timeout_ms) {
    static const char *hosts[] = {"api.ipify.org", "ipv4.icanhazip.com", "ifconfig.me",
                                  "ipinfo.io/ip"};
    RaceShared sh;
    sh.result = NULL;
    sh.done = 0;
    pthread_mutex_init(&sh.mu, NULL);
    pthread_t th[4];
    for (int i = 0; i < 4; i++) {
        RaceArg *a = malloc(sizeof(RaceArg));
        a->sh = &sh;
        a->host = hosts[i];
        pthread_create(&th[i], NULL, race_thread, a);
    }
    uint64_t deadline = jf_now_ms() + timeout_ms;
    char *got = NULL;
    while (jf_now_ms() < deadline) {
        pthread_mutex_lock(&sh.mu);
        if (sh.done) {
            got = sh.result;
            sh.result = NULL;
            pthread_mutex_unlock(&sh.mu);
            break;
        }
        pthread_mutex_unlock(&sh.mu);
        struct timespec ts = {0, 5000000};
        nanosleep(&ts, NULL);
    }
    for (int i = 0; i < 4; i++)
        pthread_detach(th[i]);
    pthread_mutex_destroy(&sh.mu);
    if (!got)
        return 0;
    snprintf(out, n, "%s", got);
    free(got);
    return 1;
}
