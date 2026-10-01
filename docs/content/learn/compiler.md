# Compiler

The **cforge** driver is a Python toolchain that lexes and parses `.cforge`, then lowers to one of two backends.

## Server backend (default)

```text
app/users.cforge
    → build/users.gen.c
    → cc -std=c11 -O2 -Wall -Wextra -Werror
    → build/cforge-users
         + runtime/server.c json.c db.c
         + libsqlite3 libssl libcrypto
```

Entry point configured in `cforge.toml`:

```toml
[package]
name = "users"
entry = "app/users.cforge"
```

### Commands

| Command | Action |
|---|---|
| `./cforge build` | Compile + link server binary |
| `./cforge check` | Parse + generate C (no link) |
| `./cforge run` | Build and exec binary |

Runtime symbols (`app_get`, `db_*`, …) are declared in `runtime/cforge_rt.h` and implemented in C.

## Machine backend

```text
mach/add.cforge
    → cforge_mach.py
    → x86-64 opcodes + ELF64 header
    → executable (no gcc)
```

| Command | Action |
|---|---|
| `./cforge machine file.cforge [-o out]` | Emit ELF |
| `./cforge machine-test` | Run add/expr fixtures |

Limits: integer functions, six parameters, no `let`/`if` in machine slice yet (see status page).

## Future native compiler

The long-term architecture is **CForge IR → register allocation → ELF** for all programs, with the C path remaining a bootstrap backend. Release 0.1 ships both pipelines; only the machine path is gcc-free.

## Diagnostics

Errors are `compile error: line: message` on stderr with non-zero exit.
