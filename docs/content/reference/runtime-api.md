# Runtime API

Declared in `runtime/cforge_rt.h`. Implemented in C. Link automatically when building via `./cforge build`.

## Slice

```c
typedef struct Slice {
    uint8_t *ptr;
    size_t len;
} Slice;
```

`slice_empty()` → zero-length slice.

## Context

Opaque `Ctx *`. Handlers use runtime helpers only.

## HTTP / response

| Function | Purpose |
|---|---|
| `ctx_status(ctx, code)` | Status, empty body |
| `ctx_text(ctx, code, body)` | Plain text body |
| `ctx_problem(ctx, code, title, detail)` | `application/problem+json` body |
| `ctx_json_user(ctx, code, id, name, age)` | JSON user object |
| `ctx_json_id(ctx, code, id)` | JSON `{"id":N}` |
| `ctx_metrics(ctx)` | Prometheus-style text metrics |
| `ctx_param_u64(ctx, index, &out)` | Parse `:id` route param |

Return `0` on success, non-zero on parse failure.

## JSON

| Function | Purpose |
|---|---|
| `json_parse_create(ctx, &name, &age)` | Parse POST body `{name, age}` |

## Database (SQLite or Postgres via env)

| Function | Returns |
|---|---|
| `db_get_user(ctx, id, &oid, &name, &age)` | 0 ok, 1 not found, -1 error |
| `db_insert_user(ctx, name, age, &id)` | 0 ok, -1 error |
| `db_delete_user(ctx, id)` | 0 ok, 1 not found, -1 error |

Backend selected by `CFORGE_PG_DSN` (libpq) or `CFORGE_DB` (SQLite pool). Metrics include `db_backend`.

## Server

| Function | Purpose |
|---|---|
| `app_get(path, fn)` | Register GET route |
| `app_post(path, fn)` | Register POST route |
| `app_delete(path, fn)` | Register DELETE route |
| `app_listen(port)` | Block in event loop; `0` → env `PORT` |

`GET /ws` WebSocket echo is implemented in the runtime (not via `app_*`).

Handler type: `int32_t handler(Ctx *ctx)`.

## Errors

No exceptions. Use integer return codes and branch in the handler.
