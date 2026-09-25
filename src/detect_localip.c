#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <linux/ethtool.h>
#include <linux/sockios.h>

#include "detect.h"

static void format_addr4(uint32_t s_addr, char *out, size_t n) {
    uint8_t b[4];
    b[0] = (uint8_t)s_addr;
    b[1] = (uint8_t)(s_addr >> 8);
    b[2] = (uint8_t)(s_addr >> 16);
    b[3] = (uint8_t)(s_addr >> 24);
    snprintf(out, n, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

static void format_addr6(const uint8_t *bytes, char *out, size_t n) {
    uint16_t g[8];
    for (int i = 0; i < 8; i++)
        g[i] = (uint16_t)(((uint16_t)bytes[2 * i] << 8) | bytes[2 * i + 1]);
    int bs = -1, bl = 0;
    int i = 0;
    while (i < 8) {
        if (g[i] == 0) {
            int s = i;
            while (i < 8 && g[i] == 0)
                i++;
            if (i - s > bl && i - s >= 2) {
                bs = s;
                bl = i - s;
            }
        } else {
            i++;
        }
    }
    char tmp[80] = "";
    if (bl > 0) {
        char head[32] = "", tail[32] = "";
        size_t hp = 0, tp = 0;
        for (i = 0; i < bs; i++)
            hp += (size_t)snprintf(head + hp, sizeof head - hp, "%s%x", i ? ":" : "", g[i]);
        for (i = bs + bl; i < 8; i++)
            tp += (size_t)snprintf(tail + tp, sizeof tail - tp, "%s%x", tp ? ":" : "", g[i]);
        snprintf(tmp, sizeof tmp, "%s::%s", head, tail);
    } else {
        size_t p = 0;
        for (i = 0; i < 8; i++)
            p += (size_t)snprintf(tmp + p, sizeof tmp - p, "%s%x", i ? ":" : "", g[i]);
    }
    snprintf(out, n, "%s", tmp);
}

static int make_sock(void) {
    return socket(AF_INET, SOCK_DGRAM, 0);
}

static void setup_ifr(const char *ifname, struct ifreq *ifr) {
    memset(ifr, 0, sizeof *ifr);
    size_t l = strlen(ifname);
    if (l >= IFNAMSIZ)
        l = IFNAMSIZ - 1;
    memcpy(ifr->ifr_name, ifname, l);
}

static void mac_for(const char *ifname, char *out, size_t n) {
    out[0] = 0;
    int fd = make_sock();
    if (fd < 0)
        return;
    struct ifreq ifr;
    setup_ifr(ifname, &ifr);
    if (ioctl(fd, SIOCGIFHWADDR, &ifr) == 0) {
        unsigned char *d = (unsigned char *)ifr.ifr_hwaddr.sa_data;
        snprintf(out, n, "%02x:%02x:%02x:%02x:%02x:%02x", d[0], d[1], d[2], d[3], d[4],
                 d[5]);
    }
    close(fd);
}

static unsigned long long mtu_for(const char *ifname) {
    int fd = make_sock();
    if (fd < 0)
        return 0;
    struct ifreq ifr;
    setup_ifr(ifname, &ifr);
    int r = ioctl(fd, SIOCGIFMTU, &ifr);
    close(fd);
    return r == 0 ? (unsigned long long)ifr.ifr_mtu : 0;
}

static unsigned long long speed_mbps(const char *ifname) {
    int fd = make_sock();
    if (fd < 0)
        return 0;
    struct ifreq ifr;
    setup_ifr(ifname, &ifr);
    struct ethtool_cmd {
        uint32_t cmd;
        uint32_t supported;
        uint32_t advertising;
        uint32_t speed;
        uint8_t duplex;
        uint8_t port;
        uint8_t phy_address;
        uint8_t autoneg;
        uint8_t mdio_support;
        uint8_t maxtxpkt;
        uint8_t maxrxpkt;
        uint16_t speed_hi;
        uint8_t eth_tp_mdix_ctrl;
        uint8_t eth_tp_mdix;
        uint32_t lp_advertising;
        uint32_t reserved[2];
    } data;
    memset(&data, 0, sizeof data);
    data.cmd = 0x00000001;
    ifr.ifr_data = (char *)&data;
    int r = ioctl(fd, SIOCETHTOOL, &ifr);
    close(fd);
    if (r != 0)
        return 0;
    unsigned long long speed = (unsigned long long)data.speed |
                               ((unsigned long long)data.speed_hi << 16);
    if (speed == 0xffff || speed == 0)
        return 0;
    return speed;
}

static void flags_for(const char *ifname, char *out, size_t n) {
    out[0] = 0;
    int fd = make_sock();
    if (fd < 0)
        return;
    struct ifreq ifr;
    setup_ifr(ifname, &ifr);
    int r = ioctl(fd, SIOCGIFFLAGS, &ifr);
    close(fd);
    if (r != 0)
        return;
    short f = ifr.ifr_flags;
    char tmp[80] = "";
    if (f & IFF_UP) {
        if (tmp[0])
            strcat(tmp, ",");
        strcat(tmp, "UP");
    }
    if (f & IFF_BROADCAST) {
        if (tmp[0])
            strcat(tmp, ",");
        strcat(tmp, "BROADCAST");
    }
    if (f & IFF_LOOPBACK) {
        if (tmp[0])
            strcat(tmp, ",");
        strcat(tmp, "LOOPBACK");
    }
    if (f & IFF_RUNNING) {
        if (tmp[0])
            strcat(tmp, ",");
        strcat(tmp, "RUNNING");
    }
    if (f & IFF_MULTICAST) {
        if (tmp[0])
            strcat(tmp, ",");
        strcat(tmp, "MULTICAST");
    }
    snprintf(out, n, "%s", tmp);
}

IpInfo *detect_localip(size_t *n) {
    IpInfo *infos = NULL;
    size_t ni = 0;
    *n = 0;
    struct ifaddrs *ifap = NULL;
    if (getifaddrs(&ifap) != 0)
        return NULL;
    for (struct ifaddrs *p = ifap; p; p = p->ifa_next) {
        if (!p->ifa_name || !strcmp(p->ifa_name, "lo"))
            continue;
        IpInfo *e = NULL;
        for (size_t i = 0; i < ni; i++) {
            if (!strcmp(infos[i].name, p->ifa_name)) {
                e = &infos[i];
                break;
            }
        }
        if (!e) {
            infos = realloc(infos, (ni + 1) * sizeof(IpInfo));
            e = &infos[ni++];
            memset(e, 0, sizeof *e);
            snprintf(e->name, sizeof e->name, "%s", p->ifa_name);
        }
        if (!p->ifa_addr)
            continue;
        if (p->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in *sa = (struct sockaddr_in *)p->ifa_addr;
            char ip[32];
            format_addr4(sa->sin_addr.s_addr, ip, sizeof ip);
            if (ip[0]) {
                int dup = 0;
                for (size_t i = 0; i < e->n4; i++) {
                    if (!strcmp(e->ipv4[i], ip)) {
                        dup = 1;
                        break;
                    }
                }
                if (!dup) {
                    unsigned char pre = 32;
                    if (p->ifa_netmask && p->ifa_netmask->sa_family == AF_INET) {
                        struct sockaddr_in *m = (struct sockaddr_in *)p->ifa_netmask;
                        uint32_t v = m->sin_addr.s_addr;
                        pre = 0;
                        while (v) {
                            pre += v & 1;
                            v >>= 1;
                        }
                    }
                    e->ipv4 = realloc(e->ipv4, (e->n4 + 1) * sizeof(char *));
                    e->prefix4 = realloc(e->prefix4, (e->n4 + 1));
                    e->ipv4[e->n4] = strdup(ip);
                    e->prefix4[e->n4] = pre;
                    e->n4++;
                }
            }
        } else if (p->ifa_addr->sa_family == AF_INET6) {
            struct sockaddr_in6 *sa = (struct sockaddr_in6 *)p->ifa_addr;
            char ip[64];
            format_addr6(sa->sin6_addr.s6_addr, ip, sizeof ip);
            if (ip[0]) {
                int dup = 0;
                for (size_t i = 0; i < e->n6; i++) {
                    if (!strcmp(e->ipv6[i], ip)) {
                        dup = 1;
                        break;
                    }
                }
                if (!dup) {
                    unsigned char pre = 128;
                    if (p->ifa_netmask && p->ifa_netmask->sa_family == AF_INET6) {
                        struct sockaddr_in6 *m = (struct sockaddr_in6 *)p->ifa_netmask;
                        pre = 0;
                        for (int i = 0; i < 16; i++) {
                            unsigned char b = m->sin6_addr.s6_addr[i];
                            while (b) {
                                pre += b & 1;
                                b >>= 1;
                            }
                        }
                    }
                    e->ipv6 = realloc(e->ipv6, (e->n6 + 1) * sizeof(char *));
                    e->prefix6 = realloc(e->prefix6, (e->n6 + 1));
                    e->ipv6[e->n6] = strdup(ip);
                    e->prefix6[e->n6] = pre;
                    e->n6++;
                }
            }
        }
    }
    freeifaddrs(ifap);
    for (size_t i = 0; i < ni; i++) {
        if (!infos[i].mac[0])
            mac_for(infos[i].name, infos[i].mac, sizeof infos[i].mac);
        if (infos[i].mtu == 0)
            infos[i].mtu = mtu_for(infos[i].name);
        infos[i].speed = speed_mbps(infos[i].name);
        if (!infos[i].flags[0])
            flags_for(infos[i].name, infos[i].flags, sizeof infos[i].flags);
    }
    for (size_t i = 0; i < ni; i++) {
        for (size_t j = i + 1; j < ni; j++) {
            if (strcmp(infos[i].name, infos[j].name) > 0) {
                IpInfo t = infos[i];
                infos[i] = infos[j];
                infos[j] = t;
            }
        }
    }
    size_t m = 0;
    for (size_t i = 0; i < ni; i++) {
        if (infos[i].n4 == 0 && infos[i].n6 == 0) {
            for (size_t k = 0; k < infos[i].n4; k++)
                free(infos[i].ipv4[k]);
            free(infos[i].ipv4);
            free(infos[i].prefix4);
            for (size_t k = 0; k < infos[i].n6; k++)
                free(infos[i].ipv6[k]);
            free(infos[i].ipv6);
            free(infos[i].prefix6);
            continue;
        }
        infos[m++] = infos[i];
    }
    *n = m;
    return infos;
}

void detect_localip_free(IpInfo *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        for (size_t k = 0; k < p[i].n4; k++)
            free(p[i].ipv4[k]);
        free(p[i].ipv4);
        free(p[i].prefix4);
        for (size_t k = 0; k < p[i].n6; k++)
            free(p[i].ipv6[k]);
        free(p[i].ipv6);
        free(p[i].prefix6);
    }
    free(p);
}

int detect_default_iface(char *out, size_t n) {
    char *text = detect_read_file("/proc/net/route");
    if (!text)
        return 0;
    unsigned long long best_m = 0, low_m = 0, high_m = 0;
    char best[64] = "", low[64] = "", high[64] = "";
    int have_best = 0, have_low = 0, have_high = 0;
    char *save = NULL;
    char *line = strtok_r(text, "\n", &save);
    int first = 1;
    while (line) {
        if (first) {
            first = 0;
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        char *f[10];
        int nf = 0;
        char *s2 = NULL;
        char *dup = strdup(line);
        char *tok = strtok_r(dup, " \t", &s2);
        while (tok && nf < 10) {
            f[nf++] = tok;
            tok = strtok_r(NULL, " \t", &s2);
        }
        if (nf >= 8) {
            char *end;
            unsigned long dest = strtoul(f[1], &end, 16);
            if (end == f[1])
                dest = 0xFFFFFFFF;
            unsigned long mask = strtoul(f[7], &end, 16);
            if (end == f[7])
                mask = 0;
            unsigned long long metric = strtoull(f[6], &end, 10);
            if (end == f[6])
                metric = 0xFFFFFFFFFFFFFFFFull;
            if (dest == 0 && mask == 0) {
                if (!have_best || metric < best_m) {
                    best_m = metric;
                    snprintf(best, sizeof best, "%s", f[0]);
                    have_best = 1;
                }
            } else if (dest == 0 && mask == 0x80000000) {
                if (!have_low || metric < low_m) {
                    low_m = metric;
                    snprintf(low, sizeof low, "%s", f[0]);
                    have_low = 1;
                }
            } else if (dest == 0x80000000 && mask == 0x80000000) {
                if (!have_high || metric < high_m) {
                    high_m = metric;
                    snprintf(high, sizeof high, "%s", f[0]);
                    have_high = 1;
                }
            }
        }
        free(dup);
        line = strtok_r(NULL, "\n", &save);
    }
    free(text);
    if (have_low && have_high) {
        if (!strcmp(low, high)) {
            snprintf(out, n, "%s", low);
            return 1;
        }
        snprintf(out, n, "%s", low_m <= high_m ? low : high);
        return 1;
    }
    if (have_best) {
        snprintf(out, n, "%s", best);
        return 1;
    }
    if (have_low) {
        snprintf(out, n, "%s", low);
        return 1;
    }
    if (have_high) {
        snprintf(out, n, "%s", high);
        return 1;
    }
    return 0;
}

int detect_is_virtual(const char *name) {
    static const char *prefixes[] = {
        "docker", "veth", "virbr", "vmnet", "br-", "proton", "wg", "tun", "tap",
        "zt", "tailscale", "ipv6leak", "vboxnet", "vmbr", "lxc", "qemu", "podman"
    };
    for (size_t i = 0; i < 17; i++) {
        if (!strncmp(name, prefixes[i], strlen(prefixes[i])))
            return 1;
    }
    return 0;
}

int detect_outbound_src_ip(char *out, size_t n) {
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return 0;
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof dst);
    dst.sin_family = AF_INET;
    dst.sin_port = htons(53);
    dst.sin_addr.s_addr = htonl(0x01010101);
    if (connect(fd, (struct sockaddr *)&dst, sizeof dst) != 0) {
        close(fd);
        return 0;
    }
    struct sockaddr_in local;
    memset(&local, 0, sizeof local);
    socklen_t len = sizeof local;
    if (getsockname(fd, (struct sockaddr *)&local, &len) != 0) {
        close(fd);
        return 0;
    }
    close(fd);
    char ip[32];
    format_addr4(local.sin_addr.s_addr, ip, sizeof ip);
    if (!ip[0] || !strcmp(ip, "0.0.0.0"))
        return 0;
    snprintf(out, n, "%s", ip);
    return 1;
}

int detect_hostname_hint(char *out, size_t n) {
    char *h = detect_getenv("HOSTNAME");
    if (h && *h) {
        snprintf(out, n, "%s", h);
        free(h);
        return 1;
    }
    free(h);
    return 0;
}
