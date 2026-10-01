# Toolchain commands (/docs/reference/toolchain)



All commands run from the repository root via `./cforge`.

| Command                    | Description                                                                      |
| -------------------------- | -------------------------------------------------------------------------------- |
| `build`                    | Server backend: `.cforge` → C → `build/cforge-users`                             |
| `check`                    | Parse and emit `build/users.gen.c` without linking                               |
| `run`                      | Build and replace process with server binary                                     |
| `test`                     | Integration tests (HTTP + TLS + WebSocket + machine ELF)                         |
| `deploy`                   | Build, install systemd user unit, restart, health check                          |
| `e2e`                      | `test` + `deploy` + users API smoke on `:8080`                                   |
| `everything`               | `check` + `package` + `e2e` + docs-site build + `deploy-docker` if Docker exists |
| `deploy-docker`            | `docker compose` Postgres + Redis + app on `:8080`                               |
| `status`                   | GET `/health` and `/metrics` on port 8080                                        |
| `all`                      | `test` + `deploy` + `status` + git publish script                                |
| `package`                  | Write `cforge.lock` source fingerprints                                          |
| `machine <file> [-o path]` | Native ELF backend                                                               |
| `machine-test`             | Verify mach/add and mach/expr exit codes                                         |

## Scripts [#scripts]

| Script                      | Purpose                                |
| --------------------------- | -------------------------------------- |
| `scripts/publish-github.sh` | Push to github.com/sumesh-s-dev/cforge |
| `scripts/do-all.sh`         | Same as `cforge all`                   |
| `docs/build_docs.py`        | Regenerate static HTML docs            |

## CI [#ci]

GitHub Actions `.github/workflows/ci.yml`: `./cforge test`, machine tests, Postgres smoke job, docs build job.

Pages deploy: `.github/workflows/pages.yml` → [https://sumesh-s-dev.github.io/cforge/](https://sumesh-s-dev.github.io/cforge/)
