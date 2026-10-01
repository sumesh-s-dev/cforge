# Implementation status

**Release 0.1** defines “complete” for this repository: everything listed below as **Done** is implemented, tested, documented, published to GitHub, and deployed locally via `./cforge deploy`.

Public docs: **https://sumesh-s-dev.github.io/cforge/**

## Release 0.1 — Done

| Area | Status |
|---|---|
| Lexer / parser (server subset) | Done |
| C code generator + link runtime | Done |
| Machine ELF backend (integer, r0–r7, syscall) | Done |
| epoll HTTP/1.1 server | Done |
| TLS 1.3 (OpenSSL) | Done |
| SQLite users API | Done |
| Request arena + backpressure | Done |
| `./cforge` toolchain (build, check, test, deploy, all, machine) | Done |
| GitHub CI | Done |
| GitHub Pages documentation site | Done |
| END-TO-END + book-style docs | Done |
| Example service on localhost | Done |

## Language — partial (post-0.1)

| Feature | Status |
|---|---|
| struct in .cforge | Planned |
| Modules / packages | Planned |
| Generics / comptime | Planned |
| async fn lowering | Planned |
| Full native compiler for server | Planned |

## Backend ecosystem — partial

| Feature | Status |
|---|---|
| PostgreSQL wire client | Not started |
| HTTP/2, HTTP/3, WebSockets | Not started |
| Connection pool (multi conn) | Not started |
| Redis / Kafka clients | Not started |
| Package registry | Not started |

## Compiler architecture roadmap

```text
Today:   .cforge → C → gcc     (server)
         .cforge → MIR → ELF   (machine toys)

Target:  .cforge → CForge IR → ELF + thin runtime (all programs)
```

## Verification

```bash
./cforge test          # must pass
./cforge machine-test  # included in test
curl http://127.0.0.1:8080/health
```

## Definition of “100%” for CForge the product

| Scope | 0.1 |
|---|---|
| Documented language subset | **100%** |
| Documented server + runtime | **100%** |
| Documented machine backend (stated limits) | **100%** |
| Entire original backend design doc | **~35%** |
| Rust/Clang-class language + ecosystem | **Not claimed** |

CForge 0.1 is **complete for its documented scope**. Expanding scope increases the roadmap, not a bug in 0.1.
