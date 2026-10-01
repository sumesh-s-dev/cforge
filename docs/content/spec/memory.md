# Memory, arenas, zero-copy, and concurrency

CForge backend services are designed around **predictable memory**: preallocated slabs, per-request bump allocators, and explicit `Slice` ownership. This document specifies the 0.1 layout, zero-copy rules, and how concurrency models interact with lifetimes.

## Global allocation policy

**Shipped (0.1):** The request hot path does not call `malloc` or `free`. Startup allocates:

- Connection slot array (size × per-slot buffers)
- Route table strings (registration time only)
- SQLite connection and prepared statements
- OpenSSL context and certificate material

Failure to allocate at startup is fatal before listening. Runtime expansion of limits requires restart with new environment values.

Constants from `runtime/internal.h` (defaults):

| Constant | Value | Purpose |
|---|---|---|
| `CFORGE_MAX_CONNS_DEFAULT` | 128 | Connection slots |
| `CFORGE_MAX_HEADER` | 8192 | Header block limit |
| `CFORGE_MAX_BODY` | 65536 | Body limit |
| `CFORGE_MAX_NAME` | 256 | User name cap |
| `CFORGE_ARENA` | 4096 | Per-connection bump arena |
| `CFORGE_OUT_CAP` | 16384 | Response buffer |
| `CFORGE_MAX_ROUTES` | 64 | Registered routes |
| `CFORGE_IDLE_MS_DEFAULT` | 30000 | Idle timeout |

## Per-connection layout

Each slot owns:

```text
in[]     CFORGE_MAX_HEADER + CFORGE_MAX_BODY   raw HTTP including partial
out[]    CFORGE_OUT_CAP                        full response before/during write
arena    CFORGE_ARENA                          decoded strings, DB copies
```

The read buffer must hold one maximum header plus one maximum body simultaneously. The arena is a bump pointer: `cforge_arena_reset` sets offset to zero with no destructors.

## Request arena lifecycle

```text
accept → … → parse complete → handler runs (arena allocates)
  → response queued → write completes or continues
  → cforge_arena_reset + cforge_db_release
  → next request on same keep-alive connection reuses arena from offset 0
```

Handlers may allocate multiple times in one request until the arena exhausts; overflow returns `-1` from helpers and should map to `500`.

## Ownership table (POST /users example)

| Bytes | Location | Freed when |
|---|---|---|
| TLS state | slot | connection close |
| Raw HTTP body | `in[]` | cursor advances after handler |
| Decoded `name` | arena | arena reset |
| Row in SQLite | database file | `DELETE` or process exit |
| JSON in response | `out[]` | overwritten on next response |

`db_get_user` returns `Slice` pointers into the arena after copying SQLite text columns—required because `sqlite3_reset` invalidates column pointers. JSON writers must finish before arena reset.

## Zero-copy principles

**True zero-copy** (kernel to user without extra memcpy) applies sparingly:

| Path | Zero-copy? | Notes |
|---|---|---|
| Static string literal → `Slice` | Yes | Points at `.rodata` |
| HTTP body → handler `ctx->body` | Yes | View into `in[]` |
| Body → JSON string field | No | Escaping and validation copy |
| SQLite TEXT → handler | No | Copied to arena for safety |
| Response → socket | One memcpy to `out[]` then write | **Planned:** `writev` scatter if encoding streams |

**Planned:** `sendfile` for static assets; `splice` between sockets for proxy mode; read-only `Slice` into mmap’d config files for the process lifetime.

## Slice safety rules for authors

1. Do not store `Slice.ptr` in global or static variables.
2. Do not return `Slice` from handlers to the runtime except through documented out-parameters filled before return.
3. Treat `ctx->body` as valid only for the synchronous handler call.
4. Assume another pipelined request on the same connection will repurpose `in[]` after the current response is processed.

The language does not enforce borrow checking. **Planned** `@safe` mode would compile-time track `Slice` scopes; 0.1 relies on discipline and short arenas.

## Concurrency and memory

**Model A (0.1):** Single thread—no locks on slots, arenas, or SQLite use flag. `busy_timeout` on SQLite only matters if another process opens the same DB file.

**Model B (planned):** Workers must not share connection `in[]`/`out[]`. Patterns:

- Move request snapshot into owned buffer on handoff
- Per-worker arena pools to avoid cross-thread bump allocators
- Reference-counted immutable response bodies for cache hits

**Read-copy-update** for read-mostly config (route table, JWT public keys) allows reload without stopping the listener—swap pointer after parsing new table in staging buffer.

## Cache memory (design)

In-process LRU for hot keys uses a fixed byte cap from a dedicated slab, not the per-request arena. Eviction is explicit; no GC. Shared **Redis** cache stores serialized blobs; handlers receive copies into arena on hit. See [database.html](database.html).

## Fragmentation and long uptime

Bump arenas eliminate per-request heap fragmentation. Long-lived malloc at startup (routes, SSL) is bounded. **Planned** metrics: arena high-water mark per slot, `out[]` overflow count, slab OOM at accept.

## Debugging memory bugs

Symptoms and likely causes:

| Symptom | Likely cause |
|---|---|
| Garbage JSON | `Slice` into freed arena |
| Intermittent 500 on large names | Arena exhaustion |
| Crash after DELETE | Use-after-free on stored `Slice` |
| TLS memory growth | Missing `SSL_free` on error paths |

AddressSanitizer builds of the runtime are recommended for contributors; production binaries trade ASan for speed.

## Relation to machine backend

Native ELF programs use the stack and registers only—no arena. Teaching syscalls without libc malloc reinforces the same **explicit lifetime** mindset as the HTTP server.

See [boundary.html](boundary.html) for ABI types and [production.html](production.html) for capacity planning from slot and buffer sizes.
