# Language reference

Release **0.1** — implemented subset. Syntax outside this list is a compile error.

## Lexical

- Line comments: `// ...`
- Identifiers: `[A-Za-z_][A-Za-z0-9_]*`
- String literals: `"..."` with escapes `\n`, `\t`, `\r`, `\"`, `\\`, `\/`
- Integer literals: decimal `u64` / `i32` range per use site

## Types

| CForge | C (server backend) |
|---|---|
| `i32`, `u32`, `i64`, `u64`, `u16`, `u8` | same-width integers |
| `bool` | `int32_t` (0 / non-zero) |
| `Slice` | `{ uint8_t *ptr; size_t len; }` |
| `Ctx` | opaque service context |
| `*T` | pointer |

`Slice` copies two words (pointer + length). It does not allocate.

## Functions

```forge
fn name(param: Type, ...) -> ReturnType {
    ...
}
```

`main` must exist. It is emitted as `cforge_main` with a C `main` wrapper.

## Statements

| Statement | Example |
|---|---|
| let | `let x: u64 = 0;` |
| return | `return ctx_status(ctx, 404);` |
| if / else | `if x != 0 { ... } else { ... }` |
| while | `while cond { ... }` |
| expression | `db_get_user(ctx, id, &oid, &name, &age);` |

## Expressions

- Literals, identifiers, calls `f(a, b)`
- Field access on `Slice`: `.len`, `.ptr`
- Operators: `+ - * / == != < > <= >=`, unary `&`, `-`, `!`
- Address-of: `&id` for pointer parameters

## Machine-only syntax

When using `./cforge machine`, raw instructions are allowed in function bodies:

```forge
add r0, r1;
mov r2, 42;
syscall;
ret;
```

Registers **r0–r7** map to System V AMD64 argument registers. **`ret`** moves r0 → rax.

The server backend (`./cforge build`) rejects raw instructions — use the [machine backend](machine.html).

## Not in 0.1

- generics, modules, `async fn`, comptime derives
- operator overloading, enums, traits

## Struct types (1.0)

```forge
struct CreateUser {
    name: Slice;
    age: u32;
}
```

Structs lower to C `typedef struct` in generated code. Fields use types from the table above.

Roadmap: [Implementation status](../status.html).
