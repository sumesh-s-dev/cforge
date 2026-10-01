# First program (/docs/learn/first-program)



## Server path: register a route [#server-path-register-a-route]

Edit `app/users.cforge` or add handlers in `main`:

```c
fn hello(ctx: *Ctx) -> i32 {
    return ctx_text(ctx, 200, "hello");
}

fn main() -> i32 {
    app_get("/hello", hello);
    return app_listen(0);
}
```

Build and run:

```bash
./cforge build
PORT=9090 CFORGE_DB=data/users.db ./build/cforge-users
curl http://127.0.0.1:9090/hello
```

`app_listen(0)` reads `PORT` (default `8080`).

## Machine path: native ELF [#machine-path-native-elf]

`mach/add.cforge`:

```c
fn add(a: u64, b: u64) -> u64 {
    add r0, r1;
    ret;
}

fn main() -> u64 {
    return add(2, 3);
}
```

```bash
./cforge machine mach/add.cforge -o build/add
./build/add
echo $?   # 5
```

Or high-level return (lowered to add + ret):

```c
fn add(a: u64, b: u64) -> u64 {
    return a + b;
}
```

## Full stack example [#full-stack-example]

The repository includes `POST /users`, `GET /users/:id`, and `DELETE /users/:id` against SQLite. See [Runtime & HTTP](runtime.html) and the repo `END-TO-END.md`.

## Toolchain one-liner [#toolchain-one-liner]

```bash
./cforge all
```

Runs tests, local deploy, and `git push` when the tree is clean.
