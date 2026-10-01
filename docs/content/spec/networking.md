# Networking and transport

CForge’s reference server is built on **Linux BSD sockets** and **edge-triggered readiness** via `epoll`. This document specifies transport behavior for the shipped users service and the broader ecosystem design for UDP, connection tuning, TLS as a transport adapter, and future kernel interfaces.

## TCP server model

**Shipped (0.1):** One OS thread owns one `epoll` instance. Listen sockets for plain HTTP and optional TLS are registered with distinct user-data tags. Accepted connections become non-blocking (`accept4` with `SOCK_NONBLOCK | SOCK_CLOEXEC`), with `TCP_NODELAY` and `SO_KEEPALIVE` enabled.

Connection state lives in a **fixed slot table** sized by `CFORGE_MAX_CONNS` (default 128, clamped 1–4096 via environment). Each slot tracks:

- File descriptor and optional `SSL *`
- Read buffer cursor (`in_off`, `in_len`) over a preallocated `in[]` slab
- Write state over `out[]`
- Last activity timestamp for idle close
- Epoll interest mask (`EPOLLIN`, `EPOLLOUT`)

When all slots are busy, listen sockets are **removed from epoll** so the kernel backlog absorbs pressure (`accept_paused` metric). This is deliberate backpressure: the process does not accept fds it cannot service.

## Read and write semantics

Reads append to `in[]` until `EAGAIN`. Partial HTTP requests stay in place; the parser returns “need more bytes” without consuming incomplete data. When the user read buffer is full, `EPOLLIN` is dropped until the handler consumes or rejects the request—preventing busy loops on slow clients.

Writes buffer the full HTTP response in `out[]` before the first `write` or `SSL_write`. If the syscall returns `EAGAIN`, `EPOLLOUT` is armed and the event loop resumes when the socket is writable. `SIGPIPE` is ignored; closed peers surface as `EPIPE` and trigger slot teardown.

Idle connections are scanned on `epoll_wait` timeout (approximately 500 ms, or half of `CFORGE_IDLE_MS` when under one second). Slots exceeding the idle budget close regardless of keep-alive negotiation—this doubles as slow-client protection for half-sent headers.

## epoll user-data encoding

**Shipped (0.1)** uses small integer cookies in `epoll_event.data.u64`:

| Cookie | Source |
|---|---|
| `1` | `eventfd` for shutdown (`SIGTERM` / `SIGINT`) |
| `2` | HTTP listen socket |
| `3` | TLS listen socket |
| `10 + index` | Connection slot `index` |

Signal handlers only set a flag and write eight bytes to the eventfd; they do not allocate. Shutdown closes listeners first, marks in-flight connections for close-after-response, then frees global slabs when `active_connections` reaches zero.

**Planned:** Per-listener load metrics, `EPOLLONESHOT` for accept thundering herds on multi-listener deployments, and optional `SO_REUSEPORT` fan-out across worker processes.

## UDP (design)

Release 0.1 does not expose UDP in the runtime API. The ecosystem design includes:

| Use case | API shape | Notes |
|---|---|---|
| DNS resolution | Blocking `getaddrinfo` off hot path | Cached in startup or worker thread |
| QUIC / HTTP/3 | UDP socket + userspace stack | Separate from epoll HTTP/1 loop initially |
| Metrics push | Connected UDP to collector | Fire-and-forget with drop counter |
| Game / RTC | Dedicated reactor thread | Not mixed with HTTP slot table |

UDP endpoints would register parallel epoll entries with the same slot discipline or a smaller datagram ring buffer to avoid per-packet malloc.

## TLS as transport

TLS terminates in the runtime before bytes are interpreted as HTTP. **Shipped (0.1):** OpenSSL drives `SSL_accept`, `SSL_read`, `SSL_write` on the connection slot. Plaintext HTTP still lands in `in[]`; ciphertext never escapes to handlers.

Context configuration:

- TLS 1.3 only (min and max protocol version pinned)
- Renegotiation and compression disabled
- PEM certificate and private key from environment paths
- Handshake failure closes the socket with no HTTP response

Dual listeners: HTTP on `PORT` (default 8080), HTTPS on `TLS_PORT` (default 8443) when cert and key are set. **Planned:** ALPN for `h2`, session tickets, OCSP stapling, mutual TLS for service-to-service calls.

## Addressing and bind policy

`CFORGE_BIND` defaults to `127.0.0.1`. Production guidance: bind loopback behind a reverse proxy unless the deployment model requires direct exposure. IPv6 listener support is **planned** (`getaddrinfo` with `AI_PASSIVE`, separate slot limits per address family).

## Connection lifecycle diagram

```text
        listen (HTTP/TLS)
              │
              ▼
           accept4 ──► slot allocate
              │
              ├─► [TLS] SSL_accept loop until OK or fatal
              │
              ▼
         EPOLLIN ──► read ──► HTTP parser
              │
              ▼
           handler
              │
              ▼
         EPOLLOUT ◄── write until drained or EAGAIN
              │
              ├─► keep-alive ──► wait EPOLLIN
              └─► close / idle / shutdown flag ──► SSL_free, close(fd), free slot
```

## Kernel alternatives (roadmap)

| API | When to adopt | Tradeoff |
|---|---|---|
| `epoll` | Default single-thread server | Simple, mature |
| `io_uring` | High QPS, batch reads | Complexity, kernel version floor |
| Thread pool + `poll` | Portable non-Linux | Not a CForge 0.1 target |

Any migration keeps the **handler ABI** unchanged: only the reactor implementation swaps.

## Observability hooks

Per-connection counters feed `/metrics` in 0.1: `requests_total`, `active_connections`, `db_errors`, `accept_paused`. **Planned:** histogram of accept-to-response latency, TLS handshake failures, read/write `EAGAIN` counts, and epoll wake reasons tagged by cookie type.

## Failure matrix

| Event | Action |
|---|---|
| Peer reset during read | Drop slot, decrement active |
| Malformed TLS alert | Close slot, no HTTP body |
| Listen backlog full | Kernel drops SYN; client retries |
| `EMFILE` on accept | Log once, pause accept until slot frees |

Networking detail for HTTP parsing and routing continues in [http-async.html](http-async.html). Memory ownership for buffers is in [memory.html](memory.html).
