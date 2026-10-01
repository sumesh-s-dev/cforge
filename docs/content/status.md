# Implementation status

**Specification:** nine-chapter **design document** on the site (boundary through production) — describes target architecture, not a promise that every item is implemented.

**This repository (release 0.1 demo)** ships a runnable **users-service vertical slice**: CForge-syntax handlers → generated C → epoll HTTP/TLS runtime.

Public site: **https://sumesh-s-dev.github.io/cforge/**

## Shipped in this repo

| Area | Status |
|---|---|
| Backend **design docs** (9 chapters) | Written |
| Lexer / parser (small subset) | Done |
| **struct** types (must appear before use in source) | Done |
| C server backend + runtime | Done |
| Machine ELF backend (x86-64 demo) | Done |
| epoll HTTP/1.1 + TLS 1.3 + WebSocket `/ws` | Done |
| SQLite pool + optional Postgres + optional Redis | Done |
| Structured **logging** (`cforge_logf`) | Done |
| **`cforge.lock`** (`./cforge package`) | Done |
| Toolchain + `./cforge compiler-test` | Done |
| GitHub CI + Pages docs | Done |
| **Docker Compose** (Postgres + Redis + app) | Done (`./cforge deploy-docker`) |
| **End-to-end** (`./cforge e2e`, `./cforge everything`) | Done |

## Roadmap (spec describes; code follows)

| Area | Code today |
|---|---|
| HTTP/2, HTTP/3 | Planned |
| Kafka / NATS | Planned |
| JWT / OAuth | Planned |
| Full **`@safe`** checking | Planned (`@…` attributes warn and are ignored) |
| Package registry + semver | Lockfile only |
| Native IR server (replace C path) | `cforge_mach` demo only |

## Environment

| Variable | Purpose |
|---|---|
| `CFORGE_DB_POOL` | SQLite pool size (1–8, default 4) |
| `CFORGE_PG_DSN` | Optional Postgres via libpq |
| `CFORGE_REDIS_URL` | Optional Redis (`redis://host:6379`); `./cforge deploy` can start bundled Redis |

## Verification

```bash
./cforge compiler-test
./cforge test
./cforge e2e
./cforge everything
```

## Honest pitch

CForge 0.1 is a **backend DSL + runtime demo**, not a general systems language yet. The spec is a **north star**; the binary is the users microservice path documented in [END-TO-END.md](https://github.com/sumesh-s-dev/cforge/blob/main/END-TO-END.md).
