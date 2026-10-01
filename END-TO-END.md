# CForge users service, end to end

**Documentation site:** https://sumesh-s-dev.github.io/cforge/

This document describes the system in `/home/knight/Projects/cforge` as it is built and deployed. It is the path from a `.cforge` source file to a live HTTP and TLS process. It is not the full backend design (no PostgreSQL wire protocol, HTTP/2, HTTP/3, Kafka, or a package registry).

A request travels through these layers:

```text
Client
  → TCP accept (epoll)
  → TLS 1.3 record layer, only if the socket is the TLS listener
  → HTTP/1.1 parser
  → route table
  → handler compiled from app/users.cforge
  → JSON and/or SQLite
  → response bytes
  → write / SSL_write
  → arena reset and database release
```

The language does not know what HTTP, JSON, TLS, or SQL are. Those live in the C runtime. The `.cforge` file only calls runtime functions and branches on their return codes.

## What is running

| Item | Value |
|---|---|
| Unit | `cforge-users.service` (systemd user) |
| Binary | `/home/knight/Projects/cforge/build/cforge-users` |
| HTTP | `http://127.0.0.1:8080` |
| TLS | `https://127.0.0.1:8443` (TLS 1.3 only, self-signed certificate) |
| Database | `/home/knight/Projects/cforge/data/users.db` (SQLite, WAL) |
| Certificate | `/home/knight/Projects/cforge/deploy/cert.pem` |
| Private key | `/home/knight/Projects/cforge/deploy/key.pem` |
| Bind address | `127.0.0.1` unless `CFORGE_BIND` is changed |

`curl` against the TLS port needs `-k` because the certificate is generated locally and is not in a public trust store.

## Repository layout

```text
cforge
├── cforge                 toolchain: lex, parse, C codegen, cc, test, deploy
├── cforge.toml            package name and entry path
├── app/users.cforge       the service source
├── runtime
│   ├── cforge_rt.h        public runtime API (what .cforge calls)
│   ├── internal.h         Ctx, Arena, limits
│   ├── server.c           epoll, HTTP, router, TLS, responses
│   ├── json.c             create-user JSON parser
│   └── db.c               SQLite prepared statements
├── build/cforge-users     native binary
├── build/users.gen.c      generated C (rebuilt by ./cforge build)
├── data/users.db          persistent users table
└── deploy/                TLS certificate and key
```

## Layer boundary

```text
app/users.cforge          application handlers
        │
runtime C API             Slice, Ctx, routes, JSON, SQLite, app_listen
        │
Linux                     epoll, sockets, OpenSSL, libsqlite3
        │
kernel / CPU
```

| Belongs in the `.cforge` file | Belongs in the runtime |
|---|---|
| Which routes exist | `socket`, `bind`, `listen`, `accept`, `epoll_wait` |
| Status codes and SQL-shaped calls | HTTP parse, keep-alive, idle timeout |
| Explicit `Result`-style integer codes (`0`, `1`, `-1`) | Buffer slabs, arenas, backpressure |
| | TLS handshake and record crypto |
| | SQLite prepare, bind, step |

There is no garbage collector, no hidden allocator on the handler path, and no async runtime. `main` calls `app_listen`, and that function is the event loop.

## Language subset

`./cforge` compiles a small statement language to C11. The entry file is `app/users.cforge`.

Accepted forms:

```text
fn name(param: Type, ...) -> Type { ... }
let name: Type = expr;
if expr { ... } else { ... }
while expr { ... }
return expr;
name(args);
```

Types that map straight onto C:

| CForge | C |
|---|---|
| `i32` | `int32_t` |
| `u32` | `uint32_t` |
| `u64` | `uint64_t` |
| `i64` | `int64_t` |
| `u16` | `uint16_t` |
| `u8` | `uint8_t` |
| `bool` | `int32_t` |
| `Slice` | `struct { uint8_t *ptr; size_t len; }` |
| `*Ctx` | `Ctx *` |
| `*u64`, `*u32`, `*Slice` | pointer to that C type |

`Slice` is two words. Copying a `Slice` copies the pointer and the length. It does not allocate and it does not copy the bytes. A string literal becomes a `Slice` pointing at the static bytes of that literal, with `len` set to the byte length.

`struct` is a reserved word. Top-level struct declarations are not compiled yet. Handlers use runtime functions instead of user-defined structs.

