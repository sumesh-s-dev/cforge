#!/usr/bin/env bash
# Build, test, deploy locally, and push to GitHub if the tree is clean.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "== build + test =="
./cforge test

echo "== local deploy (systemd user) =="
./cforge deploy

echo "== status =="
./cforge status

if [[ -n "$(git status --porcelain)" ]]; then
  echo "Uncommitted changes; commit before publishing."
  git status --short
  exit 1
fi

echo "== publish GitHub =="
./scripts/publish-github.sh

echo "Done."
