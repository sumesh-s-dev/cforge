# Language and runtime boundary (/docs/spec/boundary)



The CForge backend ecosystem depends on a **hard boundary** between what the `.cforge` compiler understands and what the linked runtime provides. This separation keeps the language small, makes the runtime replaceable, and allows the same handler source to target generated C or (eventually) native code with identical ABI symbols.

## What the language owns [#what-the-language-owns]

**Shipped (0.1):** The `cforge` driver lexes and parses a statement-oriented subset: functions, `let`, `if`/`while`, `return`, and calls. Types map directly to C11 scalars and two runtime-visible structs:

| CForge type                                     | ABI shape                                   |
| ----------------------------------------------- | ------------------------------------------- |
| `i32`, `u32`, `i64`, `u64`, `u16`, `u8`, `bool` | Fixed-width integers / `int32_t` for `bool` |
| `Slice`                                         | `{ uint8_t *ptr; size_t len; }`             |
| `*Ctx`                                          | Opaque request context                      |
| `*u64`, `*u32`, `*Slice`                        | Out-parameters and mutable views            |

`Slice` is a **non-owning view**. Copying a `Slice` copies pointer and length only. String literals become static `Slice` values pointing at read-only bytes. Handlers must not retain `Slice` pointers past the request unless documentation guarantees a longer lifetime (database helpers copy into the request arena for this reason).

The entry `main` lowers to `cforge_main`; the generated translation unit adds a C `main` that registers routes and calls `app_listen`. Application functions are `static` in the generated file and passed as `CForgeFn` pointers.

**Planned:** `struct` declarations at module scope, generics for containers, `async fn` with state-machine lowering, and comptime validation of route tables. None of these change the rule that **syscalls and protocol parsing stay out of the language**.

## What the runtime owns [#what-the-runtime-owns]

Everything that touches the outside world:

* Socket creation, `bind`, `listen`, `accept4`, non-blocking mode, `TCP_NODELAY`, keep-alive
* `epoll_wait` loop, connection slot table, idle scanning, accept pause under load
* HTTP request parsing, response framing, status codes, `Allow` headers
* TLS 1.3 handshake and record I/O via OpenSSL
* JSON parsers and response encoders scoped to known schemas
* SQLite connection, prepared statements, WAL pragmas, busy timeout
* Environment-based configuration (`PORT`, `CFORGE_DB`, TLS paths, limits)
* Process signals, graceful shutdown, stderr logging

Application code chooses **which routes exist** and **which status codes and DB calls** to make. It does not choose buffer sizes, epoll user-data encoding, or SSL context flags.

## The `Ctx` contract [#the-ctx-contract]

`Ctx` is stack-allocated per request inside the runtime. Handlers receive `*Ctx` only. Fields visible through API functions include:

* HTTP method and path as `Slice` (path without query string for routing)
* Request body as `Slice` (bounded by `CFORGE_MAX_BODY`)
* Output buffer cursor managed by `ctx_text`, `ctx_json_*`, `ctx_status`
* Up to four captured path parameters (`:id` style), parsed via `ctx_param_u64`

Handlers must not store `Ctx` pointers beyond the synchronous return of `CForgeFn`. **Planned** async models may extend `Ctx` with a generation token or arena id so resumed tasks cannot write into recycled slots.

## Error and control-flow policy [#error-and-control-flow-policy]

There is no exception unwinding. Functions return `int32_t`:

| Pattern | Meaning                                        |
| ------- | ---------------------------------------------- |
| `0`     | Success                                        |
| `1`     | Expected domain miss (e.g. row not found)      |
| `-1`    | Parse failure, I/O error, constraint violation |

HTTP mapping is application responsibility: `404` for missing users, `400` for bad JSON, `503` when `db_*` returns `-1`. The runtime may emit its own `400`/`413`/`431` before a handler runs when HTTP is malformed.

This policy keeps generated C simple and makes FFI predictable: every exported runtime function uses the same width and sign convention.

## Route registration ABI [#route-registration-abi]

Before `app_listen`, the generated `cforge_main` calls:

```text
app_get(path_slice, handler_fn);
app_post(path_slice, handler_fn);
app_delete(path_slice, handler_fn);
```

Paths are copied at registration time (malloc at startup only). Lookup at request time is linear scan with exact match or single trailing `:param` segment—**Shipped (0.1)**. **Planned:** radix tree or perfect hash for large tables, method bitmaps per path, middleware chains stored as function pointer arrays.

Handlers have type `int32_t (*)(Ctx *)`. The runtime never calls back into the language except through this pointer.

## FFI: C runtime and foreign libraries [#ffi-c-runtime-and-foreign-libraries]

**Today:** Generated C includes `cforge_rt.h` and links `server.c`, `json.c`, `db.c` with `libsqlite3`, `libssl`, `libcrypto`, `pthread`. New runtime capabilities are added as C functions and whitelisted in the compiler’s builtin call table.

**Planned FFI layers:**

1. **Stable C ABI** — `CFORGE_API` functions documented in reference; versioned struct sizes for `Ctx` extensions.
2. **Dynamic plugins** — `.so` loaded at startup registering routes or background jobs (explicit opt-in; no runtime `dlopen` in 0.1).
3. **Foreign DB/cache clients** — PostgreSQL libpq or custom wire codec in C++; handlers still see `db_*`-style integers.
4. **Rust or Zig shims** — thin wrappers exporting `extern "C"` symbols matching `CForgeFn` and allocator rules.

FFI code must respect **arena lifetimes**: any `Slice` handed to a handler must either point at static data, connection `in[]`, or the request arena until reset.

## Machine backend boundary [#machine-backend-boundary]

The [machine backend](../learn/machine.html) compiles a different subset to x86-64 ELF without linking the HTTP runtime. Registers `r0`–`r7` map to System V argument registers. Syscalls are explicit. There is no `Ctx` or `app_listen` on this path—it proves the front-end can target non-C ABIs.

Long-term, shared middle-end IR would feed both C codegen and machine codegen; handlers for microservices remain on the C server path until the IR backend can link a minimal runtime.

## Anti-patterns at the boundary [#anti-patterns-at-the-boundary]

* Parsing HTTP or SQL in `.cforge` — duplicates runtime, breaks size limits.
* Calling `malloc` from handlers — violates allocation invariants; use runtime helpers that draw from the arena.
* Holding `Slice` from `db_get_user` after return — use-before-free when the arena resets.
* Blocking syscalls in handlers — stalls the single-threaded epoll loop in 0.1; **planned** worker pool offloads such calls.

## Summary [#summary]

The boundary is intentional: **express policy in CForge, express mechanism in C**. Extending the ecosystem means growing `cforge_rt.h` and keeping the language dumb about wire formats. See [http-async.html](http-async.html) for protocol layer detail and [memory.html](memory.html) for ownership rules.
