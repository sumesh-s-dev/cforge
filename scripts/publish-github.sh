#!/usr/bin/env bash
# Publish this tree to https://github.com/sumesh-s-dev/cforge
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
remote="${1:-git@github.com:sumesh-s-dev/cforge.git}"

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  git init -b main
fi

if ! git remote get-url origin >/dev/null 2>&1; then
  git remote add origin "$remote"
fi

if ! git rev-parse HEAD >/dev/null 2>&1; then
  git add -A
  git commit -m "Initial CForge users service and machine backend"
fi

if ! git ls-remote --exit-code origin main >/dev/null 2>&1; then
  echo "Remote repository not found yet."
  echo ""
  echo "Create an empty public repo named 'cforge' on GitHub:"
  echo "  https://github.com/new?name=cforge"
  echo ""
  echo "Or, with GitHub CLI after 'gh auth login':"
  echo "  gh repo create sumesh-s-dev/cforge --public --source=. --remote=origin --push"
  echo ""
  exit 1
fi

git push -u origin main
echo "Published: https://github.com/sumesh-s-dev/cforge"
