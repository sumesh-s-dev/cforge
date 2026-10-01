#include "internal.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

enum { PH_HTTP = 0, PH_HANDSHAKE = 1 };

typedef struct Conn {
    int fd;
    int used;
    int tls;
    int phase;
    int close_after;
    int want_write;
    int pause_read;
    SSL *ssl;
    uint8_t *in;
    size_t in_cap;
    size_t in_len;
    size_t in_off;
    uint8_t *out;
    size_t out_cap;
    size_t out_len;
    size_t out_off;
    Arena arena;
    uint64_t last_ms;
} Conn;

typedef struct Route {
    int method;
    char *pattern;
    size_t plen;
    int param;
    size_t prefix_len;
    CForgeFn fn;
} Route;

enum { M_GET = 1, M_POST = 2, M_DELETE = 4, ID_EVENT = 1, ID_HTTP = 2, ID_TLS = 3, ID_CONN = 10 };

static Conn *g_conns;
static int g_max_conns;
static int *g_free_stack;
static int g_free_top;
static Route g_routes[CFORGE_MAX_ROUTES];
static int g_nroutes;
static int g_epfd = -1;
static int g_http_fd = -1;
static int g_tls_fd = -1;
static int g_event_fd = -1;
static int g_accepting = 1;
static int g_draining = 0;
static SSL_CTX *g_ssl;
static uint64_t g_requests;
static uint64_t g_active;
static int g_paused;
static uint64_t g_idle_ms = CFORGE_IDLE_MS_DEFAULT;
static volatile sig_atomic_t g_stop;

void *cforge_arena_alloc(Arena *arena, size_t n) {
    size_t off = (arena->off + 7u) & ~(size_t)7u;
    if (n > arena->cap || off > arena->cap - n) {
        return NULL;
    }
    void *p = arena->base + off;
    arena->off = off + n;
    return p;
}

void cforge_arena_reset(Arena *arena) {
    arena->off = 0;
}

Slice slice_empty(void) {
    Slice s;
    s.ptr = NULL;
    s.len = 0;
    return s;
}

static uint64_t mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
    if (g_event_fd >= 0) {
        uint64_t one = 1;
        ssize_t n = write(g_event_fd, &one, sizeof one);
        (void)n;
    }
}

static const char *reason_phrase(int status) {
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    case 417: return "Expectation Failed";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
    case 505: return "HTTP Version Not Supported";
    default: return "Error";
    }
}

static const char *method_name(int method) {
    if (method == M_GET) return "GET";
    if (method == M_POST) return "POST";
    if (method == M_DELETE) return "DELETE";
    return "?";
}

int cforge_queue(Ctx *ctx, int status, const char *ctype, const void *body, size_t len, const char *extra) {
    char head[512];
    const char *conn = ctx->close_after ? "close" : "keep-alive";
    int hn;
    if (ctype && extra) {
        hn = snprintf(head, sizeof head,
                      "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: %s\r\n%s\r\n",
                      status, reason_phrase(status), ctype, len, conn, extra);
    } else if (ctype) {
        hn = snprintf(head, sizeof head,
                      "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: %s\r\nServer: cforge\r\n\r\n",
                      status, reason_phrase(status), ctype, len, conn);
    } else if (extra) {
        hn = snprintf(head, sizeof head,
                      "HTTP/1.1 %d %s\r\nContent-Length: %zu\r\nConnection: %s\r\n%s\r\n",
                      status, reason_phrase(status), len, conn, extra);
    } else {
        hn = snprintf(head, sizeof head,
                      "HTTP/1.1 %d %s\r\nContent-Length: %zu\r\nConnection: %s\r\nServer: cforge\r\n\r\n",
                      status, reason_phrase(status), len, conn);
    }
    if (hn < 0 || (size_t)hn >= sizeof head) {
        return -1;
    }
    if (*ctx->out_len + (size_t)hn + len > ctx->out_cap) {
        return -1;
    }
    memcpy(ctx->out + *ctx->out_len, head, (size_t)hn);
    *ctx->out_len += (size_t)hn;
    if (len > 0 && body) {
        memcpy(ctx->out + *ctx->out_len, body, len);
        *ctx->out_len += len;
    }
    ctx->responded = 1;
    return 0;
}

static int queue_text(Ctx *ctx, int status, const char *msg) {
    return cforge_queue(ctx, status, "text/plain; charset=utf-8", msg, strlen(msg), NULL);
}

int32_t ctx_status(Ctx *ctx, int32_t status) {
    return cforge_queue(ctx, status, NULL, NULL, 0, NULL);
}

int32_t ctx_text(Ctx *ctx, int32_t status, Slice body) {
    return cforge_queue(ctx, status, "text/plain; charset=utf-8", body.ptr, body.len, NULL);
}

