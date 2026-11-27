#!/usr/bin/env bash
set -euo pipefail

fixture=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(git -C "$fixture" rev-parse --show-toplevel)
base=ff518c4aaf594e5edb03b2274ea68a45afac29c7
if ! git -C "$repo" cat-file -e "$base^{commit}"; then
    printf 'Historical PAT2 source commit %s is required in the local Git history.\n' "$base" >&2
    exit 1
fi

(cd "$fixture" && sha256sum -c SHA256SUMS)

scratch=$(mktemp -d)
trap 'rm -rf -- "$scratch"' EXIT
git -C "$repo" archive "$base" | tar -xf - -C "$scratch"
# Keep Git operations inside the temporary archive even if TMPDIR is in a worktree.
git --git-dir="$scratch/.git" --work-tree="$scratch" init -q
git --git-dir="$scratch/.git" --work-tree="$scratch" -C "$scratch" apply --whitespace=error-all \
    "$fixture/pico-24-branch-backing.patch" "$fixture/pat2-matrix24-fixture.patch"

cd "$scratch"
sha256sum -c <<'SUMS'
0def7aa5808d9d8bb3cd4e591325e7651816608d5318b28ee2686c6e9072d700  api/patchbay.h
4c9357f1baf0157442d26e7048544494dfd85cfe7bc6e989ebe16f68e5f61b05  app/tests/pat2_apply_host.inc
SUMS
bash app/tests/run_pat2_projection.sh --apply
