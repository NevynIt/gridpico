#!/usr/bin/env bash
set -euo pipefail

# Run in an interactive shell pane INSIDE gridpico's Herdr/container.
# The human approved Docker-only isolation for gridpico on 2026-11-22.
if [[ ! -t 0 || ! -t 1 ]]; then
  printf 'Run in an interactive terminal in the gridpico container.\n' >&2
  exit 1
fi
cd /workspace/gridpico
if [[ ! -r AGENTS.md || ! -r .team/roles/session-manager.md ]]; then
  printf 'gridpico workspace not mounted at /workspace/gridpico.\n' >&2
  exit 1
fi

model="${GRIDPICO_SESSION_MANAGER_MODEL:-gpt-6-luna}"
effort="${GRIDPICO_SESSION_MANAGER_EFFORT:-low}"
printf 'Starting Session Manager: %s, effort=%s, Docker-only execution.\n' "$model" "$effort"
printf 'Codex commands are unrestricted INSIDE the container, including credential mounts.\n'

exec codex \
  --sandbox danger-full-access --ask-for-approval never \
  -m "$model" -c "model_reasoning_effort=\"$effort\"" \
  "Read AGENTS.md, AGENT_TEAM_COORDINATION.md, .team/MODEL_PLAN.md, .team/APPROVED_AGENTS.md, .team/roles/session-manager.md and docs/CODEX_RUNTIME.md. You are Session Manager. Using the approved Docker-only execution mode for ALL agents, create only the Strategist, Architect and Project Manager persistent Codex sessions; pass their assigned model/effort and explicit --sandbox danger-full-access --ask-for-approval never launch settings. Do not make model or budget decisions independently, do not create additional seats."