static int append_json_str(char *dst, size_t cap, size_t *off, Slice s) {
    if (*off + 1 > cap) {
        return -1;
    }
    dst[(*off)++] = '"';
    for (size_t i = 0; i < s.len; i++) {
        uint8_t c = s.ptr[i];
        const char *esc = NULL;
        if (c == '"') esc = "\\\"";
        else if (c == '\\') esc = "\\\\";
        else if (c == '\n') esc = "\\n";
        else if (c == '\r') esc = "\\r";
        else if (c == '\t') esc = "\\t";
        if (esc) {
            size_t el = strlen(esc);
            if (*off + el > cap) return -1;
            memcpy(dst + *off, esc, el);
            *off += el;
        } else if (c < 0x20) {
            if (*off + 6 > cap) return -1;
            int n = snprintf(dst + *off, cap - *off, "\\u%04x", c);
            if (n != 6) return -1;
            *off += 6;
        } else {
            if (*off + 1 > cap) return -1;
            dst[(*off)++] = (char)c;
        }
    }
    if (*off + 1 > cap) return -1;
    dst[(*off)++] = '"';
    return 0;
}

int32_t ctx_json_user(Ctx *ctx, int32_t status, uint64_t id, Slice name, uint32_t age) {
    char body[2048];
    size_t off = 0;
    int n = snprintf(body, sizeof body, "{\"id\":%llu,\"name\":", (unsigned long long)id);
    if (n < 0 || (size_t)n >= sizeof body) return -1;
    off = (size_t)n;
    if (append_json_str(body, sizeof body, &off, name) != 0) return -1;
    n = snprintf(body + off, sizeof body - off, ",\"age\":%u}", age);
    if (n < 0 || (size_t)n >= sizeof body - off) return -1;
    off += (size_t)n;
    return cforge_queue(ctx, status, "application/json", body, off, NULL);
}

int32_t ctx_json_id(Ctx *ctx, int32_t status, uint64_t id) {
    char body[64];
    int n = snprintf(body, sizeof body, "{\"id\":%llu}", (unsigned long long)id);
    if (n < 0 || (size_t)n >= sizeof body) return -1;
    return cforge_queue(ctx, status, "application/json", body, (size_t)n, NULL);
}

int32_t ctx_metrics(Ctx *ctx) {
    char body[320];
    int n = snprintf(body, sizeof body,
                     "requests_total %llu\nactive_connections %llu\ndb_errors %llu\naccept_paused %d\ndb_backend %s\n",
                     (unsigned long long)g_requests, (unsigned long long)g_active,
                     (unsigned long long)cforge_db_errors(), g_paused, cforge_db_backend());
    if (n < 0 || (size_t)n >= sizeof body) return -1;
    return cforge_queue(ctx, 200, "text/plain; charset=utf-8", body, (size_t)n, NULL);
}

int32_t ctx_param_u64(Ctx *ctx, int32_t index, uint64_t *out) {
    if (index < 0 || index >= ctx->nparams) return -1;
    Slice s = ctx->params[index];
    if (!s.ptr || s.len == 0 || s.len > 20) return -1;
    if (s.len > 1 && s.ptr[0] == '0') return -1;
    uint64_t v = 0;
    for (size_t i = 0; i < s.len; i++) {
        if (s.ptr[i] < '0' || s.ptr[i] > '9') return -1;
        uint64_t d = (uint64_t)(s.ptr[i] - '0');
        if (v > (UINT64_MAX - d) / 10u) return -1;
        v = v * 10u + d;
    }
    *out = v;
    return 0;
}

static int add_route(int method, Slice path, CForgeFn fn) {
    if (!fn || path.len == 0 || path.len > 128 || !path.ptr || g_nroutes >= CFORGE_MAX_ROUTES) {
        fprintf(stderr, "ERROR route rejected\n");
        return -1;
    }
    char *pat = malloc(path.len + 1);
    if (!pat) return -1;
    memcpy(pat, path.ptr, path.len);
    pat[path.len] = 0;
    Route *r = &g_routes[g_nroutes];
    r->method = method;
    r->pattern = pat;
    r->plen = path.len;
    r->fn = fn;
    r->param = 0;
    r->prefix_len = 0;
    char *colon = strchr(pat, ':');
    if (colon) {
        if (strchr(colon + 1, '/') || strchr(colon + 1, ':') || colon == pat || *(colon - 1) != '/' || colon[1] == 0) {
            fprintf(stderr, "ERROR route pattern %s\n", pat);
            free(pat);
            return -1;
        }
        r->param = 1;
        r->prefix_len = (size_t)(colon - pat);
    }
    g_nroutes++;
    return 0;
}

void app_get(Slice path, CForgeFn fn) { add_route(M_GET, path, fn); }
void app_post(Slice path, CForgeFn fn) { add_route(M_POST, path, fn); }
void app_delete(Slice path, CForgeFn fn) { add_route(M_DELETE, path, fn); }

static int path_match(const Route *r, Slice path, Slice *param) {
    if (!r->param) {
        return path.len == r->plen && path.ptr && memcmp(path.ptr, r->pattern, path.len) == 0;
    }
    if (!path.ptr || path.len <= r->prefix_len) return 0;
    if (memcmp(path.ptr, r->pattern, r->prefix_len) != 0) return 0;
    Slice rest;
    rest.ptr = path.ptr + r->prefix_len;
    rest.len = path.len - r->prefix_len;
    for (size_t i = 0; i < rest.len; i++) {
        if (rest.ptr[i] == '/') return 0;
    }
    *param = rest;
    return 1;
}

