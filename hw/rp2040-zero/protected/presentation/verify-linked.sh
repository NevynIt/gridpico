#!/usr/bin/env bash
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1

presentation="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
bundle="$(dirname -- "$presentation")"
replay="$(mktemp -d)"
trap 'rm -rf -- "$replay"' EXIT
python3 -c 'import pymupdf; assert pymupdf.VersionBind == "1.26.5"'

(
    cd "$bundle/electrical"
    test "$(sha256sum SHA256SUMS | cut -d ' ' -f 1)" =         31f4c8170146ce23628559881289345713535ba592a4c9557785650c84561b66
    sha256sum --quiet -c SHA256SUMS
)
test "$(sha256sum "$bundle/review/HAL.md" | cut -d ' ' -f 1)" =     300cc6df79bfe35a99f95e120f3a3b95364d08c932008f4df60c019bb0ff3dbb
echo "PASS 44-file electrical candidate and independent HAL receipt."

cp -a "$bundle/electrical" "$presentation" "$replay/"
python3 "$replay/presentation/render_candidate.py"
for name in Gridpico-Four-Port-Simplified.pdf parts-links.csv; do
    cmp "$presentation/$name" "$replay/presentation/$name"
done
diff -qr "$presentation/drawings" "$replay/presentation/drawings"
diff -qr "$presentation/verification" "$replay/presentation/verification"
for local_file in "$presentation/verification/"*connectivity.csv "$presentation/verification/"*-graph.txt; do
    cmp "$local_file" "$bundle/electrical/verification/${local_file##*/}"
done

(
    cd "$bundle"
    python3 - <<'PY'
from pathlib import Path
import csv
from xml.etree import ElementTree as ET

def drawing_marks(path):
    def is_electrical(item):
        return item.tag.endswith("}circle") or any(
            attr in item.attrib for attr in ("data-wire", "data-ground", "data-nc-x"))
    return [item.attrib for item in ET.parse(path).getroot() if is_electrical(item)]

for svg in Path("presentation/drawings").glob("sheet-*.svg"):
    assert drawing_marks(svg) == drawing_marks(Path("electrical/drawings") / svg.name)
reading_copy = Path("review/CURRENT-FAULT.md").read_bytes()
sealed = Path("electrical/CURRENT-FAULT.md").read_bytes()
old = b"Physical J2 sense opens native data."
new = "J2 D± are isolated when J2_PRESENT is low.".encode()
assert sealed.count(old) == 1
expected = sealed.replace(old, new)
expected = expected.replace(b"](baseline/)", b"](../electrical/baseline/)")
expected = expected.replace(b"](verification/power-screen.txt)", b"](../electrical/verification/power-screen.txt)")
assert reading_copy == expected
with Path("electrical/pin-netlist.csv").open() as stream:
    assert len(list(csv.DictReader(stream))) == 303
with Path("presentation/parts-links.csv").open() as stream:
    rows = list(csv.DictReader(stream))
assert len(rows) == 85
assert sum(bool(row["role"]) for row in rows) == 13
assert sum(bool(row["manufacturer_document"]) for row in rows) == 18
assert sum(row["status"] == "exact_part_open" for row in rows) == 67
PY
)
echo "PASS regenerated PDF/SVG, parts link index, all four graph ledgers and electrical wire/junction/NC positions."
echo "PASS reviewed fault-matrix wording and relocated relative links."
