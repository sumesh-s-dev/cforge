# CForge (/docs)



A low-level <strong>backend DSL</strong> and systems-language experiment: small typed language → C11 (plus experimental x86-64 ELF), with a bounded-memory HTTP/TLS/SQLite runtime on Linux. Not a general-purpose Rust/C competitor in 0.1.

## Documentation [#documentation]

|                 |                                                              |
| --------------- | ------------------------------------------------------------ |
| **Book**        | [Introduction](learn/introduction.html)                      |
| **Design spec** | [Specification index](spec/index.html) — target architecture |
| **Status**      | [Release 0.1 demo](status.html)                              |

Handlers live in `.cforge` files. HTTP, JSON, TLS, and storage live in the **C runtime**, not in the language grammar.

## Two compile paths [#two-compile-paths]

| Path               | Command                        | Output                                    |
| ------------------ | ------------------------------ | ----------------------------------------- |
| **Server**         | `./cforge build`               | Native binary via generated C11 + runtime |
| **Machine (demo)** | `./cforge machine prog.cforge` | x86-64 ELF without gcc                    |

## Quick links [#quick-links]

* [Introduction](learn/introduction.html) — philosophy and boundaries
* [Installation](learn/install.html) — clone, build, test
* [Language reference](learn/language.html) — implemented subset
* [Implementation status](status.html) — shipped vs roadmap

## Live demo [#live-demo]

After [installation](learn/install.html):

```text
./cforge deploy
curl http://127.0.0.1:8080/health
```

Repository: [github.com/sumesh-s-dev/cforge](https://github.com/sumesh-s-dev/cforge)
