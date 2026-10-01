# CForge

<p class="hero">Small backend DSL (compiles to C) plus an epoll HTTP/TLS/SQLite users service. Full ecosystem design is documented; most of it is roadmap.</p>

## Documentation

| | |
|---|---|
| **Book** | [Introduction](learn/introduction.html) |
| **Design spec** | [Specification index](spec/index.html) — target architecture |
| **Status** | [Release 0.1 demo](status.html) |

Handlers live in `.cforge` files. HTTP, JSON, TLS, and storage live in the **C runtime**, not in the language grammar.

## Two compile paths

| Path | Command | Output |
|---|---|---|
| **Server** | `./cforge build` | Native binary via generated C11 + runtime |
| **Machine (demo)** | `./cforge machine prog.cforge` | x86-64 ELF without gcc |

## Quick links

- [Introduction](learn/introduction.html) — philosophy and boundaries
- [Installation](learn/install.html) — clone, build, test
- [Language reference](learn/language.html) — implemented subset
- [Implementation status](status.html) — shipped vs roadmap

## Live demo

After [installation](learn/install.html):

```text
./cforge deploy
curl http://127.0.0.1:8080/health
```

Repository: [github.com/sumesh-s-dev/cforge](https://github.com/sumesh-s-dev/cforge)