`main` is emitted as `cforge_main`. The generated file adds a C `main` that calls it. Other functions stay `static` in `build/users.gen.c` and are passed to `app_get` / `app_post` / `app_delete` as function pointers.

Calls may target functions defined in the same file or the runtime list in `cforge`:

```text
slice_empty  ctx_param_u64  ctx_status  ctx_text
ctx_json_user  ctx_json_id  ctx_metrics
json_parse_create
db_get_user  db_insert_user  db_delete_user
app_get  app_post  app_delete  app_listen
```

Integer returns are the error policy. There is no exception unwinding.

| Code | Meaning for database calls |
|---|---|
| `0` | Success |
| `1` | Row missing (`GET` / `DELETE`) |
| `-1` | Parse failure, constraint, or SQLite error |

## Compile and link

```text
app/users.cforge
    → lexer / parser          (./cforge)
    → build/users.gen.c
    → cc -std=c11 -O2 -g -Wall -Wextra -Werror
        users.gen.c
        runtime/server.c
        runtime/json.c
        runtime/db.c
    → build/cforge-users
```

Linked libraries: `libsqlite3`, `libssl`, `libcrypto`, `pthread`.

`cforge.toml` only selects the entry file:

```toml
[package]
name = "users"
entry = "app/users.cforge"
```

Commands:

```text
./cforge build     compile and link
./cforge run       build, then replace this process with the binary
./cforge test      build, start on ports 18081 and 18443, run 14 checks, SIGTERM
./cforge deploy    build, write the user unit, enable and start it, wait for /health
./cforge e2e       test, deploy, full users API smoke on :8080
./cforge deploy-docker   Docker Compose: Postgres + app on :8080 (needs Docker)
./cforge status    GET /health and GET /metrics on port 8080
```

## Process model

One OS thread. One `epoll` set. No thread pool and no work stealing.

The listen sockets and an `eventfd` share that set:

| `epoll` user data | Source |
|---|---|
| `1` | `eventfd` written by `SIGTERM` / `SIGINT` |
| `2` | HTTP listen socket |
| `3` | TLS listen socket, if certificates are configured |
| `10 + index` | a connection slot |

`SIGPIPE` is ignored so a write to a closed socket returns `EPIPE` instead of killing the process. The signal handlers only set a flag and write 8 bytes to the `eventfd`. They do not allocate.

Connections are non-blocking (`accept4` with `SOCK_NONBLOCK | SOCK_CLOEXEC`). Accepted sockets get `TCP_NODELAY` and `SO_KEEPALIVE`.

## Memory

Startup allocates a fixed slab. Nothing in the request path calls `malloc`.

Default limits (`runtime/internal.h`):

| Constant | Value | Role |
|---|---|---|
| `CFORGE_MAX_CONNS_DEFAULT` | 128 | connection slots |
| `CFORGE_MAX_HEADER` | 8192 | header bytes |
| `CFORGE_MAX_BODY` | 65536 | body bytes |
| `CFORGE_MAX_NAME` | 256 | user name |
| `CFORGE_ARENA` | 4096 | per-connection bump allocator |
| `CFORGE_OUT_CAP` | 16384 | response buffer |
| `CFORGE_MAX_ROUTES` | 64 | registered routes |
| `CFORGE_IDLE_MS_DEFAULT` | 30000 | idle close |

Each slot owns:

```text
in buffer     8192 + 65536 bytes    read bytes, including a partial request
out buffer    16384 bytes           the full response before and during write
arena         4096 bytes            decoded JSON strings and copied SQL columns
```

The read buffer is large enough for one maximum header block plus one maximum body. The arena is a bump pointer. `cforge_arena_reset` sets `off = 0`. It does not run destructors.

Who owns what during `POST /users`:

| Bytes | Where they live | Freed when |
|---|---|---|
| Socket and TLS object | connection slot | `close` / `SSL_free` on connection close |
| Raw HTTP body | `in` buffer | cursor advances after the handler returns |
| Decoded `name` | request arena | `cforge_arena_reset` after the handler |
| Stored user row | SQLite (it copies on bind with `SQLITE_TRANSIENT`) | `DELETE`, or process exit |
| JSON response | `out` buffer | after `write` consumes it |

A `Slice` returned by `db_get_user` points into the arena. The JSON writer copies those bytes into `out` before the arena is reset. Holding that `Slice` after the handler returns would be a dangling pointer. The handlers do not do that.

