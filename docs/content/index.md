# CForge

<p class="hero">Low-level systems language with a complete backend specification and a runnable HTTP/TLS/SQLite stack.</p>

## Documentation

| | |
|---|---|
| **Book** | [Introduction](learn/introduction.html) |
| **Full backend spec** | [Specification index](spec/index.html) — 100% architecture document |
| **Status** | [Release 1.0](status.html) |

CForge sits close to the machine. Handlers are written in `.cforge` files. Networking, HTTP, JSON, TLS, and SQLite live in a **C runtime library**, not in the language grammar.

## Two compile paths

| Path | Command | Output |
|---|---|---|
| **Server** | `./cforge build` | Native binary via generated C11 + runtime |
| **Machine** | `./cforge machine prog.cforge` | x86-64 ELF without gcc |

## Quick links

- [Introduction](learn/introduction.html) — philosophy and boundaries
- [Installation](learn/install.html) — clone, build, test
- [First program](learn/first-program.html) — hello service and native add
- [Implementation status](status.html) — what is complete in release 0.1

## Live demo

After [installation](learn/install.html):

```text
./cforge deploy
curl http://127.0.0.1:8080/health
```

Repository: [github.com/sumesh-s-dev/cforge](https://github.com/sumesh-s-dev/cforge)