static void allow_header(int bits, char *dst, size_t cap) {
    size_t off = 0;
    int n = snprintf(dst, cap, "Allow: ");
    if (n < 0) {
        dst[0] = 0;
        return;
    }
    off = (size_t)n;
    int first = 1;
    const int ids[3] = {M_GET, M_POST, M_DELETE};
    for (int i = 0; i < 3; i++) {
        if ((bits & ids[i]) == 0) continue;
        n = snprintf(dst + off, cap - off, "%s%s", first ? "" : ", ", method_name(ids[i]));
        if (n < 0 || (size_t)n >= cap - off) {
            dst[0] = 0;
            return;
        }
        off += (size_t)n;
        first = 0;
    }
    snprintf(dst + off, cap - off, "\r\nServer: cforge\r\n");
}

static void dispatch(Conn *c, int method, Slice path, Slice body, int close_after) {
    Ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.arena = &c->arena;
    ctx.path = path;
    ctx.body = body;
    ctx.close_after = close_after;
    ctx.method = method;
    ctx.out = c->out;
    ctx.out_len = &c->out_len;
    ctx.out_cap = c->out_cap;

    int allow = 0;
    CForgeFn fn = NULL;
    for (int i = 0; i < g_nroutes; i++) {
        Slice param;
        param.ptr = NULL;
        param.len = 0;
        if (!path_match(&g_routes[i], path, &param)) continue;
        allow |= g_routes[i].method;
        if (g_routes[i].method == method && !fn) {
            fn = g_routes[i].fn;
            if (g_routes[i].param && ctx.nparams < 4) {
                ctx.params[ctx.nparams++] = param;
            }
        }
    }
    if (fn) {
        fn(&ctx);
    } else if (allow) {
        char extra[64];
        allow_header(allow, extra, sizeof extra);
        cforge_queue(&ctx, 405, "text/plain; charset=utf-8", "method", 6, extra);
    } else {
        queue_text(&ctx, 404, "not found");
    }
    if (!ctx.responded) {
        queue_text(&ctx, 500, "no response");
    }
    c->close_after = ctx.close_after;
    cforge_arena_reset(&c->arena);
    cforge_db_release();
    g_requests++;

    char logpath[129];
    size_t pl = path.len < 128 ? path.len : 128;
    if (path.ptr && pl) memcpy(logpath, path.ptr, pl);
    logpath[pl] = 0;
    cforge_logf(2, "request method=%s path=%s", method_name(method), logpath);
}

static int header_name_eq(const uint8_t *s, size_t n, const char *lit) {
    size_t L = strlen(lit);
    if (n != L) return 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t c = s[i];
        if (c >= 'A' && c <= 'Z') c = (uint8_t)(c - 'A' + 'a');
        if (c != (uint8_t)lit[i]) return 0;
    }
    return 1;
}

static int contains_ci(const uint8_t *s, size_t n, const char *lit) {
    size_t L = strlen(lit);
    if (L > n) return 0;
    for (size_t i = 0; i + L <= n; i++) {
        size_t k = 0;
        for (; k < L; k++) {
            uint8_t c = s[i + k];
            if (c >= 'A' && c <= 'Z') c = (uint8_t)(c - 'A' + 'a');
            if (c != (uint8_t)lit[k]) break;
        }
        if (k == L) return 1;
    }
    return 0;
}

typedef struct Parsed {
    int kind;
    int err_status;
    const char *err;
    int method;
    Slice path;
    Slice body;
    int close_after;
    size_t consumed;
} Parsed;

