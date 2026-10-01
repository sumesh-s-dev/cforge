# Installation

## Requirements

| Component | Server path | Machine path |
|---|---|---|
| Python 3 | yes (compiler driver) | yes |
| GCC or Clang | yes | no |
| pkg-config + libsqlite3 | yes | no |
| OpenSSL dev libs | yes (TLS) | no |
| Linux x86-64 | yes | yes |

## Clone and verify

```bash
git clone git@github.com:sumesh-s-dev/cforge.git
cd cforge
./cforge test
```

You should see `ok 14 checks` and machine ELF tests.

## Build the users service

```bash
./cforge build
# → build/cforge-users
```

## Optional: systemd deploy (localhost)

```bash
./cforge deploy
curl http://127.0.0.1:8080/health
```

TLS on `https://127.0.0.1:8443` uses self-signed certs under `deploy/` (created on first deploy).

## Publish documentation locally

```bash
python3 docs/build_docs.py
# open docs/index.html in a browser
```

Public docs URL after GitHub Pages deploy: **https://sumesh-s-dev.github.io/cforge/**

## Environment overview

See [Environment variables](../reference/environment.html) for `CFORGE_*` and `PORT`.
