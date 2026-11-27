"""Generate and verify the candidate's directly wired presentation."""

import csv
from pathlib import Path
from xml.etree import ElementTree as ET

from drawlib import Canvas, Drawing, fitz, PW, INK, SUB, EDGE, WHITE
from layouts import sheet_1, sheet_2, sheet_3, sheet_4

BASE = Path(__file__).resolve().parent
LAYOUTS = (
    (sheet_1, 1515, "Separate input protection and U1-only U3"),
    (sheet_2, 1550, "Automatic supply selection and protected U4"),
    (sheet_3, 2040, "USB data isolation and upstream admission"),
    (sheet_4, 1655, "RP2040-Zero and short 3.3 V accessories"),
)
BROKEN_AT = (("IC1", "9"), ("IC6", "7"), ("IC14", "4"), ("M1", "P1.11"))


def check_negative(drawing, terminal):
    pos = drawing.p(*terminal)
    index = next(i for i, segment in enumerate(drawing.segments) if pos in segment)
    original = drawing.segments.pop(index)
    try:
        drawing.compare()
    except AssertionError:
        return "PASS deleted local wire was rejected at " + ".".join(terminal)
    finally:
        drawing.segments.insert(index, original)
    raise AssertionError(terminal)


def check_pdf_text(page):
    raw = page.get_text("rawdict")
    ink = []
    for block in raw["blocks"]:
        for line in block.get("lines", []):
            for span in line["spans"]:
                chars = [c for c in span["chars"] if c["c"].strip()]
                if not chars:
                    continue
                assert all(page.rect.contains(fitz.Rect(c["bbox"])) for c in chars)
                box = fitz.Rect(chars[0]["bbox"])
                for c in chars[1:]:
                    box |= fitz.Rect(c["bbox"])
                ink.append(("".join(c["c"] for c in span["chars"]), box))
    overlaps = []
    for i, (label, box) in enumerate(ink):
        for other, second in ink[i + 1:]:
            intersect = box & second
            if intersect.width > 1.4 and intersect.height > 2.0:
                overlaps.append((label, other, round(intersect.width, 1), round(intersect.height, 1)))
    return overlaps


def main():
    with (BASE / "pin-netlist.csv").open(newline="") as file:
        rows = list(csv.DictReader(file))
    with fitz.open() as result:
        for index, (layout, height, title) in enumerate(LAYOUTS, 1):
            canvas = Canvas(result, height)
            canvas.rect(0, 0, PW, 98, INK, INK)
            canvas.text(38, 37, "GRIDPICO / FOUR-PORT DEV SUCCESSOR", 15, "#a7cbd1", True)
            canvas.text(38, 74, title, 31, WHITE, True)
            canvas.text(PW - 38, 73, f"PAPER REVIEW / {index:02} OF 04", 15, WHITE, True, "end")
            canvas.text(38, 123, "Distinct A5 / B5 rails  •  Retain four USB roles  •  Basis 0b431a9", 15, SUB)
            canvas.line(38, 143, PW - 38, 143, EDGE)
            canvas.line(38, height - 45, PW - 38, height - 45, EDGE)
            canvas.text(38, height - 21, "Matching remote labels and rails join electrically. X is deliberate NC. See REVIEW.md for fit and power holds.", 13, SUB)
            drawing = Drawing(canvas, rows, index)
            layout(drawing)
            summary = drawing.compare(export=True)
            rejected = check_negative(drawing, BROKEN_AT[index-1])
            (BASE / "verification" / f"sheet-{index:02}-graph.txt").write_text(summary + rejected + "\n")
            svg = BASE / "drawings" / f"sheet-{index:02}.svg"
            canvas.save(svg)
            root = ET.parse(svg).getroot()
            actual = [((int(a["x1"]), int(a["y1"])), (int(a["x2"]), int(a["y2"])))
                      for e in list(root) if (a := e.attrib).get("data-wire") == "true"]
            assert actual == drawing.segments
            for word in canvas.page.get_text("words"):
                assert canvas.page.rect.contains(fitz.Rect(word[:4])), (index, word)
            assert not (overlaps := check_pdf_text(canvas.page)), (index, overlaps)
            canvas.page.get_pixmap(matrix=fitz.Matrix(1, 1), alpha=False).save(
                BASE / "verification" / f"sheet-{index:02}.png")
        result.set_metadata({"title": "Gridpico four-port DEV · electrical successor paper candidate"})
        result.ez_save(BASE / "Gridpico-Four-Port-Simplified.pdf", no_new_id=True, deflate=True)


if __name__ == "__main__":
    main()
