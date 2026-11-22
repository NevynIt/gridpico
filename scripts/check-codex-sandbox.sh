#!/usr/bin/env bash
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
compose=(docker compose -f "$root/.devcontainer/compose.yaml")

printf 'Checking Codex Linux sandbox inside the running gridpico dev container...\n'
if ! "${compose[@]}" ps --services --status running | grep -Fxq dev; then
    printf 'Gridpico dev is not running; run ./scripts/start.sh first.\n' >&2
    exit 1
fi

# Never print mounted authentication material or modify shared Codex config.
# The command runs inside Codex's nested Linux sandbox, not merely Docker.
if "${compose[@]}" exec -T -w /workspace/gridpico dev     codex sandbox linux -- /bin/sh -c 'test -r AGENTS.md && cat AGENTS.md >/dev/null'; then
    printf '\nPASS: Codex sandbox can run a read-only workspace command.\n'
    printf 'Next: check Herdr controls and workspace-write access for Session Manager.\n'
else
    status=$?
    printf '\nFAIL: Codex nested sandbox could not execute a basic read (exit %d).\n' "$status" >&2
    printf 'Do not start more Codex agents until the environment is repaired or a human approves an alternative launch mode.\n' >&2
    printf 'See docs/CODEX_RUNTIME.md.\n' >&2
    exit "$status"
fi