## Connection lifecycle

```text
accept
  → take a free slot index
  → plain HTTP, or SSL_new + SSL_accept until the handshake finishes
  → read into in[]
  → parse one request
  → call the handler
  → reset the arena and release the SQLite statement
  → write out[] until it is empty or EAGAIN
  → if keep-alive, wait for the next request on the same fd
  → if Connection: close, drain, or idle timeout, close the fd and return the slot
```

`EAGAIN` on write arms `EPOLLOUT` and stops the loop from spinning. When the user buffer is full, read interest is dropped so a full socket does not busy-wake the loop. Freeing a slot turns accept back on.

If every slot is in use, the listen sockets are removed from `epoll`. The kernel keeps the backlog. Clients stall instead of the process accepting unbounded fds. `accept_paused` in `/metrics` is `1` while that is true.

Idle connections are scanned on the `epoll_wait` timeout (about 500 ms, or half of `CFORGE_IDLE_MS` when that value is under one second). A slot whose last read or write is older than the idle limit is closed. That is also the slow-client timeout: a peer that stops sending after a partial header is closed.

## HTTP/1.1

The parser is a byte scan over `in[in_off, in_len)`. A short read returns “need more” and leaves the bytes in place. The next `EPOLLIN` appends and parses again.

Handled:

- `GET`, `POST`, `DELETE`
- `HTTP/1.0` and `HTTP/1.1`
- `Content-Length` bodies
- `Host` required on HTTP/1.1
- `Connection: close` and HTTP/1.0 keep-alive
- query strings are stripped before routing (`/users/1?x=1` routes as `/users/1`)
- pipelined requests already sitting in the buffer
- one `:param` at the end of a path (`/users/:id`)

Rejected:

| Condition | Status |
|---|---|
| Bad request line, missing `Host`, duplicate `Content-Length` | 400, then close |
| `Content-Length` above 65536 | 413, then close |
| Headers over 8192 bytes, or more than 64 header lines | 431, then close |
| `Transfer-Encoding` (chunked is not implemented) | 501, then close |
| `Expect` | 417, then close |
| Any other HTTP version | 505, then close |
| Path matches a route but the method does not | 405 with an `Allow` header |
| No route | 404 |

HTTP/1.1 defaults to keep-alive. HTTP/1.0 defaults to close unless the client sends `Connection: keep-alive`.

Responses are written in full into `out` before the first socket write: status line, `Content-Type`, `Content-Length`, `Connection`, `Server: cforge`, then the body. There is no chunked response encoder. `204` is `Content-Length: 0` and an empty body.

## Router

Routes are copied into malloc’d strings at startup, when `app_get` / `app_post` / `app_delete` run, before `app_listen`. Lookup does not allocate.

`app/users.cforge` registers:

```text
GET    /health
GET    /metrics
GET    /users/:id
POST   /users
DELETE /users/:id
```

An exact pattern matches with `memcmp`. A pattern containing one trailing `:name` matches a prefix plus one path segment that does not contain `/`. `ctx_param_u64(ctx, 0, &id)` parses that segment. It rejects an empty segment, a leading zero (`01`), a non-digit, and overflow past `u64`.

First matching method wins. If the path matches some other method, the response is `405`.

## Handlers

`Ctx` is filled on the stack for each request. The handler sees the path, the body, up to four parameters, and the output buffer. It does not see the file descriptor.

`POST /users` in `app/users.cforge`:

1. `json_parse_create` reads `ctx->body` and writes `name` into the arena and `age` into a `u32`.
2. Empty names and names longer than 256 bytes become `400`.
3. `db_insert_user` binds the name and age and returns the new row id.
4. `ctx_json_id` writes `{"id":N}` and `201`.

`GET /users/:id` parses the id, loads the row, and writes `{"id":N,"name":"...","age":N}`. A missing row is `404`. A SQLite failure is `503`.

`DELETE /users/:id` is `204` when a row was removed and `404` when `sqlite3_changes` is zero.

`/health` writes `ok`. `/metrics` writes four counters:

```text
requests_total
active_connections
db_errors
accept_paused
```

`active_connections` includes the connection serving `/metrics`.

## JSON

`json_parse_create` accepts one object and only the keys `name` (string) and `age` (integer). Both are required. Duplicate keys, unknown keys, trailing commas, fractions, negative ages, and trailing junk fail the parse. The handler turns that failure into `400 bad json`.

