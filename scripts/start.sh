#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# Starting never silently rebuilds the image.
exec docker compose -f "$root/.devcontainer/compose.yaml" up -d --no-build dev "$@"
