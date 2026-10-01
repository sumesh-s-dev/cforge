# CForge

Low-level systems language prototype with two compile paths:

| Path | Command | Output |
|---|---|---|
| **Server** (HTTP, SQLite, TLS) | `./cforge build` | `build/cforge-users` via generated C + runtime |
| **Machine** (registers, syscalls, ELF) | `./cforge machine mach/add.cforge` | native x86-64 ELF, no gcc |

Handlers live in `.cforge` files. HTTP, JSON, epoll, and SQLite stay in the C runtime—not in the language.

## Quick start

```bash
./cforge build
./cforge test              # integration tests (ports 18081/18443)
./cforge machine-test      # native ELF add/expr programs

PORT=8080 CFORGE_DB=data/users.db ./cforge run
# or
./cforge deploy            # systemd user unit on 8080 / 8443 (localhost)
```

Set `CFORGE_BIND=127.0.0.1` by default. TLS uses self-signed certs under `deploy/` (created on deploy).

## Users API

| Method | Path | Notes |
|---|---|---|
| GET | `/health` | `ok` |
| GET | `/metrics` | text counters |
| POST | `/users` | JSON `{"name":"...","age":N}` |
| GET | `/users/:id` | JSON user |
| DELETE | `/users/:id` | `204` or `404` |

## Documentation

See [END-TO-END.md](./END-TO-END.md) for architecture, memory model, request lifecycle, environment variables, and scope limits.

## Layout

```text
app/users.cforge     service source (C backend)
mach/*.cforge        machine-backend samples
runtime/             epoll HTTP server, JSON, SQLite
cforge               compiler + toolchain
cforge_mach.py       x86-64 ELF backend
```

## License

MIT — see [LICENSE](./LICENSE).

## GitHub

Source: [github.com/sumesh-s-dev/cforge](https://github.com/sumesh-s-dev/cforge)

After cloning:

```bash
git clone git@github.com:sumesh-s-dev/cforge.git
cd cforge
./cforge build && ./cforge test
./cforge deploy   # optional: local systemd user service
```

CI runs `./cforge test` (includes machine ELF checks) on every push to `main`.
