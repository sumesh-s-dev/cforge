# Toolchain commands

All commands run from the repository root via `./cforge`.

| Command | Description |
|---|---|
| `build` | Server backend: `.cforge` → C → `build/cforge-users` |
| `check` | Parse and emit `build/users.gen.c` without linking |
| `run` | Build and replace process with server binary |
| `test` | Integration tests (HTTP + TLS + machine ELF) |
| `deploy` | Build, install systemd user unit, health check |
| `status` | GET `/health` and `/metrics` on port 8080 |
| `all` | `test` + `deploy` + `status` + git publish script |
| `machine <file> [-o path]` | Native ELF backend |
| `machine-test` | Verify mach/add and mach/expr exit codes |

## Scripts

| Script | Purpose |
|---|---|
| `scripts/publish-github.sh` | Push to github.com/sumesh-s-dev/cforge |
| `scripts/do-all.sh` | Same as `cforge all` |
| `docs/build_docs.py` | Regenerate static HTML docs |

## CI

GitHub Actions workflow `.github/workflows/ci.yml` runs `./cforge test` on Ubuntu.

Pages deploy: `.github/workflows/pages.yml` → https://sumesh-s-dev.github.io/cforge/
