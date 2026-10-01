# Machine backend

Direct **x86-64** code generation for freestanding Linux ELF executables.

## Register file

| CForge | Hardware (SysV args) |
|---|---|
| r0 | rdi |
| r1 | rsi |
| r2 | rdx |
| r3 | rcx |
| r4 | r8 |
| r5 | r9 |
| r6 | r10 |
| r7 | r11 |

Return value: **r0** copied to **rax** before `ret`.

## Instructions (0.1)

| Mnemonic | Effect |
|---|---|
| `mov rD, rS` | move |
| `mov rD, imm` | 32-bit immediate |
| `add rD, rS` | add |
| `sub rD, rS` | subtract |
| `syscall` | `syscall` instruction |
| `ret` | exit function |

## Startup

Emitted `_start` calls `main`, moves result to rdi, `syscall` exit (60).

## Example

```forge
fn main() -> u64 {
    return add(40, 2);
}

fn add(a: u64, b: u64) -> u64 {
    add r0, r1;
    ret;
}
```

```bash
./cforge machine mach/expr.cforge -o /tmp/expr && /tmp/expr; echo $?
# 42
```

## Relation to server backend

The HTTP server does **not** use the machine backend yet. It uses C codegen + runtime. Convergence goal: link runtime as native object code compiled from CForge IR.

## Platform

Linux x86-64 only in 0.1. ARM64 planned as a second Machine IR target.
