# Database, pooling, cache, and message queues

Persistence and asynchronous messaging in the CForge ecosystem are **runtime libraries**, not language builtins. Release 0.1 ships SQLite for the users service; this spec defines the target architecture for connection pools, external databases, in-memory cache, and queue integrations.

## SQLite (shipped)

**Shipped (0.1):** SQLite pool (`CFORGE_DB_POOL`, default 4) of connections with prepared statements. Same schema:

```sql
CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    age INTEGER NOT NULL
);
```

Pragmas at open: `journal_mode=WAL`, `synchronous=NORMAL`. Three prepared statements:

```sql
INSERT INTO users(name, age) VALUES (?1, ?2)
SELECT id, name, age FROM users WHERE id = ?1
DELETE FROM users WHERE id = ?1
```

Parameters are bound; SQL text is never concatenated with user input. `SQLITE_TRANSIENT` on insert copies name bytes out of the request arena. `SELECT` copies TEXT into the arena before `sqlite3_reset`.

**Checkout discipline:** Each `db_*` call checks out a pool connection. After the handler returns, `cforge_db_release` returns it to the pool and resets statements.

**Optional Postgres:** When `CFORGE_PG_DSN` is set and the binary is built with libpq, the same `db_*` API uses PostgreSQL (`users` table, `BIGSERIAL` id). Metrics report `db_backend postgres` or `sqlite`.

**Optional Redis:** When `CFORGE_REDIS_URL` is set, the runtime connects and `DEL user:{id}` on insert/delete (cache invalidation). Metrics report `redis_backend connected` or `disabled`.

Integer API:

| Return | Meaning |
|---|---|
| `0` | Success |
| `1` | Row missing (`GET`/`DELETE`) |
| `-1` | SQLite or bind error |

Handlers map `-1` to `503` and increment `db_errors` in metrics.

`busy_timeout` is 1000 ms for contention with external tools opening the same file.

## Connection pool (PostgreSQL / MySQL target)

Target shape for additional pool features on Postgres:

```text
Pool {
  min, max connections
  acquire(timeout) → PooledConn
  release(conn) → return to idle or destroy if broken
}
```

Rules:

- **Never** hold a pooled connection across `async` yield unless Model B explicitly pins conn to continuation
- One statement cache per connection; reset on release
- Health check query on idle timeout
- Metrics: `pool_wait_ms`, `pool_active`, `pool_idle`, `pool_create_failures`

Handlers keep the integer error policy; `db_get_user` becomes a thin wrapper over `SELECT` with dialect-specific types. Migrations run from toolchain (`cforge migrate`) applying versioned SQL files—not from handlers.

## PostgreSQL wire client

**Shipped:** libpq via `CFORGE_PG_DSN` on the reactor thread (same sync model as 0.1).

Design options for future scaling:

| Approach | Pros | Cons |
|---|---|---|
| libpq | Battle-tested, TLS built-in | C dependency, blocking API |
| Custom frontend/backend protocol | Full control | Large implementation |
| Embedded via CGO/Rust shim | Reuse ecosystem | FFI boundary discipline |

CForge favors **libpq in worker threads** under Model B, with results marshaled into arena `Slice` on the reactor thread before handler return in hybrid mode.

## Transaction boundaries

**0.1:** Each statement auto-commits. **Planned:** `db_tx_begin(ctx)` / `db_tx_commit` / `db_tx_rollback` storing tx state in `Ctx` extensions, forbidden across keep-alive requests unless explicitly bound to a session id.

Isolation default: `READ COMMITTED` for OLTP handlers; serializable for financial slices via opt-in.

## In-memory and distributed cache

Layers:

1. **Request arena** — not a cache; discard every request
2. **Process LRU** — fixed-size slab for hot keys (user by id, JWKS)
3. **Redis** — shared cache and rate-limit counters
4. **CDN** — HTTP cache headers for static content

Cache API sketch:

```text
cache_get(ctx, key_slice, value_out_slice) → hit/miss
cache_set(ctx, key, value, ttl_sec) → status
```

Serialization format for values is caller-chosen (JSON blob, msgpack). **Stampede protection:** single-flight lock per key in process LRU; Redis `SET NX` for cross-node.

Invalidation: explicit `cache_del` on write paths (`POST`/`DELETE` users); **shipped** Redis `DEL user:{id}` when `CFORGE_REDIS_URL` is set; **planned** pub/sub channel `cache:invalidate` for multi-instance deployments.

## Message queues (planned)

Use cases: outbox pattern for email, audit log shipping, search index updates, decoupled workers.

| System | Pattern | CForge integration |
|---|---|---|
| Kafka | Log-based, consumer groups | Background thread + batch publish |
| NATS | Lightweight pub/sub | Fire-and-forget sidecar goroutine equivalent in C |
| RabbitMQ | AMQP routing | Worker pool consumers |
| Redis Streams | Simple queue | Same client as cache |

**Outbox table** (relational):

```sql
CREATE TABLE outbox (
  id BIGSERIAL PRIMARY KEY,
  payload BYTEA NOT NULL,
  created_at TIMESTAMPTZ NOT NULL,
  published_at TIMESTAMPTZ
);
```

Handler inserts user row and outbox row in one transaction; relay process publishes and marks `published_at`. This preserves **at-least-once** delivery without dual-write races.

Handlers should not block on publish; Model B workers drain outbox. Metrics: `outbox_lag_seconds`, `publish_failures`.

## Read replicas and routing

**Planned:** `db_read` vs `db_write` pool handles; sticky primary for session consistency. Handlers remain unaware if the runtime routes `SELECT` to replica based on `Ctx` read-only flag set by middleware.

## Backup and migrations

Operational requirements:

- SQLite: file snapshot when process stopped or `VACUUM INTO`
- PostgreSQL: WAL archiving, PITR
- Migrations versioned in `migrations/` applied by `cforge migrate` with advisory lock

## Security

- Least-privilege DB roles per service binary
- No dynamic SQL from handler strings
- Encrypt connections (TLS to Postgres)
- Secrets from environment or vault sidecar, not committed files

## 0.1 vs target summary

| Capability | 0.1 | Target |
|---|---|---|
| Engine | SQLite file or Postgres (DSN) | Postgres primary |
| Pool | SQLite pool + libpq conn | Sized pool + health |
| Cache | Redis invalidation (optional) | LRU + Redis read-through |
| Queues | None | Outbox + Kafka/NATS |
| Migrations | Embedded `CREATE TABLE` | Toolchain migrator |

See [http-async.html](http-async.html) for handler error mapping and [production.html](production.html) for SLO impact of pool wait time.
