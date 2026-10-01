# CForge backend ecosystem specification

This document set describes the **full CForge backend ecosystem design**: how application code, the language compiler, the C runtime, optional native machine code, and Linux facilities fit together to build network services. It is normative for **intent** and descriptive for **release 0.1**, which implements a vertical slice (HTTP/1.1, WebSocket `/ws`, SQLite pool, optional Postgres/Redis, TLS 1.3, single-threaded epoll) documented in the repository’s end-to-end guide.

The specification is organized by concern, not by source file. Cross-cutting themes—explicit memory, integer error codes, library-not-language features, and predictable syscall budgets—appear in every layer.

## Architectural stance

CForge targets **low-latency, allocation-bounded request paths** on Linux. The language expresses control flow and calls into a **stable runtime ABI** (`Slice`, `Ctx`, route registration, database helpers). The runtime owns syscalls, protocol state machines, buffer pools, and cryptographic primitives. There is no mandatory garbage collector and no hidden thread pool on the hot path in the reference server architecture.

Three compile-time backends coexist in the long-term design:

| Backend | Role | Typical artifact |
|---|---|---|
| **C server** | Fast iteration, full runtime link | `build/cforge-users` via generated C11 |
| **Native machine** | Syscalls, tight loops, teaching | x86-64 ELF from `cforge_mach.py` |
| **Future IR** | Shared optimizations, cross-target | SSA-like IR lowering to C or machine |

Release 0.1 ships the C server path and a partial machine path. The IR backend is roadmap only; see [toolchain.html](toolchain.html).

## Layer diagram

```text
┌─────────────────────────────────────────────────────────────┐
│  Application (.cforge) — routes, validation, orchestration   │
└───────────────────────────────┬─────────────────────────────┘
                                │ CForgeFn(Ctx*), integer codes
┌───────────────────────────────▼─────────────────────────────┐
│  Runtime API (cforge_rt.h) — HTTP helpers, JSON, DB, listen  │
└───────────────────────────────┬─────────────────────────────┘
                                │ internal.h: Arena, limits, Ctx
┌───────────────────────────────▼─────────────────────────────┐
│  Runtime implementation (server.c, json.c, db.c, …)          │
│  HTTP/1.1, router, TLS glue, SQLite, metrics, logging       │
└───────────────────────────────┬─────────────────────────────┘
                                │ epoll, sockets, OpenSSL, pthread*
┌───────────────────────────────▼─────────────────────────────┐
│  Linux kernel — TCP/UDP, timers, files, optional io_uring     │
└───────────────────────────────────────────────────────────────┘

Optional sidecars: PostgreSQL (`CFORGE_PG_DSN`), Redis invalidation (`CFORGE_REDIS_URL`), and future message bus consumers — Kafka/NATS remain design-only.
```

Handlers never see file descriptors. The runtime passes **views** (`Slice`) into fixed per-connection storage. That boundary is the primary safety and performance contract; see [boundary.html](boundary.html).

## Request lifecycle (reference server)

A single client request on the 0.1 server traverses:

```text
TCP accept (epoll) → optional TLS 1.3 → HTTP/1.1 parse → route match
  → handler (.cforge) → JSON/SQLite → response buffer → write/SSL_write
  → arena reset + DB statement release
```

Future designs add HTTP/2 streams, async I/O variants, and worker threads without changing the handler signature; work is scheduled before `CForgeFn` is invoked. Details live in [http-async.html](http-async.html) and [networking.html](networking.html).

## Specification map

| Document | Topics |
|---|---|
| [boundary.html](boundary.html) | Language vs runtime, `Slice`/`Ctx`, error policy, FFI to C and native |
| [networking.html](networking.html) | TCP/UDP, epoll, backpressure, TLS transport, DNS hooks |
| [http-async.html](http-async.html) | HTTP versions, router, JSON/serialization, async model A vs B |
| [memory.html](memory.html) | Arenas, slabs, zero-copy rules, concurrency models |
| [database.html](database.html) | SQLite today, pool design, cache, message queues |
| [libraries.html](libraries.html) | Auth, config, logging, observability, packages |
| [toolchain.html](toolchain.html) | Build/test, security posture, compiler and IR roadmap |
| [production.html](production.html) | Deployment, SLO model, three architectures, operations |

## Design invariants

These rules apply across all planned backends unless a document explicitly carves an exception:

1. **No allocation on the steady-state handler path** in the reference epoll server; bump arenas and pre-sized buffers only.
2. **Integer return codes** instead of exceptions; `0` success, positive domain codes, `-1` for hard failures.
3. **Library boundaries** — protocols and storage are runtime functions, not language keywords.
4. **Bounded resources** — max connections, header/body sizes, route count, idle timeouts; overload degrades by pausing accept, not by unbounded fds.
5. **Explicit shutdown** — signal → eventfd → drain in-flight → release slabs and DB handles.

## Relationship to other docs

The [learn](../learn/introduction.html) track teaches CForge from a user perspective. [Reference](../reference/runtime-api.html) lists the stable runtime surface. [Implementation status](../status.html) marks what is shipped in 0.1 versus planned. This **spec** series is the systems design reference for contributors extending the runtime, adding protocols, or choosing between sync epoll and future async models.

## Versioning

Specification sections label **Shipped (0.1)** when behavior matches the users service binary, and **Planned** when describing ecosystem components (HTTP/2, Kafka, JWT, multi-thread pool) not yet in the tree. Postgres, Redis, and WebSocket are shipped when enabled via environment or built-in `/ws`; see [status.html](../status.html).
