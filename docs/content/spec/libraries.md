# Libraries: packages, config, auth, errors, logging, observability

Beyond HTTP and storage, a production CForge deployment composes **libraries** linked into the runtime or loaded as plugins. None of these are language syntax; they expose C ABI entry points and environment hooks. This page catalogs the ecosystem design and what release 0.1 actually links.

## Package model

**Shipped (0.1):** `cforge.toml` is minimal:

```toml
[package]
name = "users"
entry = "app/users.cforge"
```

The `cforge` driver resolves the entry file, emits `build/<name>.gen.c`, compiles runtime sources, and links a single binary. There is no remote registry, version solver, or transitive dependencies.

**Planned package manifest:**

```toml
[package]
name = "users"
version = "0.2.0"
entry = "app/users.cforge"

[dependencies]
cforge-http = "1.0"
cforge-pg = "0.3"

[features]
default = ["tls"]
tls = ["cforge-http/tls"]
```

Packages publish as source trees or static archives with a `cforge.lock` pinning hashes. `cforge vendor` copies dependencies into `vendor/` for reproducible CI. The compiler whitelists callable symbols per dependency’s `exports.txt`.

## Configuration

**Shipped (0.1):** Environment variables only (`PORT`, `TLS_PORT`, `CFORGE_BIND`, `CFORGE_DB`, `CFORGE_PG_DSN`, `CFORGE_REDIS_URL`, `CFORGE_DB_POOL`, TLS paths, `CFORGE_MAX_CONNS`, `CFORGE_IDLE_MS`). No config file parser in the binary.

**Planned layers:**

| Source | Precedence | Use |
|---|---|---|
| CLI flags | Highest | One-off overrides |
| Environment | High | 12-factor containers |
| `cforge.toml` `[env]` | Medium | Defaults in repo |
| File `cforge.yaml` | Low | Structured multi-section config |

Hot reload: SIGHUP or admin endpoint reloads non-secret sections; secrets rotate via file watch on symlink swap. Typed accessors `config_u16("server.port")` avoid string typos in handlers.

## Authentication and authorization

**0.1:** No auth on the users API (localhost deployment assumption).

**Planned library `cforge-auth`:**

- Password verify: Argon2id with per-user salt stored in DB
- JWT issue/validate: Ed25519 keys, `exp`/`iss`/`aud` checks, constant-time compare
- API keys: HMAC-signed tokens in header `X-Api-Key`
- RBAC: role claims in JWT mapped to route middleware allowlists

Middleware runs as `CForgeFn` wrappers registered before route handlers. Failed auth returns `401`/`403` without invoking business logic.

OAuth2 authorization code flow belongs in a separate admin binary or edge gateway; the service runtime only validates bearer tokens.

## Errors

Two layers:

1. **Runtime protocol errors** — fixed HTTP statuses before handlers (malformed request)
2. **Application errors** — integer codes from `db_*`, `json_*`, handler logic

**Planned standardization:**

- `cforge_error` enum in headers for tooling
- Problem Details JSON (`type`, `title`, `status`, `detail`, `instance`)
- Correlation id header `X-Request-Id` generated at accept, stored in `Ctx`, echoed in logs

No stack traces to clients in production; `detail` is sanitized.

## Logging

**Shipped (0.1):** One stderr line per queued response:

```text
INFO status_queued method=GET path=/health
```

systemd journal captures stderr. Status numeric code is not in the log line—only in wire bytes.

**Planned structured logging:**

- Key-value fields: `ts`, `level`, `request_id`, `method`, `path`, `status`, `duration_us`, `db_wait_us`
- Log levels: ERROR, WARN, INFO, DEBUG (compile-time strip DEBUG in release)
- Optional JSON lines to stdout for Loki/Elasticsearch
- Secret redaction filter on `Authorization` and cookie headers

Signal-safe logging on critical paths: preformatted ring buffer written from signal handler for crash dumps only.

## Observability

**Shipped (0.1):** `GET /metrics` text format includes:

```text
requests_total
active_connections
db_errors
accept_paused
db_backend
redis_backend
redis_errors
ws_upgrades
```

`/health` liveness and `/ready` readiness are shipped. Histograms and OpenTelemetry remain planned.

Cardinality control: aggregate by route pattern, not raw path (avoid `:id` explosion).

**Planned telemetry stack:** OpenTelemetry tracing, profiling admin port, histogram latency on `/metrics`.

## Security libraries (cross-cutting)

- **TLS:** OpenSSL today; consider BoringSSL build flavor
- **Crypto:** libsodium for new code paths (JWT, cookies)
- **Input validation:** max lengths enforced in runtime parsers, duplicated in handlers only for domain rules
- **Rate limiting:** token bucket in Redis or in-process sharded counters
- **CSRF:** relevant for cookie sessions on HTML forms, not JSON API default

See [toolchain.html](toolchain.html) for supply-chain and build hardening.

## HTTP client (planned)

Outbound calls for BFF patterns:

```text
http_client_get(url_slice, headers, body_out) → status
```

Connection pool per host, TLS verify with system CA bundle, timeout per request. Never called from Model A reactor thread without blocking risk—use Model B workers.

## Email, SMS, webhooks

Thin adapters behind queue consumers. Handlers enqueue intent; workers call external APIs with retries and exponential backoff. Idempotency keys stored in outbox or dedup table.

## Internationalization

Not on server hot path for JSON APIs. **Planned** `Accept-Language` negotiation for HTML error pages generated by runtime templates.

## Testing libraries

**Shipped:** `./cforge test` integration harness. **Planned:** `cforge-test` crate with assert macros on HTTP responses, snapshot JSON bodies, and fake clock for idle timeout tests.

## Documentation generation

OpenAPI from route metadata annotations in comments or sidecar YAML; `cforge docgen` emits spec for consumers. Keeps handlers as source of truth for paths and methods.

## Summary matrix

| Library area | 0.1 | Planned entry |
|---|---|---|
| Packages | Single binary | Registry + lockfile |
| Config | Env vars | Layered config |
| Auth | None | JWT + middleware |
| Errors | Integer + HTTP | Problem Details |
| Logging | stderr INFO | Structured JSON |
| Metrics | Four counters | Prometheus + traces |

Integration with storage and queues is in [database.html](database.html); deployment wiring in [production.html](production.html).
