"""Check readability constraints without asserting electrical qualification."""

import xml.etree.ElementTree as ET
from render import BASE, fitz

doc = fitz.open(BASE / "Gridpico-RP2040-Zero-Schematic.pdf")
assert len(doc) == len(list((BASE / "drawings").glob("sheet-*.svg"))) == 9
for i, page in enumerate(doc, 1):
    svg = ET.parse(BASE / "drawings" / f"sheet-{i:02}.svg")
    assert svg.getroot().tag == "{http://www.w3.org/2000/svg}svg"
    assert f"{i:02d} / 09" in page.get_text()
    words = page.get_text("words")
    for j, word in enumerate(words):
        area = fitz.Rect(*word[:4])
        assert page.rect.contains(area), (i, word)
        for other in words[j+1:]:
            other_area = fitz.Rect(*other[:4])
            overlap = area & other_area
            assert overlap.height <= .35 * min(area.height, other_area.height) or overlap.width <= 1.8, (i, word, other)
print("PASS nine parallel PDF/SVG sheets, valid XML, PDF text inside bounds and no detected text collisions.")
