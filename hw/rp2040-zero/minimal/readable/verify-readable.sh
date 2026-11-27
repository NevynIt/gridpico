#!/usr/bin/env bash
set -euo pipefail

drawing_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
baseline_dir="$(dirname -- "$drawing_dir")"
replay_dir="$(mktemp -d)"
trap 'rm -rf -- "$replay_dir"' EXIT
export PYTHONDONTWRITEBYTECODE=1
python3 -c 'import pymupdf; assert pymupdf.VersionBind == "1.26.5"'

mkdir -p "$replay_dir/readable"
cp -a "$baseline_dir/"*.{py,csv,txt,md,pdf,svg} "$baseline_dir/SHA256SUMS"     "$baseline_dir/drawings" "$baseline_dir/review" "$replay_dir/"
cp -a "$drawing_dir/"*.py "$replay_dir/readable/"
(
    cd -- "$replay_dir"
    test "$(sha256sum SHA256SUMS | cut -d ' ' -f 1)" =         f3fb48a66b668eee909210acc3944bb5537095314d2a35f49475eee1373aacbb
    sha256sum --quiet -c SHA256SUMS
    python3 render.py
    python3 overview.py
    python3 comparison.py
    python3 power_budget.py > power-budget.txt
    python3 verify.py >> design-check.txt
    sha256sum --quiet -c SHA256SUMS
    echo "PASS all 24 current minimum-package files before and after replay."
)

python3 "$replay_dir/readable/redraw.py"
diff -qr -- "$drawing_dir/drawings" "$replay_dir/readable/drawings"
cmp -- "$drawing_dir/parts-links.csv" "$replay_dir/readable/parts-links.csv"
for file in "$replay_dir/readable/verification/"*; do
    cmp -- "$file" "$drawing_dir/verification/${file##*/}"
done
echo "PASS byte-identical regenerated PDF, SVGs, part/link index and endpoint ledgers."