static Parsed parse_request(const uint8_t *buf, size_t len) {
    Parsed p;
    memset(&p, 0, sizeof p);
    p.kind = 0;
    size_t i = 0;
    if (len > CFORGE_MAX_HEADER) {
        size_t hdr_end = 0;
        int found = 0;
        for (size_t k = 0; k + 3 < len && k < CFORGE_MAX_HEADER + 4; k++) {
            if (buf[k] == '\r' && buf[k + 1] == '\n' && buf[k + 2] == '\r' && buf[k + 3] == '\n') {
                found = 1;
                hdr_end = k + 4;
                break;
            }
        }
        if (!found) {
            p.kind = 2;
            p.err_status = 431;
            p.err = "headers too large";
            return p;
        }
        (void)hdr_end;
    }
    while (i < len && buf[i] != ' ' && buf[i] != '\r') i++;
    if (i == 0 || i >= len || buf[i] != ' ') {
        if (len > 8) {
            p.kind = 2;
            p.err_status = 400;
            p.err = "bad request";
        }
        return p;
    }
    int method = 0;
    if (i == 3 && memcmp(buf, "GET", 3) == 0) method = M_GET;
    else if (i == 4 && memcmp(buf, "POST", 4) == 0) method = M_POST;
    else if (i == 6 && memcmp(buf, "DELETE", 6) == 0) method = M_DELETE;
    else method = 0;
    size_t path_b = ++i;
    while (i < len && buf[i] != ' ' && buf[i] != '\r') i++;
    if (i >= len || buf[i] != ' ' || i == path_b) {
        if (len > CFORGE_MAX_HEADER) {
            p.kind = 2;
            p.err_status = 431;
            p.err = "headers too large";
        }
        return p;
    }
    Slice raw_path;
    raw_path.ptr = (uint8_t *)(buf + path_b);
    raw_path.len = i - path_b;
    if (raw_path.len == 0 || raw_path.ptr[0] != '/' || raw_path.len > 2048) {
        p.kind = 2;
        p.err_status = 400;
        p.err = "bad path";
        return p;
    }
    size_t path_len = raw_path.len;
    for (size_t k = 0; k < raw_path.len; k++) {
        if (raw_path.ptr[k] == '?') {
            path_len = k;
            break;
        }
    }
    if (path_len == 0) {
        p.kind = 2;
        p.err_status = 400;
        p.err = "bad path";
        return p;
    }
    i++;
    size_t ver_b = i;
    while (i + 1 < len && !(buf[i] == '\r' && buf[i + 1] == '\n')) i++;
    if (i + 1 >= len) return p;
    size_t ver_len = i - ver_b;
    int minor = -1;
    if (ver_len == 8 && memcmp(buf + ver_b, "HTTP/1.1", 8) == 0) minor = 1;
    else if (ver_len == 8 && memcmp(buf + ver_b, "HTTP/1.0", 8) == 0) minor = 0;
    else {
        p.kind = 2;
        p.err_status = 505;
        p.err = "bad version";
        return p;
    }
    i += 2;
    int saw_cl = 0;
    int saw_te = 0;
    int saw_host = 0;
    int saw_expect = 0;
    int close_after = minor == 0;
    uint64_t cl = 0;
    int header_count = 0;
    for (;;) {
        if (i + 1 >= len) return p;
        if (buf[i] == '\r' && buf[i + 1] == '\n') {
            i += 2;
            break;
        }
        if (++header_count > 64) {
            p.kind = 2;
            p.err_status = 431;
            p.err = "too many headers";
            return p;
        }
        size_t nb = i;
        while (i < len && buf[i] != ':' && buf[i] != '\r') i++;
        if (i >= len) return p;
        if (buf[i] != ':') {
            p.kind = 2;
            p.err_status = 400;
            p.err = "bad header";
            return p;
        }
        size_t nlen = i - nb;
        i++;
        while (i < len && buf[i] == ' ') i++;
        size_t vb = i;
        while (i + 1 < len && !(buf[i] == '\r' && buf[i + 1] == '\n')) i++;
        if (i + 1 >= len) return p;
        size_t vlen = i - vb;
        while (vlen > 0 && buf[vb + vlen - 1] == ' ') vlen--;
        i += 2;
        if (i > CFORGE_MAX_HEADER) {
            p.kind = 2;
            p.err_status = 431;
            p.err = "headers too large";
            return p;
        }
        if (header_name_eq(buf + nb, nlen, "content-length")) {
            if (saw_cl) {
                p.kind = 2;
                p.err_status = 400;
                p.err = "bad content-length";
                return p;
            }
            saw_cl = 1;
            if (vlen == 0) {
                p.kind = 2;
                p.err_status = 400;
                p.err = "bad content-length";
                return p;
            }
            uint64_t v = 0;
            for (size_t k = 0; k < vlen; k++) {
                if (buf[vb + k] < '0' || buf[vb + k] > '9') {
                    p.kind = 2;
                    p.err_status = 400;
                    p.err = "bad content-length";
                    return p;
                }
                v = v * 10u + (uint64_t)(buf[vb + k] - '0');
                if (v > CFORGE_MAX_BODY) {
                    p.kind = 2;
                    p.err_status = 413;
                    p.err = "body too large";
                    return p;
                }
            }
            cl = v;
        } else if (header_name_eq(buf + nb, nlen, "transfer-encoding")) {
            saw_te = 1;
        } else if (header_name_eq(buf + nb, nlen, "connection")) {
            if (contains_ci(buf + vb, vlen, "close")) close_after = 1;
            if (minor == 0 && contains_ci(buf + vb, vlen, "keep-alive")) close_after = 0;
        } else if (header_name_eq(buf + nb, nlen, "host")) {
            if (vlen > 0) saw_host = 1;
        } else if (header_name_eq(buf + nb, nlen, "expect")) {
            saw_expect = 1;
        }
    }
    if (saw_te) {
        p.kind = 2;
        p.err_status = 501;
        p.err = "chunked not in this slice";
        return p;
    }
    if (saw_expect) {
        p.kind = 2;
        p.err_status = 417;
        p.err = "no expect";
        return p;
    }
    if (minor == 1 && !saw_host) {
        p.kind = 2;
        p.err_status = 400;
        p.err = "missing host";
        return p;
    }
    if (len - i < cl) return p;
    p.kind = 1;
    p.method = method;
    p.path.ptr = raw_path.ptr;
    p.path.len = path_len;
    p.body.ptr = (uint8_t *)(buf + i);
    p.body.len = (size_t)cl;
    p.close_after = close_after;
    p.consumed = i + (size_t)cl;
    return p;
}

