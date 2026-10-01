#!/usr/bin/env bash
# One-time: turn on GitHub Pages + repo About (needs gh auth as repo admin).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if ! command -v gh >/dev/null 2>&1; then
  echo "Install GitHub CLI: https://cli.github.com/"
  exit 1
fi

if ! gh auth status -h github.com >/dev/null 2>&1; then
  echo "Log in (browser opens; needs admin on sumesh-s-dev/cforge):"
  gh auth login -h github.com -p ssh -s repo,read:org
fi

REPO="sumesh-s-dev/cforge"
DESC="Low-level CForge language: dual compiler, epoll HTTP/TLS/SQLite server, full backend specification."
HOME="https://sumesh-s-dev.github.io/cforge/"

echo "Ensuring gh-pages is published (push main if needed)..."
git push origin main 2>/dev/null || true

echo "Enabling GitHub Pages from branch gh-pages..."
if ! gh api "repos/${REPO}/pages" -X POST \
  -f build_type=legacy \
  -f 'source[branch]=gh-pages' \
  -f 'source[path]=/' 2>/dev/null; then
  gh api "repos/${REPO}/pages" -X PUT \
    -f build_type=legacy \
    -f 'source[branch]=gh-pages' \
    -f 'source[path]=/' || true
fi

gh repo edit "$REPO" --description "$DESC" --homepage "$HOME" || true

echo ""
echo "Done. In 1–3 minutes open: $HOME"
echo "Mirror (works before Pages): https://cdn.jsdelivr.net/gh/${REPO}@gh-pages/index.html"
