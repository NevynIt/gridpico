#!/usr/bin/env bash
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/gridpico-patch.XXXXXXXX")"
trap 'rm -rf -- "$build_dir"' EXIT

if ! cmake -S "$root" -B "$build_dir" -G "Unix Makefiles" \
    -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release >"$build_dir/build.log" 2>&1 ||
   ! cmake --build "$build_dir" --target gridpico_patch_demo >>"$build_dir/build.log" 2>&1; then
    cat "$build_dir/build.log" >&2
    exit 1
fi

"$build_dir/gridpico_patch_demo"