static int conn_read(Conn *c, uint8_t *dst, size_t cap) {
    if (cap > INT_MAX) cap = INT_MAX;
    if (c->ssl) {
        int n = SSL_read(c->ssl, dst, (int)cap);
        if (n > 0) return n;
        int e = SSL_get_error(c->ssl, n);
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
            errno = EAGAIN;
            if (e == SSL_ERROR_WANT_WRITE) c->want_write = 1;
            return -1;
        }
        if (e == SSL_ERROR_ZERO_RETURN) return 0;
        return -1;
    }
    ssize_t n = read(c->fd, dst, cap);
    if (n < 0) return -1;
    return (int)n;
}

static int conn_write(Conn *c, const uint8_t *src, size_t len) {
    if (len > INT_MAX) len = INT_MAX;
    if (c->ssl) {
        int n = SSL_write(c->ssl, src, (int)len);
        if (n > 0) return n;
        int e = SSL_get_error(c->ssl, n);
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
            errno = EAGAIN;
            c->want_write = 1;
            return -1;
        }
        return -1;
    }
    ssize_t n = write(c->fd, src, len);
    if (n < 0) return -1;
    return (int)n;
}

static void interest(Conn *c, int idx) {
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLRDHUP | (c->want_write ? EPOLLOUT : 0);
    ev.data.u64 = (uint64_t)(ID_CONN + idx);
    if (epoll_ctl(g_epfd, EPOLL_CTL_MOD, c->fd, &ev) < 0) {
        c->close_after = 1;
    }
}

static void pause_accept(void);
static void resume_accept(void);

static void conn_close(Conn *c, int idx) {
    if (!c->used) return;
    if (c->ssl) {
        SSL_free(c->ssl);
        c->ssl = NULL;
    }
    if (c->fd >= 0) {
        epoll_ctl(g_epfd, EPOLL_CTL_DEL, c->fd, NULL);
        close(c->fd);
        c->fd = -1;
    }
    c->used = 0;
    c->in_len = 0;
    c->in_off = 0;
    c->out_len = 0;
    c->out_off = 0;
    c->want_write = 0;
    cforge_arena_reset(&c->arena);
    if (g_active > 0) g_active--;
    g_free_stack[g_free_top++] = idx;
    if (g_paused) resume_accept();
}

static int flush_out(Conn *c, int idx) {
    while (c->out_off < c->out_len) {
        int n = conn_write(c, c->out + c->out_off, c->out_len - c->out_off);
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                c->want_write = 1;
                interest(c, idx);
                return 0;
            }
            conn_close(c, idx);
            return -1;
        }
        if (n == 0) {
            conn_close(c, idx);
            return -1;
        }
        c->out_off += (size_t)n;
        c->last_ms = mono_ms();
    }
    c->out_off = 0;
    c->out_len = 0;
    if (c->want_write) {
        c->want_write = 0;
        interest(c, idx);
    }
    return 1;
}

static void compact_in(Conn *c) {
    if (c->in_off == 0) return;
    size_t keep = c->in_len - c->in_off;
    if (keep > 0) memmove(c->in, c->in + c->in_off, keep);
    c->in_len = keep;
    c->in_off = 0;
}

static void queue_err(Conn *c, int status, const char *msg) {
    Ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.out = c->out;
    ctx.out_len = &c->out_len;
    ctx.out_cap = c->out_cap;
    ctx.close_after = 1;
    ctx.arena = &c->arena;
    if (queue_text(&ctx, status, msg) != 0) {
        const char *tiny = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        size_t n = strlen(tiny);
        if (n <= c->out_cap) {
            memcpy(c->out, tiny, n);
            c->out_len = n;
            c->out_off = 0;
        }
    }
    c->close_after = 1;
}

static void pump(Conn *c, int idx) {
    for (;;) {
        if (c->out_off < c->out_len) {
            if (flush_out(c, idx) <= 0) return;
        }
        if (!c->used) return;
        if (c->close_after && c->out_len == 0 && c->in_off >= c->in_len) {
            conn_close(c, idx);
            return;
        }
        if (c->in_off >= c->in_len) {
            compact_in(c);
            return;
        }
        Parsed p = parse_request(c->in + c->in_off, c->in_len - c->in_off);
        if (p.kind == 0) {
            if ((c->in_len - c->in_off) > CFORGE_MAX_HEADER) {
                queue_err(c, 431, "headers too large");
                flush_out(c, idx);
                if (c->used) conn_close(c, idx);
            }
            return;
        }
        if (p.kind == 2) {
            queue_err(c, p.err_status ? p.err_status : 400, p.err ? p.err : "bad request");
            flush_out(c, idx);
            if (c->used) conn_close(c, idx);
            return;
        }
        dispatch(c, p.method, p.path, p.body, p.close_after || g_draining);
        c->in_off += p.consumed;
        if (flush_out(c, idx) <= 0) return;
        if (!c->used) return;
        if (c->close_after) {
            compact_in(c);
            if (c->out_len == 0) conn_close(c, idx);
            return;
        }
        compact_in(c);
        if (c->in_len == 0) return;
    }
}

