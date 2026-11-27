"""Verify review rendering and that every BOM item traces to the source."""

import csv
import xml.etree.ElementTree as ET

from render import BASE, fitz


def check_visual(pdf_path, svg_paths):
    with fitz.open(pdf_path) as doc:
        assert len(doc) == len(svg_paths)
        for page, path in zip(doc, svg_paths):
            assert ET.parse(path).getroot().tag == "{http://www.w3.org/2000/svg}svg"
            words = page.get_text("words")
            for i, word in enumerate(words):
                area = fitz.Rect(*word[:4])
                assert page.rect.contains(area), (path.name, word)
                for other in words[i + 1:]:
                    other_area = fitz.Rect(*other[:4])
                    overlap = area & other_area
                    if overlap.width > 1.8 and overlap.height > .35 * min(area.height, other_area.height):
                        raise AssertionError((path.name, word, other))


def main():
    pages = [BASE / "drawings" / f"sheet-{n:02d}.svg" for n in range(1, 5)]
    check_visual(BASE / "Gridpico-RP2040-Zero-Schematic.pdf", pages)
    check_visual(BASE / "carrier-overview.pdf", [BASE / "carrier-overview.svg"])
    with (BASE / "pin-netlist.csv").open(newline="", encoding="utf-8") as stream:
        refs = {row["reference"]: row["value"] for row in csv.DictReader(stream)}
    with (BASE / "bom.csv").open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    assert len(rows) == len(refs) == 102
    assert {row["reference"]: row["part_or_value"] for row in rows} == refs
    print("PASS four schematic sheets and one-page overview, valid SVG, no detected text collisions, exact BOM coverage.")


if __name__ == "__main__":
    main()
