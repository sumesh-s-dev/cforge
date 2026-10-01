# HTTP, routing, serialization, and async models

This document covers application-layer protocols in the CForge backend design: HTTP family semantics, the reference router, JSON and broader serialization, and two async architectures (**Model A** synchronous reactor vs **Model B** executor with continuations).

## HTTP/1.1 (shipped)

The 0.1 parser is a byte scanner over the connection `in[]` buffer. It supports:

- Methods: `GET`, `POST`, `DELETE`
- Versions: `HTTP/1.0`, `HTTP/1.1`
- `Content-Length` bodies up to 65536 bytes
- Required `Host` on HTTP/1.1
- `Connection: close` and HTTP/1.0 keep-alive negotiation
- Query strings stripped before route lookup (`/users/1?trace=1` → `/users/1`)
- Pipelined requests already present in the buffer
- One trailing path parameter (`/users/:id`)

Rejected conditions map to explicit statuses: `400` bad line, `413` oversized body, `431` header limits, `501` chunked `Transfer-Encoding`, `417` `Expect`, `505` unknown HTTP version, `405` wrong method with `Allow`, `404` no route.

Responses are assembled entirely in `out[]` before socket write: status line, `Content-Type`, `Content-Length`, `Connection`, `Server: cforge`, body. No chunked encoder in 0.1; `204` uses zero body length.

Keep-alive defaults: HTTP/1.1 persistent unless `Connection: close`; HTTP/1.0 closes unless client requests keep-alive.

## HTTP/2 and HTTP/3 (planned)

| Feature | HTTP/2 plan | HTTP/3 plan |
|---|---|---|
| Multiplexing | Stream per request on one TCP conn | QUIC streams |
| Header compression | HPACK static/dynamic tables | QPACK |
| Server push | Disabled by default | N/A |
| Integration | ALPN `h2` after TLS | UDP listener + QUIC stack |

Handlers remain `CForgeFn`; the runtime maps stream id → synthetic `Ctx` with per-stream arena slices carved from a connection-level pool. Flow control backpressure surfaces as “pause read on stream” rather than global accept pause.

## Router

**Shipped (0.1):** Routes registered at startup via `app_get` / `app_post` / `app_delete`. Patterns stored as malloc’d strings once; lookup is O(routes) with:

- Exact `memcmp` match on path length and bytes
- Optional single `:name` suffix matching one path segment without `/`

`ctx_param_u64(ctx, 0, &id)` parses decimal `u64`, rejecting empty, leading zeros, non-digits, overflow.

First matching method wins. Path match with wrong method yields `405` and `Allow` listing methods registered for that path pattern.

**Planned extensions:**

- Middleware stack: `app_use(fn)` running before route handlers (auth, request id)
- Host-based virtual hosts and mount prefixes
- Static file route kind with `sendfile` zero-copy path
- OpenAPI-derived route table generation at compile time

## JSON and serialization

**Shipped (0.1):** `json_parse_create` accepts one object with required keys `name` (string) and `age` (unsigned integer). Strictness: no duplicate keys, no unknown keys, no trailing commas, BMP-only `\uXXXX` escapes (no surrogate pairs), UTF-8 validation, max name 256 bytes into request arena.

Response helpers `ctx_json_user`, `ctx_json_id` escape strings for JSON text in `out[]`.

**Ecosystem serialization roadmap:**

| Format | Role | Handler exposure |
|---|---|---|
| JSON | REST APIs, config | `Slice` in/out via runtime |
| MessagePack | Compact RPC | `msgpack_decode_user(ctx, …)` |
| CBOR | IoT / COSE adjacency | Schema-id prefixed blobs |
| Protobuf | gRPC services | Generated C structs + arena views |
| Form urlencoded | HTML forms | Query parser sharing router utilities |

Design rule: parsers are **schema-specific** in early releases to bound complexity; a general DOM JSON API is optional and lives off the hot path.

## Authentication and authorization (planned)

Not in 0.1. Intended layers:

1. **Transport:** mTLS between services
2. **Request:** `Authorization: Bearer` JWT validation (HMAC or Ed25519) in middleware
3. **Session:** Signed cookie + server-side store in Redis
4. **OAuth2:** External IdP; runtime stores only validated claims in `Ctx` extensions

Password hashing (Argon2id) belongs in registration flows, never in handlers as raw string compare.

## Async Model A — synchronous reactor (shipped)

```text
epoll_wait → read → parse → handler() → write → arena reset
```

Properties:

- Single thread, no locks on connection table
- Handler must return quickly; SQLite calls block the loop
- Predictable latency variance under low load
- Simple reasoning about `Slice` lifetimes

This is the 0.1 users service model documented end-to-end in the repository.

## Async Model B — executor and continuations (planned)

```text
epoll_wait → schedule IoTask → on complete resume Continuation → handler stages
```

Components:

- **Reactor thread(s)** — only socket/TLS/timer events
- **Worker pool** — CPU-bound or blocking DB/HTTP client work
- **Channels** — pass owned buffers between threads with refcount or arena transfer
- **`async fn`** — compiler lowers to struct state machine + function pointers

Handler ABI evolution: `CForgeFn` returns `CFORGE_YIELD` to suspend; runtime stores stack frame in connection slot. **Constraint:** no `Slice` pointing at stack across yield unless copied to arena.

Comparison:

| Concern | Model A | Model B |
|---|---|---|
| Throughput on many cores | Limited | Scales with workers |
| Tail latency under blocking DB | Spikes | Isolated to workers |
| Memory | Fixed slots | Queues + continuation storage |
| Debugging | Linear | Harder ordering bugs |

Hybrid deployments may run Model A on edge nodes and Model B behind internal load balancers.

## WebSockets and SSE (planned)

WebSocket upgrade would share the HTTP parser’s header phase, then switch slot mode to frame codec. Server-Sent Events reuse HTTP/1.1 chunked responses with `text/event-stream`. Both stay out of language syntax; registration resembles `app_get` with a protocol flag.

**Shipped:** `GET /ws` performs an RFC 6455 handshake and echoes client text frames (runtime-built; not registered via `app_get`).

## Caching semantics at HTTP layer

`ETag` / `If-None-Match` and `Cache-Control` for static assets are **planned**. Dynamic JSON APIs default to `Cache-Control: no-store` unless handlers set headers via future `ctx_header` API.

## Error responses

Runtime-generated errors use fixed bodies or empty bodies per status. Application errors use `ctx_text` / `ctx_status`. **Shipped:** `ctx_problem` returns Problem Details (`application/problem+json`) with `title`, `detail`, and `status` fields (used on invalid create-user JSON).

## Testing implications

Integration tests in `./cforge test` cover pipelining, split header writes, TLS 1.3, WebSocket `/ws` echo, and graceful `SIGTERM`. Async Model B will need stress tests for continuation leaks and ordered response writes on one connection.

See [networking.html](networking.html) for sockets and [memory.html](memory.html) for buffer lifetimes during pipelining.