static int handshake(Conn *c, int idx) {
    int r = SSL_accept(c->ssl);
    if (r == 1) {
        c->phase = PH_HTTP;
        c->want_write = 0;
        interest(c, idx);
        return 1;
    }
    int e = SSL_get_error(c->ssl, r);
    if (e == SSL_ERROR_WANT_READ) {
        c->want_write = 0;
        interest(c, idx);
        return 0;
    }
    if (e == SSL_ERROR_WANT_WRITE) {
        c->want_write = 1;
        interest(c, idx);
        return 0;
    }
    conn_close(c, idx);
    return -1;
}

static void on_readable(Conn *c, int idx) {
    if (!c->used) return;
    c->last_ms = mono_ms();
    if (c->phase == PH_HANDSHAKE) {
        handshake(c, idx);
        return;
    }
    if (c->in_len == c->in_cap) {
        compact_in(c);
        if (c->in_len == c->in_cap) {
            queue_err(c, 431, "headers too large");
            flush_out(c, idx);
            if (c->used) conn_close(c, idx);
            return;
        }
    }
    if (c->in_len == c->in_cap) {
        pump(c, idx);
        if (!c->used) return;
    }
    if (c->in_len == c->in_cap) {
        c->pause_read = 1;
        c->want_write = c->out_off < c->out_len;
        interest(c, idx);
        return;
    }
    int n = conn_read(c, c->in + c->in_len, c->in_cap - c->in_len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EINTR) {
            if (c->want_write) interest(c, idx);
            return;
        }
        conn_close(c, idx);
        return;
    }
    if (n == 0) {
        pump(c, idx);
        if (c->used) conn_close(c, idx);
        return;
    }
    c->in_len += (size_t)n;
    pump(c, idx);
    if (c->used) {
        c->pause_read = (c->in_len == c->in_cap);
        interest(c, idx);
    }
}

static void on_writable(Conn *c, int idx) {
    if (!c->used) return;
    if (c->phase == PH_HANDSHAKE) {
        handshake(c, idx);
        return;
    }
    c->want_write = 0;
    pump(c, idx);
    if (c->used && !c->want_write) {
        c->pause_read = (c->in_len == c->in_cap);
        interest(c, idx);
    }
}

static void pause_accept(void) {
    if (!g_accepting) return;
    if (g_http_fd >= 0) epoll_ctl(g_epfd, EPOLL_CTL_DEL, g_http_fd, NULL);
    if (g_tls_fd >= 0) epoll_ctl(g_epfd, EPOLL_CTL_DEL, g_tls_fd, NULL);
    g_accepting = 0;
    g_paused = 1;
}

static void resume_accept(void) {
    if (g_draining || g_accepting) return;
    struct epoll_event ev;
    ev.events = EPOLLIN;
    if (g_http_fd >= 0) {
        ev.data.u64 = ID_HTTP;
        epoll_ctl(g_epfd, EPOLL_CTL_ADD, g_http_fd, &ev);
    }
    if (g_tls_fd >= 0) {
        ev.data.u64 = ID_TLS;
        epoll_ctl(g_epfd, EPOLL_CTL_ADD, g_tls_fd, &ev);
    }
    g_accepting = 1;
    g_paused = 0;
}

static int listen_on(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    const char *bind_addr = getenv("CFORGE_BIND");
    if (!bind_addr || !bind_addr[0]) bind_addr = "127.0.0.1";
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, bind_addr, &addr.sin_addr) != 1) {
        fprintf(stderr, "ERROR bad CFORGE_BIND %s\n", bind_addr);
        close(fd);
        return -1;
    }
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        fprintf(stderr, "ERROR bind %u: %s\n", port, strerror(errno));
        close(fd);
        return -1;
    }
    if (listen(fd, 128) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void accept_loop(int listen_fd, int tls) {
    for (;;) {
        if (g_free_top == 0) {
            pause_accept();
            return;
        }
        int fd = accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            if (errno == EINTR) continue;
            return;
        }
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof one);
        int idx = g_free_stack[--g_free_top];
        Conn *c = &g_conns[idx];
        uint8_t *in = c->in;
        uint8_t *outb = c->out;
        uint8_t *abase = c->arena.base;
        size_t in_cap = c->in_cap;
        size_t out_cap = c->out_cap;
        size_t acap = c->arena.cap;
        memset(c, 0, sizeof *c);
        c->in = in;
        c->out = outb;
        c->in_cap = in_cap;
        c->out_cap = out_cap;
        c->arena.base = abase;
        c->arena.cap = acap;
        c->fd = fd;
        c->used = 1;
        c->tls = tls;
        c->phase = tls ? PH_HANDSHAKE : PH_HTTP;
        c->last_ms = mono_ms();
        g_active++;
        if (tls) {
            c->ssl = SSL_new(g_ssl);
            if (!c->ssl) {
                close(fd);
                c->fd = -1;
                c->used = 0;
                g_active--;
                g_free_stack[g_free_top++] = idx;
                continue;
            }
            SSL_set_fd(c->ssl, fd);
            SSL_set_accept_state(c->ssl);
            SSL_set_mode(c->ssl, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER | SSL_MODE_ENABLE_PARTIAL_WRITE);
        }
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLRDHUP;
        ev.data.u64 = (uint64_t)(ID_CONN + idx);
        if (epoll_ctl(g_epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
            conn_close(c, idx);
            continue;
        }
        if (tls) handshake(c, idx);
        if (g_free_top == 0) pause_accept();
    }
}

