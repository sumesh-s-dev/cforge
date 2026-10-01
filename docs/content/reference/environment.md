# Environment variables

Read at process start by the runtime (`app_listen`).

| Variable | Default | Meaning |
|---|---|---|
| `PORT` | `8080` | HTTP listen port when `app_listen(0)` |
| `TLS_PORT` | `8443` | TLS port if certs set |
| `CFORGE_BIND` | `127.0.0.1` | IPv4 bind address |
| `CFORGE_DB` | `data/users.db` | SQLite database path |
| `CFORGE_TLS_CERT` | unset | PEM certificate path |
| `CFORGE_TLS_KEY` | unset | PEM private key path |
| `CFORGE_MAX_CONNS` | `128` | Connection slab size (1–4096) |
| `CFORGE_DB_POOL` | `4` | SQLite connections in pool (1–8) |

Parent directory of `CFORGE_DB` is created if missing.

## systemd example

See `deploy/cforge-users.service.example` — replace `__CFORGE_ROOT__` with clone path.

## Security

Default bind is localhost. Use `CFORGE_BIND=0.0.0.0` only with firewall rules and real TLS certificates.

Private keys are gitignored; generate with `./cforge deploy` or OpenSSL.
