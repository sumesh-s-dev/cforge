#!/usr/bin/env bash
# Build a local redis-server binary (no root). Used by ./cforge deploy for CFORGE_REDIS_URL.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
VER=7.4.2
OUT="$BUILD/redis-server-bin"
if [[ -x "$OUT" ]]; then
  echo "$OUT"
  exit 0
fi
mkdir -p "$BUILD"
cd "$BUILD"
curl -fsSL -o redis.tgz "https://download.redis.io/releases/redis-${VER}.tar.gz"
tar xf redis.tgz
make -C "redis-${VER}" -j"$(nproc)"
cp "redis-${VER}/src/redis-server" "$OUT"
echo "$OUT"
