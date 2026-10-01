# Introduction

CForge is a systems programming language prototype. The goal is **machine-level control** without a mandatory garbage collector, hidden allocations, or heavyweight async runtime.

## Design principles

- **No hidden work** — allocation, syscalls, and buffer lifetimes are visible.
- **Libraries, not language features** — HTTP, JSON, TLS, and SQL are runtime code you link, not syntax built into the compiler.
- **Two backends** — rapid iteration through C codegen; native ELF for registers and syscalls on x86-64.
- **Explicit memory** — connection buffers, request arenas, and `Slice` views; reset arenas per request instead of GC.

## What CForge is not

CForge is **not** a memory-safe language like Rust. It does **not** replace PostgreSQL, Kubernetes, or a full package ecosystem in release 0.1. See [Implementation status](../status.html) for the exact matrix.

## Architecture

```text
Application (.cforge)
        ↓
Runtime API (Slice, Ctx, app_listen, db_*)
        ↓
C runtime (epoll, HTTP, SQLite, OpenSSL)
        ↓
Linux kernel
```

The [machine backend](machine.html) bypasses C codegen for small programs and emits ELF directly.

## Comparison

| | CForge | C | Rust |
|---|---|---|---|
| Control | High | High | High |
| Hidden runtime | None required | None | Minimal |
| Memory safety | Programmer + optional modes | Programmer | Compiler |
| Backend in 0.1 | C + partial native | Native | Native |

## Next steps

[Install CForge](install.html) and write your [first program](first-program.html).
