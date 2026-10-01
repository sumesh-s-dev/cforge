# CForge

Low-level systems language: **dual compilers** (C server + native ELF), epoll HTTP/TLS/SQLite (optional Postgres/Redis), [full backend specification](https://sumesh-s-dev.github.io/cforge/docs/spec/).

**Documentation:** https://sumesh-s-dev.github.io/cforge/docs/

**Repository:** https://github.com/sumesh-s-dev/cforge

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
./cforge e2e               # test + systemd deploy + API smoke on :8080
./cforge everything        # e2e path + lockfile + docs build + docker (if installed)

PORT=8080 CFORGE_DB=data/users.db ./cforge run
# or
./cforge deploy            # systemd user unit on 8080 / 8443 (localhost)
./cforge deploy-docker     # Compose: Postgres + Redis + app (needs Docker)
```

Set `CFORGE_BIND=127.0.0.1` by default. Optional `CFORGE_PG_DSN`, `CFORGE_REDIS_URL`. TLS uses self-signed certs under `deploy/` (created on deploy).

## Users API

| Method | Path | Notes |
|---|---|---|
| GET | `/health` | `ok` |
| GET | `/ready` | `ready` |
| GET | `/metrics` | text counters (`db_backend`, `redis_backend`, …) |
| GET | `/ws` | WebSocket upgrade + text echo (runtime) |
| POST | `/users` | JSON `{"name":"...","age":N}`; bad JSON → problem+json |
| GET | `/users/:id` | JSON user |
| DELETE | `/users/:id` | `204` or `404` |

## Documentation

- **Book (web):** https://sumesh-s-dev.github.io/cforge/docs/
- **Service deep-dive:** [END-TO-END.md](./END-TO-END.md)
- **Regenerate site:** `cd docs-site && npm ci && npm run build` (source: `docs/content/`)

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
./cforge deploy            # optional: local systemd user service
./cforge all               # test + deploy + git push (when clean)
```

CI runs `./cforge test`, machine tests, Postgres smoke, and docs build on push to `main`. Pages: https://sumesh-s-dev.github.io/cforge/docs/