static void init_conn_storage(void) {
    size_t in_cap = CFORGE_MAX_HEADER + CFORGE_MAX_BODY;
    for (int i = 0; i < g_max_conns; i++) {
        g_conns[i].in = malloc(in_cap);
        g_conns[i].out = malloc(CFORGE_OUT_CAP);
        g_conns[i].arena.base = malloc(CFORGE_ARENA);
        g_conns[i].arena.cap = CFORGE_ARENA;
        g_conns[i].arena.off = 0;
        g_conns[i].in_cap = in_cap;
        g_conns[i].out_cap = CFORGE_OUT_CAP;
        g_conns[i].fd = -1;
        g_free_stack[g_free_top++] = i;
        if (!g_conns[i].in || !g_conns[i].out || !g_conns[i].arena.base) {
            fprintf(stderr, "ERROR out of memory\n");
            exit(1);
        }
    }
}

static void sweep_idle(void) {
    uint64_t now = mono_ms();
    for (int i = 0; i < g_max_conns; i++) {
        Conn *c = &g_conns[i];
        if (!c->used) continue;
        if (now - c->last_ms < g_idle_ms) continue;
        conn_close(c, i);
    }
}

static char *parent_dir(const char *path, char *buf, size_t cap) {
    size_t n = strlen(path);
    if (n + 1 > cap) return NULL;
    memcpy(buf, path, n + 1);
    char *slash = strrchr(buf, '/');
    if (!slash) return NULL;
    if (slash == buf) {
        slash[1] = 0;
        return buf;
    }
    *slash = 0;
    return buf;
}

static uint16_t env_port(const char *name, uint16_t fallback) {
    const char *e = getenv(name);
    if (!e || !e[0]) return fallback;
    char *end = NULL;
    long v = strtol(e, &end, 10);
    if (!end || *end || v < 0 || v > 65535) {
        fprintf(stderr, "ERROR bad %s\n", name);
        exit(1);
    }
    return (uint16_t)v;
}

