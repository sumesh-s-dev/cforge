#!/usr/bin/env bash
# Rootless Docker (no sudo after deps). Run once in a real terminal if this fails here.
set -euo pipefail
if command -v docker >/dev/null && docker info >/dev/null 2>&1; then
  echo "docker ok"
  exit 0
fi
if [[ ! -x "$HOME/bin/dockerd-rootless-setuptool.sh" ]]; then
  curl -fsSL https://get.docker.com/rootless | sh
fi
export PATH="$HOME/bin:$PATH"
export DOCKER_HOST=unix://"$HOME"/.docker/run/docker.sock
dockerd-rootless-setuptool.sh install
echo 'Add to shell: export PATH="$HOME/bin:$PATH" DOCKER_HOST=unix://'"$HOME"'/.docker/run/docker.sock'
