# Implementation status (/docs/status)



**Specification:** nine-chapter **design document** on the site — target architecture, not a promise that every item is implemented.

**This repository (release 0.1 demo)** ships a **users-service vertical slice**: CForge-syntax handlers → typechecked → generated C → epoll HTTP/TLS runtime.

Public site: **[https://sumesh-s-dev.github.io/cforge/](https://sumesh-s-dev.github.io/cforge/)**

## Shipped in this repo [#shipped-in-this-repo]

| Area                                                               | Status  |
| ------------------------------------------------------------------ | ------- |
| Backend **design docs** (9 chapters)                               | Written |
| Lexer / parser + **struct** types                                  | Done    |
| **`app/runtime.cforge`** + **`extern fn`** (no compiler allowlist) | Done    |
| **Type checker** (arity, handlers, `let` env)                      | Done    |
| **`./cforge compiler-test`** + **HTTP fuzz** (in `./cforge test`)  | Done    |
| C server backend + runtime                                         | Done    |
| Machine ELF backend (x86-64 demo)                                  | Done    |
| HTTP/1.1 + TLS 1.3 + WebSocket `/ws`                               | Done    |
| SQLite pool + optional Postgres + optional Redis                   | Done    |
| Toolchain + `./cforge everything`                                  | Done    |
| GitHub CI + Pages                                                  | Done    |
| Docker Compose stack                                               | Done    |

## Roadmap [#roadmap]

HTTP/2–3, Kafka/NATS, JWT/OAuth, full `@safe`, package registry, native IR **server** — see [status table](status.html) in prior commits / spec index.

## Verification [#verification]

```bash
./cforge compiler-test
./cforge test          # + fuzz-http
./cforge everything
./cforge deploy
```

## Honest pitch [#honest-pitch]

CForge 0.1 is a **backend DSL + runtime demo** with a **strong architecture skeleton** and a **prototype compiler**. The spec is a **north star**; the next technical depth is **semantics** (ownership/lifetimes, IR), not more optional integrations before that.

## What we are not claiming [#what-we-are-not-claiming]

* A production-ready **general-purpose** systems language (vs C/Rust/Zig)
* **Memory safety** enforced by the compiler today
* That the **machine backend** is more than a teaching/demo second target
