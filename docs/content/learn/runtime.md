# Runtime and HTTP

The **runtime** is a C library: epoll event loop, HTTP/1.1 parser, router, JSON helper, SQLite access, optional TLS 1.3 (OpenSSL).

Application code never calls `epoll` directly in 0.1 — it calls `app_listen` and handler helpers.

## Starting the server

```forge
fn main() -> i32 {
    app_get("/health", health);
    app_post("/users", post_user);
    return app_listen(0);
}
```

Routes are registered at startup (fixed table, max 64 routes). Parameters: `/users/:id` → `ctx_param_u64(ctx, 0, &id)`.

## Handler context

Handlers receive `*Ctx` with:

- parsed path and body as `Slice`
- request arena for decoded strings
- output buffer via `ctx_json_*`, `ctx_text`, `ctx_status`

## HTTP features (0.1)

| Feature | Supported |
|---|---|
| GET POST DELETE | yes |
| HTTP/1.0 / 1.1 | yes |
| Keep-alive, pipelining | yes |
| Content-Length bodies | yes |
| Chunked request bodies | no (501) |
| HTTP/2 / 3 | no |

## Backpressure

- Full connection slab → stop accepting
- Full read buffer → drop EPOLLIN
- Write EAGAIN → EPOLLOUT

## TLS

When `CFORGE_TLS_CERT` and `CFORGE_TLS_KEY` are set, a second listener serves TLS 1.3 only.

## Full specification

See repository `END-TO-END.md` for byte-level lifecycle, error codes, and shutdown (`SIGTERM`).

## Runtime API

[Runtime API reference](../reference/runtime-api.html)