Strings are checked as UTF-8. Escapes are `\"`, `\\`, `\/`, `\b`, `\f`, `\n`, `\r`, `\t`, and `\uXXXX` for a single BMP code point. Surrogate pairs are rejected. The decoded name is capped at 256 bytes and stored in the request arena.

Response strings are escaped into the output buffer (`"`, `\`, and control characters). That write is encoding, not a copy of the raw HTTP body.

This parser is not a general JSON library. It only understands the create-user body.

## SQLite

One connection for the process, opened in `app_listen` before the listen sockets. The schema:

```sql
CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    age INTEGER NOT NULL
);
```

`PRAGMA journal_mode=WAL` and `PRAGMA synchronous=NORMAL` run at open. Three statements are prepared once:

```sql
INSERT INTO users(name, age) VALUES (?1, ?2)
SELECT id, name, age FROM users WHERE id = ?1
DELETE FROM users WHERE id = ?1
```

Parameters are bound. The SQL text is not concatenated with user input. `SQLITE_TRANSIENT` makes SQLite copy the name out of the arena during `INSERT`. `SELECT` copies the column into the arena before `sqlite3_reset`, because SQLite invalidates column pointers on reset.

Each database call sets a checkout flag. After the handler returns, `cforge_db_release` resets every statement and clears bindings. The flag is the stand-in for “return this connection to the pool.” There is one connection, so a pool of many server connections is not implemented. A second request cannot overlap a statement because the thread is the only caller.

Text columns are copied. Integers are read into registers. That copy is required so the SQLite buffer can be reused for the next statement.

## TLS

TLS is optional and is on in the deployed unit because `CFORGE_TLS_CERT` and `CFORGE_TLS_KEY` are set.

OpenSSL is the crypto implementation. The runtime owns the handshake state machine only in the sense that it drives `SSL_accept`, `SSL_read`, and `SSL_write` on the same connection slot and the same `in` / `out` buffers. Plaintext for HTTP still lands in `in`. Ciphertext stays inside OpenSSL.

The context is TLS 1.3 only (`SSL_CTX_set_min_proto_version` and `set_max_proto_version`). Renegotiation and compression are turned off. The certificate file and key file are PEM. Handshake failure closes the socket and does not produce an HTTP response.

`deploy/cert.pem` is a local P-256 certificate (`CN=localhost`) created by `./cforge deploy` if the files are missing. It is not a production CA certificate.

## Backpressure and shutdown

Backpressure in this process:

- All connection slots used: stop accepting. The TCP backlog fills.
- User read buffer full: drop `EPOLLIN` until bytes are consumed.
- `write` or `SSL_write` returns `EAGAIN`: arm `EPOLLOUT` and wait.
- SQLite `busy_timeout` is 1000 ms. There is no second thread, so this only matters if another process opens the same file.

`SIGTERM` or `SIGINT`:

```text
eventfd wakes epoll_wait
  → close the listen sockets (port can be reused)
  → close idle connections immediately
  → mark in-flight connections close-after-response
  → when active_connections hits 0, free slabs, finalize SQLite, exit 0
```

The systemd unit uses `Restart=on-failure`. A clean `SIGTERM` exits 0, so a stop does not restart the process. A crash does.

## Failure behavior

| Failure | What the process does |
|---|---|
| Client closes mid-request | drop the slot |
| Malformed HTTP | 400 and close |
| Bad JSON or bad id | 400, connection may stay open |
| Unknown path | 404 |
| Wrong method | 405 |
| SQLite error | 503, `db_errors` increments, error line on stderr |
| Arena or response buffer cannot fit the reply | 500 |
| Idle longer than `CFORGE_IDLE_MS` | close |
| TLS alert or bad handshake | close |

Logs are one line per request on stderr, which systemd captures:

```text
INFO status_queued method=GET path=/health
```

The line records that a response was queued, not the numeric status. The status is in the bytes written to the socket.

## Environment

| Variable | Default | Effect |
|---|---|---|
| `PORT` | `8080` when `app_listen(0)` | HTTP port |
| `TLS_PORT` | `8443` if a certificate is configured | TLS port |
| `CFORGE_BIND` | `127.0.0.1` | IPv4 address passed to `bind` |
| `CFORGE_DB` | `data/users.db` | SQLite path; the parent directory is created |
| `CFORGE_TLS_CERT` | unset | PEM certificate; together with the key, turns TLS on |
| `CFORGE_TLS_KEY` | unset | PEM private key |
| `CFORGE_MAX_CONNS` | `128` | slot count, clamped to 1..4096 |
| `CFORGE_IDLE_MS` | `30000` | idle close, clamped to 50..3599999 |

`CFORGE_BIND=0.0.0.0` listens on all IPv4 interfaces. The deployed unit does not do that.

## HTTP API

`POST /users`

```http
POST /users HTTP/1.1
Host: 127.0.0.1:8080
Content-Type: application/json

