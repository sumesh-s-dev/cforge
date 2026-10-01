# Memory and slices (/docs/learn/memory)



CForge services use **fixed buffers** and **request arenas**. There is no garbage collector on the hot path.

## Per-connection layout [#per-connection-layout]

```text
Connection
 ├── in_buf     (read buffer, HTTP + body)
 ├── out_buf    (response bytes)
 └── arena      (bump allocator, reset each request)
```

Default sizes are documented in `runtime/internal.h` (`CFORGE_MAX_HEADER`, `CFORGE_MAX_BODY`, `CFORGE_ARENA`, etc.).

## Request lifetime [#request-lifetime]

```text
read bytes → parse HTTP → handler runs
    → slices point into in_buf or arena
    → response written to out_buf
    → arena.reset()
    → db checkout released
```

Anything allocated in the **arena** dies at `arena.reset()`. Do not store arena `Slice` values in globals.

## Slice rules [#slice-rules]

| Operation                   | Allocates?                          |
| --------------------------- | ----------------------------------- |
| `Slice` from string literal | No (static bytes)                   |
| JSON string into arena      | Yes (arena bump)                    |
| SQL text column into arena  | Yes (copy for reuse of wire buffer) |
| `ctx_json_*` output         | Written to `out_buf`                |

Database functions copy text into `ctx->arena` because the SQLite statement buffer is reused.

## Stack [#stack]

Locals (`let x: u64 = 0`) live on the C stack for the duration of the handler call.

## Safety modes (planned) [#safety-modes-planned]

See [Safety modes](../reference/safety.html): `@unsafe` and `@checked` are accepted and stripped by the lexer; `@safe` compile mode is planned — not enforced beyond explicit coding discipline.

## Further reading [#further-reading]

Repository: `END-TO-END.md` — full request/response lifecycle.