int32_t app_listen(uint16_t port) {
    if (port == 0) port = env_port("PORT", 8080);
    const char *db_path = getenv("CFORGE_DB");
    if (!db_path || !db_path[0]) db_path = "data/users.db";
    char parent[512];
    if (parent_dir(db_path, parent, sizeof parent)) {
        if (mkdir(parent, 0755) < 0 && errno != EEXIST) {
            perror("mkdir");
            return 1;
        }
    }
    if (cforge_db_open(db_path) != 0) return 1;

    const char *cert = getenv("CFORGE_TLS_CERT");
    const char *key = getenv("CFORGE_TLS_KEY");
    uint16_t tls_port = 0;
    if (cert && key && cert[0] && key[0]) {
        tls_port = env_port("TLS_PORT", 8443);
        g_ssl = SSL_CTX_new(TLS_server_method());
        if (!g_ssl) {
            fprintf(stderr, "ERROR ssl ctx\n");
            return 1;
        }
        SSL_CTX_set_min_proto_version(g_ssl, TLS1_3_VERSION);
        SSL_CTX_set_max_proto_version(g_ssl, TLS1_3_VERSION);
        SSL_CTX_set_options(g_ssl, SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_COMPRESSION);
        if (SSL_CTX_use_certificate_file(g_ssl, cert, SSL_FILETYPE_PEM) != 1 ||
            SSL_CTX_use_PrivateKey_file(g_ssl, key, SSL_FILETYPE_PEM) != 1 ||
            SSL_CTX_check_private_key(g_ssl) != 1) {
            fprintf(stderr, "ERROR tls cert/key\n");
            ERR_print_errors_fp(stderr);
            return 1;
        }
    }

    const char *maxc = getenv("CFORGE_MAX_CONNS");
    g_max_conns = CFORGE_MAX_CONNS_DEFAULT;
    if (maxc && maxc[0]) {
        int v = atoi(maxc);
        if (v >= 1 && v <= 4096) g_max_conns = v;
    }
    const char *idle = getenv("CFORGE_IDLE_MS");
    if (idle && idle[0]) {
        long v = strtol(idle, NULL, 10);
        if (v >= 50 && v < 3600000) g_idle_ms = (uint64_t)v;
    }

    g_conns = calloc((size_t)g_max_conns, sizeof(Conn));
    g_free_stack = calloc((size_t)g_max_conns, sizeof(int));
    if (!g_conns || !g_free_stack) return 1;
    init_conn_storage();

    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    g_event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    g_epfd = epoll_create1(EPOLL_CLOEXEC);
    g_http_fd = listen_on(port);
    if (g_http_fd < 0 || g_epfd < 0 || g_event_fd < 0) return 1;
    if (tls_port) {
        g_tls_fd = listen_on(tls_port);
        if (g_tls_fd < 0) return 1;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.u64 = ID_EVENT;
    epoll_ctl(g_epfd, EPOLL_CTL_ADD, g_event_fd, &ev);
    ev.data.u64 = ID_HTTP;
    epoll_ctl(g_epfd, EPOLL_CTL_ADD, g_http_fd, &ev);
    if (g_tls_fd >= 0) {
        ev.data.u64 = ID_TLS;
        epoll_ctl(g_epfd, EPOLL_CTL_ADD, g_tls_fd, &ev);
    }

    const char *shown = getenv("CFORGE_BIND");
    if (!shown || !shown[0]) shown = "127.0.0.1";
    fprintf(stderr, "INFO cforge-users http=%s:%u tls=%s db=%s conns=%d\n",
            shown, port, g_tls_fd >= 0 ? "on" : "off", db_path, g_max_conns);
    if (g_tls_fd >= 0) fprintf(stderr, "INFO cforge-users tls=%s:%u\n", shown, tls_port);

    int timeout = g_idle_ms < 1000 ? (int)(g_idle_ms / 2) : 500;
    if (timeout < 50) timeout = 50;

    while (!g_draining || g_active > 0) {
        if (g_stop && !g_draining) {
            g_draining = 1;
            if (g_http_fd >= 0) {
                epoll_ctl(g_epfd, EPOLL_CTL_DEL, g_http_fd, NULL);
                close(g_http_fd);
                g_http_fd = -1;
            }
            if (g_tls_fd >= 0) {
                epoll_ctl(g_epfd, EPOLL_CTL_DEL, g_tls_fd, NULL);
                close(g_tls_fd);
                g_tls_fd = -1;
            }
            g_accepting = 0;
            for (int i = 0; i < g_max_conns; i++) {
                Conn *c = &g_conns[i];
                if (!c->used) continue;
                if (c->in_len == c->in_off && c->out_len == c->out_off && c->phase == PH_HTTP) {
                    conn_close(c, i);
                } else {
                    c->close_after = 1;
                }
            }
            if (g_active == 0) break;
        }
        struct epoll_event events[64];
        int n = epoll_wait(g_epfd, events, 64, timeout);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < n; i++) {
            uint64_t id = events[i].data.u64;
            uint32_t bits = events[i].events;
            if (id == ID_EVENT) {
                uint64_t x;
                ssize_t rn = read(g_event_fd, &x, sizeof x);
                (void)rn;
                continue;
            }
            if (id == ID_HTTP) {
                accept_loop(g_http_fd, 0);
                continue;
            }
            if (id == ID_TLS) {
                accept_loop(g_tls_fd, 1);
                continue;
            }
            if (id < ID_CONN) continue;
            int idx = (int)(id - ID_CONN);
            if (idx < 0 || idx >= g_max_conns) continue;
            Conn *c = &g_conns[idx];
            if (!c->used) continue;
            if (bits & (EPOLLERR | EPOLLHUP)) {
                conn_close(c, idx);
                continue;
            }
            if (bits & EPOLLRDHUP) {
                if (bits & EPOLLIN) on_readable(c, idx);
                if (c->used && c->out_off < c->out_len) on_writable(c, idx);
                if (c->used && c->in_len == c->in_off && c->out_len == c->out_off) conn_close(c, idx);
                continue;
            }
            if ((bits & EPOLLOUT) && c->used) on_writable(c, idx);
            if ((bits & EPOLLIN) && c->used) on_readable(c, idx);
        }
        sweep_idle();
        if (g_draining && g_active == 0) break;
    }

    for (int i = 0; i < g_max_conns; i++) {
        if (g_conns[i].used) conn_close(&g_conns[i], i);
        free(g_conns[i].in);
        free(g_conns[i].out);
        free(g_conns[i].arena.base);
    }
    for (int i = 0; i < g_nroutes; i++) free(g_routes[i].pattern);
    if (g_http_fd >= 0) close(g_http_fd);
    if (g_tls_fd >= 0) close(g_tls_fd);
    if (g_event_fd >= 0) close(g_event_fd);
    if (g_epfd >= 0) close(g_epfd);
    if (g_ssl) SSL_CTX_free(g_ssl);
    cforge_db_close();
    free(g_conns);
    free(g_free_stack);
    fprintf(stderr, "INFO cforge-users stopped\n");
    return 0;
}