{"name":"ada","age":36}
```

`201` body: `{"id":1}`

`GET /users/1` → `200` `{"id":1,"name":"ada","age":36}`

`DELETE /users/1` → `204` empty body

`GET /health` → `200` `ok`

`GET /metrics` → `200` text counters

## Deployed unit

`~/.config/systemd/user/cforge-users.service`:

```ini
[Service]
ExecStart=/home/knight/Projects/cforge/build/cforge-users
WorkingDirectory=/home/knight/Projects/cforge
Environment=PORT=8080
Environment=TLS_PORT=8443
Environment=CFORGE_BIND=127.0.0.1
Environment=CFORGE_DB=/home/knight/Projects/cforge/data/users.db
Environment=CFORGE_TLS_CERT=/home/knight/Projects/cforge/deploy/cert.pem
Environment=CFORGE_TLS_KEY=/home/knight/Projects/cforge/deploy/key.pem
Restart=on-failure
```

```text
systemctl --user status cforge-users
systemctl --user restart cforge-users
systemctl --user stop cforge-users
```

The unit is enabled, so it starts again at user login. `./cforge deploy` rewrites this file from the toolchain and waits until `GET /health` returns `ok`.

## Tests

`./cforge test` uses a separate database under `build/` and ports `18081` (HTTP) and `18443` (TLS), so it does not touch the deployed database. It checks:

- `GET /health`
- rejected JSON and an empty name
- create, get, delete, get-after-delete
- `POST /health` is `405`
- an unknown path is `404`
- `/metrics` contains `requests_total`
- a request whose headers arrive in two TCP writes
- two pipelined requests on one connection
- TLS 1.3 on the test port
- `SIGTERM` exits 0
- native machine programs (`mach/add.cforge`, `mach/expr.cforge`) via `./cforge machine-test` (also run at the end of `./cforge test`)

## Publish on GitHub

Repository: [github.com/sumesh-s-dev/cforge](https://github.com/sumesh-s-dev/cforge)

```bash
git clone git@github.com:sumesh-s-dev/cforge.git
cd cforge
./cforge build
./cforge test
```

GitHub Actions (`.github/workflows/ci.yml`) installs `gcc`, SQLite, and OpenSSL on Ubuntu, then runs `./cforge test`. Local TLS certificates and `data/users.db` are not committed; generate certs with `./cforge deploy` or the `openssl` command in the deploy section of this document.

## Machine backend (second pipeline)

Some programs compile to ELF without gcc:

```bash
./cforge machine mach/add.cforge -o build/mach-add
./build/mach-add   # exit status 5
./cforge machine-test
```

Registers `r0`–`r7` map to System V argument registers. Raw instructions (`add r0, r1; ret;`) and small `return a + b` forms lower to x86-64 in `cforge_mach.py`. The users HTTP server still uses the C runtime path (`./cforge build`).

## Full stack verification

```bash
./cforge everything    # check, lockfile, test, deploy, smoke, docs-site build, deploy-docker if docker exists
./cforge e2e           # faster: test + systemd deploy + smoke only
```

Also shipped: **WebSocket** echo on `GET /ws`, optional **Redis** (`CFORGE_REDIS_URL`), **Postgres** (`CFORGE_PG_DSN`), **Docker Compose**, and **Problem Details** JSON for some 400 responses.

## What this slice does not do

Roadmap items (spec documented; not in the binary yet):

- HTTP/2, HTTP/3, chunked request bodies
- Kafka, NATS, RabbitMQ
- JWT, OAuth, sessions, password hashing
- OpenTelemetry / histogram metrics, worker pools
- full `@safe` borrow checking, generics, `async fn`
- public package registry with semver resolver

Use-after-free and data races are still possible. Protection is the request arena’s short lifetime, parser bounds checks, and single-threaded I/O.
