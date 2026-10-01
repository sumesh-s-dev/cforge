# Implementation status

**Specification:** [100% complete on the documentation site](../spec/index.html) — full backend ecosystem design (boundary through production).

**Release 1.0 (this repository)** ships the runnable vertical slice plus libraries specified as *Shipped* below.

Public site: **https://sumesh-s-dev.github.io/cforge/**

## Shipped in release 1.0

| Area | Status |
|---|---|
| Full backend **specification** (9 chapters) | **100%** documented |
| Lexer / parser + **struct** types | Done |
| C server backend + runtime | Done |
| Machine ELF backend (x86-64) | Done |
| epoll HTTP/1.1 + TLS 1.3 | Done |
| SQLite users API + **connection pool** | Done |
| Structured **logging** (`cforge_logf`) | Done |
| Request arena + backpressure | Done |
| **`cforge.lock`** (`./cforge package`) | Done |
| Toolchain (build, check, test, deploy, all, machine) | Done |
| GitHub CI + Pages docs | Done |
| **Docker Compose deploy** (Postgres + app) | Done (`./cforge deploy-docker`) |
| **End-to-end deploy** (test + systemd + smoke) | Done (`./cforge e2e`) |

## Planned (spec describes; code follows)

| Area | Spec | Code |
|---|---|---|
| PostgreSQL wire / libpq | Documented | **Shipped** (`CFORGE_PG_DSN`) |
| HTTP/2, HTTP/3, WebSockets | Documented | Planned |
| Native IR compiler for server | Documented | In progress (`cforge_mach`) |
| Redis / Kafka / NATS clients | Documented | Planned |
| `@checked` / `@safe` modes | Documented | Planned |
| Package registry + semver resolver | Documented | Lockfile only |

## Environment

| Variable | New in 1.0 |
|---|---|
| `CFORGE_DB_POOL` | SQLite pool size (1–8, default 4) |

## Verification

```bash
./cforge package
./cforge test
curl http://127.0.0.1:8080/health
```

## Definition of complete

| Scope | Coverage |
|---|---|
| Original backend architecture **document** | **100%** on [spec index](../spec/index.html) |
| Same architecture **implemented in code** | **Core path 100%**; advanced subsystems per table above |

CForge 1.0 is **spec-complete** and **production-demo-complete** for the HTTP/SQLite/TLS stack. Remaining spec items are explicit roadmap entries, not missing documentation.
