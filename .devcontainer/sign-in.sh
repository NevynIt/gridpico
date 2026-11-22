#!/usr/bin/env bash
set -euo pipefail

# Run inside the interactive gridpico dev container.
# Authentication persists in the explicitly named Docker volumes.
if [[ ! -t 0 || ! -t 1 ]]; then
    printf 'Run this script in an interactive gridpico container terminal.\n' >&2
    exit 1
fi

: "${CODEX_HOME:=/home/pico/.codex}"
: "${GH_CONFIG_DIR:=/home/pico/.config/gh}"

if [[ "$CODEX_HOME" != /home/pico/.codex || "$GH_CONFIG_DIR" != /home/pico/.config/gh ]]; then
    printf 'Unexpected credential locations. Check .devcontainer/compose.yaml.\n' >&2
    exit 1
fi

for path in "$CODEX_HOME" "$GH_CONFIG_DIR"; do
    if ! mountpoint -q "$path"; then
        printf 'Not a persistent volume mount: %s\n' "$path" >&2
        printf 'Start gridpico with its Docker Compose file before authenticating.\n' >&2
        exit 1
    fi
done

codex_auth=(codex -c 'cli_auth_credentials_store="file"')
if "${codex_auth[@]}" login status; then
    printf 'Codex: already signed in.\n'
else
    printf 'Codex: starting device authorization.\n'
    "${codex_auth[@]}" login --device-auth
    "${codex_auth[@]}" login status
fi

if gh auth status --hostname github.com; then
    printf 'GitHub CLI: already signed in.\n'
else
    printf 'GitHub CLI: starting web authorization.\n'
    gh auth login --hostname github.com --git-protocol https --web
    gh auth status --hostname github.com
fi

printf '\nCredentials verified. They are stored in the server-side Docker volumes.\n'
