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
else
  git remote set-url origin "$remote"
fi

if ! git rev-parse HEAD >/dev/null 2>&1; then
  git add -A
  git commit -m "Initial CForge users service and machine backend"
fi

if ! git ls-remote --exit-code origin main >/dev/null 2>&1; then
  echo "Remote repository not found yet."
  echo ""
  echo "Create a public repo named 'cforge' (empty, or with README — we merge either way):"
  echo "  https://github.com/new?name=cforge"
  echo ""
  exit 1
fi

git fetch origin main 2>/dev/null || git fetch origin

if ! git merge-base --is-ancestor origin/main HEAD 2>/dev/null && \
   ! git merge-base --is-ancestor HEAD origin/main 2>/dev/null; then
  if ! git merge-base HEAD origin/main >/dev/null 2>&1; then
    echo "Merging unrelated GitHub history (e.g. initial README)..."
    if ! git pull origin main --no-rebase --allow-unrelated-histories --no-edit; then
      if git ls-files -u | grep -q .; then
        git checkout --ours README.md 2>/dev/null || true
        git add -A
        git commit -m "Merge remote; keep project README" || true
      fi
    fi
  fi
fi

git push -u origin main
echo "Published: https://github.com/sumesh-s-dev/cforge"
echo "CI: https://github.com/sumesh-s-dev/cforge/actions"
