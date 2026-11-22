#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# Interactive terminal required. Opens Herdr, not any agent seat.
if [[ ! -t 0 || ! -t 1 ]]; then
  printf 'Open Herdr from an interactive terminal (SSH/TTY).\n' >&2
  exit 1
fi
exec docker compose -f "$root/.devcontainer/compose.yaml" exec -w /workspace/gridpico dev herdr "$@"
