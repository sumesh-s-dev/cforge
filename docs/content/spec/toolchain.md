# Toolchain, build, test, security, compiler and IR roadmap

The CForge **toolchain** is the `cforge` executable plus `cforge_mach.py` for native ELF. It orchestrates compile, link, integration test, deploy, and (eventually) package fetch and migration. This document covers commands, CI expectations, security posture, and the multi-year compiler roadmap.

## Commands (shipped)

| Command | Behavior |
|---|---|
| `./cforge build` | Lex/parse entry → `build/*.gen.c` → `cc` runtime objects → binary |
| `./cforge run` | Build then exec binary in place |
| `./cforge test` | Build, ephemeral ports 18081/18443, 14+ HTTP/TLS checks, SIGTERM, machine tests |
| `./cforge deploy` | Build, write systemd user unit, enable/start, wait for `/health` |
| `./cforge status` | GET `/health` and `/metrics` on default port |
| `./cforge machine` | Emit x86-64 ELF via `cforge_mach.py` |
| `./cforge machine-test` | Run native add/expr samples |

Link line (conceptually): C11, `-O2 -g -Wall -Wextra -Werror`, `libsqlite3`, `libssl`, `libcrypto`, `pthread`.

`cforge.toml` selects package name and entry path only in 0.1.

## Build graph

```text
app/users.cforge
    → lexer / parser (cforge)
    → build/users.gen.c
    → cc users.gen.c + runtime/*.c
    → build/cforge-users

mach/add.cforge
    → cforge_mach.py
    → build/mach-add (ELF)
```

**Planned:** `cforge build --release` strips symbols; `cforge build --target aarch64-unknown-linux-gnu` cross-compile via LLVM backend.

## Testing strategy

Layers:

1. **Machine tests** — deterministic exit codes for arithmetic programs
2. **Integration tests** — real TCP, TLS 1.3, pipelining, split writes, JSON edge cases
3. **Unit tests (planned)** — C tests for HTTP parser vectors, JSON fuzz inputs
4. **Property tests (planned)** — generate random valid HTTP requests within limits

CI (GitHub Actions): Ubuntu, install gcc/SQLite/OpenSSL, run `./cforge test`. No committed TLS certs or production database.

Local parity: tests use separate DB under `build/` to avoid touching `data/users.db`.

## Documentation build

`python3 docs/build_docs.py` walks `docs/content/**/*.md` and emits static HTML. Spec pages live under `docs/content/spec/` and publish alongside learn/reference when regenerated.

## Security posture

### Supply chain

- Pin compiler flags `-Werror` in CI
- Verify dependency hashes when package registry exists
- Reproducible builds: document compiler version in release notes

### Runtime attack surface

- Bind localhost by default; document risk of `0.0.0.0`
- TLS 1.3 only; disable weak ciphers and renegotiation
- Request size limits enforced before handler
- No `eval`, no runtime `dlopen` in 0.1
- SQLite parameterized queries only

### Secrets

- PEM keys read from paths in environment, not argv
- **Planned:** secret files mode `0400`, refuse world-readable key paths
- Never log bearer tokens or passwords

### Dependency CVE process

Track OpenSSL and SQLite advisories; rebuild and redeploy on critical fixes. SBOM export **planned** as `cforge sbom`.

## Compiler pipeline today

```text
Source (.cforge)
  → tokens
  → AST (functions, statements, calls)
  → C codegen (types, static functions, cforge_main)
  → host C compiler
```

Builtin calls are validated against a fixed list (`app_listen`, `db_*`, `ctx_*`, etc.). Unknown identifiers are compile errors.

## IR roadmap (planned phases)

### Phase 1 — Typed AST + lint

- Name resolution and type checking beyond syntax
- Dead code elimination on unreachable functions
- Route table validation at compile time (duplicate paths)

### Phase 2 — SSA-like IR

```text
.cforge → AST → IR (instructions, basic blocks, phi for merges)
         → optimizations (const fold, inline small helpers)
         → C backend OR machine backend
```

Benefits: single place for `async fn` lowering, shared inlining, debug symbols mapping IR to source lines.

### Phase 3 — Machine backend parity

- Lower IR to x86-64 and aarch64
- ELF linking with minimal libc or none for freestanding
- Same calling convention as current `r0`–`r7` mapping where applicable

### Phase 4 — Alternative backends

- WASM for edge workers
- LLVM IR for vendor optimizers

IR is **not** shipped in 0.1; `cforge_mach.py` remains a separate ad hoc lowering for teaching.

## FFI and codegen safety

Generated C must:

- Include `stdint.h` and `cforge_rt.h`
- Not emit VLAs on handler path
- Map `bool` to `int32_t` consistently with runtime

**Planned:** sanitizers build flag `cforge build --asan` for contributors.

## Formatter and linter (planned)

`cforge fmt` — canonical indentation and brace style
`cforge lint` — warn on unreachable code, unused `let`, suspicious `Slice` escapes

## Versioning

Semantic versioning for language breaking changes vs runtime ABI. Runtime functions gain new symbols without breaking existing binaries when using stable `Ctx` size or version negotiation at `app_listen`.

## Release artifacts

- Static binary tarball per arch
- systemd unit example under `deploy/`
- **Planned:** OCI images with distroless base, read-only rootfs, non-root user

## Relation to spec siblings

- [boundary.html](boundary.html) — what the compiler may call
- [production.html](production.html) — how built artifacts run at scale
- [libraries.html](libraries.html) — future package manifests

## Machine backend reference

`cforge_mach.py` parses register operations and small expressions, emits ELF with Linux syscall exit. It does not share the HTTP runtime. Roadmap merges front-end with server compiler via IR; until then, two entry commands remain intentional.

## Contributor workflow

```text
edit .cforge or runtime/*.c
./cforge test
python3 docs/build_docs.py   # if docs changed
```

Pre-commit hooks **planned** for fmt/lint/test on changed paths only.
