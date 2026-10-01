# Production deployment, performance, and architecture comparison

Operating CForge services in production requires aligning the **0.1 reference binary** with broader ecosystem goals: graceful lifecycle, observable SLOs, capacity planning, and choosing among three server architectures. This document is the operations-facing capstone of the spec series.

## Reference deployment (0.1)

Typical layout:

```text
systemd user unit → build/cforge-users
  → bind 127.0.0.1:8080 (HTTP), :8443 (TLS)
  → SQLite file on persistent volume
  → PEM cert/key from deploy/ (self-signed for dev)
```

Example unit fields:

- `ExecStart` absolute path to binary
- `WorkingDirectory` repo root for relative DB default
- `Environment=PORT`, `TLS_PORT`, `CFORGE_BIND`, `CFORGE_DB`, TLS paths
- `Restart=on-failure` — clean SIGTERM exit 0 does not restart

`./cforge deploy` writes and enables the user unit, generates certs if missing, waits until `GET /health` returns `ok`.

**Production hardening beyond 0.1:**

- Terminate TLS at reverse proxy (nginx, Caddy) with real CA certs
- Run behind private network; expose only proxy
- Separate DB to managed PostgreSQL per [database.html](database.html)
- Centralize logs and metrics per [libraries.html](libraries.html)

## Graceful shutdown

**Shipped behavior** on `SIGTERM` / `SIGINT`:

```text
eventfd wakes epoll_wait
  → close listen sockets (port reusable)
  → close idle connections
  → mark active connections close-after-response
  → when active_connections == 0, free slabs, finalize SQLite, exit 0
```

Kubernetes: `terminationGracePeriodSeconds` must exceed worst-case in-flight request time plus client slow-read. PreStop hook can sleep briefly after deregistering from load balancer.

## Performance model

### Steady-state cost per request (Model A)

Dominant terms:

1. TLS decrypt (if used) — OpenSSL constant overhead
2. HTTP parse — linear in header + body size, bounded
3. Handler — JSON parse + SQLite bind/step (blocking)
4. Response encode + encrypt + write

No per-request heap allocation; arena bump is O(bytes allocated in handler).

### Capacity variables

| Variable | Effect |
|---|---|
| `CFORGE_MAX_CONNS` | Max parallel clients |
| Handler DB latency | Directly adds to event-loop stall (Model A) |
| Keep-alive | Amortizes TLS handshake; increases slot occupancy |
| `accept_paused` | Signals saturation; clients see SYN backlog delay |

Rough planning: if mean handler time is `T` ms and one thread, sustainable RPS ≈ `1000 / T` for CPU-bound handlers ignoring I/O overlap. I/O overlap improves effective RPS until SQLite or handler blocks.

### Tail latency

Model A suffers **head-of-line blocking** behind slow SQLite or large bodies. Model B isolates blocking to workers at cost of queue depth and memory. See [http-async.html](http-async.html).

### Zero-copy and proxy mode (planned)

Edge proxy terminates TLS and speaks HTTP/1.1 or h2 to backend over loopback—reduces crypto load on CForge process. Static assets served via `sendfile` from proxy, not app binary.

## Three architectures comparison

The ecosystem contemplates three deployment shapes:

### Architecture 1 — Monolithic sync reactor (0.1)

```text
[ Clients ] → [ cforge-users: epoll + TLS + HTTP + SQLite ]
```

| Strength | Weakness |
|---|---|
| Simple deploy, low memory | Single-core CPU ceiling |
| Predictable lifetimes | Blocking DB hurts all clients |
| Easy local dev | Not ideal for high QPS |

Best for: edge APIs on small VMs, development, embedded admin ports.

### Architecture 2 — Monolith + worker pool (planned Model B)

```text
[ Clients ] → [ reactor ] → queue → [ workers + PG pool ]
```

| Strength | Weakness |
|---|---|
| Uses multiple cores | Continuation + queue complexity |
| Hides DB latency | Harder debug, more memory |
| Same binary artifact | Requires careful Slice rules |

Best for: OLTP APIs with 10–50 ms DB calls, moderate concurrency.

### Architecture 3 — Microservices + bus (planned)

```text
[ Clients ] → [ API gateway ]
                 ↓           ↓
           [ cforge-users ]  [ cforge-search ]
                 ↓           ↑
              [ Kafka / outbox relay ]
                 ↓
           [ PostgreSQL ]
```

| Strength | Weakness |
|---|---|
| Independent scale and deploy | Network partitions, distributed tracing required |
| Technology mix per service | Operational overhead |
| Clear blast radius | Eventual consistency |

Best for: teams, multi-tenant SaaS, search/analytics sidecars.

### Decision matrix

| Criterion | Arch 1 | Arch 2 | Arch 3 |
|---|---|---|---|
| Time to first deploy | ★★★ | ★★ | ★ |
| Peak RPS on one host | ★ | ★★★ | ★★ (aggregate) |
| Operational cost | ★★★ | ★★ | ★ |
| Data consistency | Strong local | Strong with one DB | Eventual across services |

Migrate 1 → 2 by enabling worker pool in same binary. Migrate 2 → 3 by extracting read models and async consumers without changing handler source if outbox pattern is used from day one.

## SLO suggestions

Example targets for a users API behind Arch 1 on loopback:

| SLO | Target | Measurement |
|---|---|---|
| Availability | 99.9% | `/health` probe |
| p99 latency | < 50 ms | Prometheus histogram |
| Error rate | < 0.1% 5xx | `db_errors` + 5xx counter |

Saturation alert: `accept_paused == 1` for > 30 s.

## Horizontal scaling

**0.1 SQLite** does not multi-write across instances. Production path:

- Move to PostgreSQL
- Stateless app replicas behind L4/L7 load balancer
- Sticky sessions only if required; prefer stateless JWT

Session affinity not required for REST JSON API.

## Backup and disaster recovery

- SQLite file: snapshot when stopped or use replication to server DB
- PostgreSQL: managed backups, test restore quarterly
- Config and secrets: infra-as-code, not only in unit files

## FFI at production boundary

Services may call Rust/Zig crypto or Go policy engines via C ABI plugins. Require:

- Versioned plugin API
- Timeout on plugin calls in worker threads
- Fail closed on plugin crash (disable route, alert)

Details in [boundary.html](boundary.html).

## Compliance and audit

Log admin mutations with actor id when auth ships. Retain audit outbox immutably. GDPR delete propagates via `DELETE` handler plus cache invalidation and search indexer consumer.

## Upgrade strategy

Rolling restart with multiple Arch 2/3 instances behind health-checked load balancer. Run `cforge migrate` once before rolling app deploy. Blue/green: deploy new binary to alternate unit, swap proxy upstream after health pass.

## What 0.1 proves in production

The users service validates:

- Epoll accept pause under load
- TLS 1.3 alongside HTTP
- Arena-safe JSON + SQLite path
- systemd lifecycle and integration tests

It is not yet a template for multi-region HA without the planned database and queue layers. See [index.html](index.html) for the full spec map; operational detail also appears in the project END-TO-END guide and [implementation status](../status.html).
