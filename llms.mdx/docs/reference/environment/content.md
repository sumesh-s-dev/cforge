# Environment variables (/docs/reference/environment)



Read at process start by the runtime (`app_listen`).

| Variable           | Default         | Meaning                                                                           |
| ------------------ | --------------- | --------------------------------------------------------------------------------- |
| `PORT`             | `8080`          | HTTP listen port when `app_listen(0)`                                             |
| `TLS_PORT`         | `8443`          | TLS port if certs set                                                             |
| `CFORGE_BIND`      | `127.0.0.1`     | IPv4 bind address                                                                 |
| `CFORGE_DB`        | `data/users.db` | SQLite database path (ignored when Postgres is active)                            |
| `CFORGE_PG_DSN`    | unset           | Optional Postgres libpq DSN (e.g. `postgres://user:pass@host/db?sslmode=disable`) |
| `CFORGE_REDIS_URL` | unset           | Optional Redis (`redis://host:6379`) for cache key invalidation                   |
| `CFORGE_TLS_CERT`  | unset           | PEM certificate path                                                              |
| `CFORGE_TLS_KEY`   | unset           | PEM private key path                                                              |
| `CFORGE_MAX_CONNS` | `128`           | Connection slab size (1–4096)                                                     |
| `CFORGE_IDLE_MS`   | `30000`         | Close idle connections after this many ms                                         |
| `CFORGE_DB_POOL`   | `4`             | SQLite connections in pool (1–8)                                                  |

Parent directory of `CFORGE_DB` is created if missing.

## systemd example [#systemd-example]

See `deploy/cforge-users.service.example` — replace `__CFORGE_ROOT__` with clone path.

## Security [#security]

Default bind is localhost. Use `CFORGE_BIND=0.0.0.0` only with firewall rules and real TLS certificates.

Private keys are gitignored; generate with `./cforge deploy` or OpenSSL.
