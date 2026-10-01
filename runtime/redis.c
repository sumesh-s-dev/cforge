#include "internal.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_redis_fd = -1;
static int g_redis_on;
static uint64_t g_redis_errors;

static void redis_fail(const char *what) {
    g_redis_errors++;
    fprintf(stderr, "ERROR redis %s\n", what);
}

static int redis_write_all(const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(g_redis_fd, buf + off, len - off);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

static int redis_read_line(char *buf, size_t cap) {
    size_t off = 0;
    while (off + 1 < cap) {
        ssize_t n = read(g_redis_fd, buf + off, 1);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        if (buf[off] == '\n') {
            buf[off] = 0;
            if (off > 0 && buf[off - 1] == '\r') {
                buf[off - 1] = 0;
            }
            return 0;
        }
        off++;
    }
    return -1;
}

static int redis_expect_pong(void) {
    char line[64];
    if (redis_read_line(line, sizeof line) != 0) {
        return -1;
    }
    return strcmp(line, "+PONG") == 0 ? 0 : -1;
}

static void parse_host_port(const char *url, char *host, size_t hcap, char *port, size_t pcap) {
    const char *p = url;
    if (strncmp(p, "redis://", 8) == 0) {
        p += 8;
    }
    const char *colon = strrchr(p, ':');
    if (colon && colon > p) {
        size_t hlen = (size_t)(colon - p);
        if (hlen >= hcap) {
            hlen = hcap - 1;
        }
        memcpy(host, p, hlen);
        host[hlen] = 0;
        snprintf(port, pcap, "%.*s", (int)(strlen(colon + 1) > pcap - 1 ? pcap - 1 : strlen(colon + 1)), colon + 1);
    } else {
        snprintf(host, hcap, "%s", p);
        snprintf(port, pcap, "6379");
    }
}

int cforge_redis_open(const char *url) {
    if (!url || !url[0]) {
        return 0;
    }
    char host[256];
    char port[16];
    parse_host_port(url, host, sizeof host, port, sizeof port);

    struct addrinfo hints;
    struct addrinfo *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0 || !res) {
        redis_fail("resolve");
        return -1;
    }
    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        redis_fail("connect");
        return -1;
    }
    g_redis_fd = fd;
    if (redis_write_all("PING\r\n", 6) != 0 || redis_expect_pong() != 0) {
        redis_fail("ping");
        close(g_redis_fd);
        g_redis_fd = -1;
        return -1;
    }
    g_redis_on = 1;
    return 0;
}

void cforge_redis_close(void) {
    if (g_redis_fd >= 0) {
        close(g_redis_fd);
        g_redis_fd = -1;
    }
    g_redis_on = 0;
}

const char *cforge_redis_backend(void) {
    return g_redis_on ? "connected" : "disabled";
}

uint64_t cforge_redis_errors(void) {
    return g_redis_errors;
}

void cforge_redis_invalidate_user(uint64_t id) {
    if (!g_redis_on || g_redis_fd < 0) {
        return;
    }
    char cmd[128];
    int n = snprintf(cmd, sizeof cmd, "DEL user:%llu\r\n", (unsigned long long)id);
    if (n < 0 || (size_t)n >= sizeof cmd) {
        return;
    }
    if (redis_write_all(cmd, (size_t)n) != 0) {
        redis_fail("del");
        return;
    }
    char line[32];
    (void)redis_read_line(line, sizeof line);
}
