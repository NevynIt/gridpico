"""Generate and verify the candidate's directly wired presentation."""

import csv
from pathlib import Path
from xml.etree import ElementTree as ET

from drawlib import ELECTRICAL, Canvas, Drawing, fitz, PW, SCALE, INK, SUB, EDGE, WHITE
from layouts import sheet_1, sheet_2, sheet_3, sheet_4
from parts import DOCUMENTS, IC_ROLES

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


def validate_svg(drawing, svg_path):
    root = ET.parse(svg_path).getroot()
    tags = list(root)
    references = {e.get("data-ref"): e.text for e in tags if e.get("data-ref")}
    assert references == {ref: ref if ref in DOCUMENTS else f"{ref}*" for ref in drawing.values}
    roles = {e.get("data-role-for"): e.text for e in tags if e.get("data-role-for")}
    assert roles == {ref: IC_ROLES[ref] for ref in drawing.values if ref in IC_ROLES}
    with fitz.open(svg_path) as svg:
        visible = svg[0].get_text()
        assert all(role in visible for role in roles.values())
        assert all(drawing.values[ref] in visible for ref in drawing.links)
    anchors = root.findall("{*}a")
    refs = {ref for ref in drawing.values if ref in DOCUMENTS}
    assert len(anchors) == len(refs) and {anchor.get("data-part") for anchor in anchors} == refs
    displayed = {e.get("data-part-value"): e for e in tags if e.get("data-part-value")}
    assert set(displayed) == refs
    for anchor in anchors:
        ref = anchor.get("data-part")
        assert anchor.get("href") == anchor.get("{http://www.w3.org/1999/xlink}href") == DOCUMENTS[ref].url
        assert displayed[ref].text == drawing.values[ref]
        hit_area = anchor.find("{*}rect")
        assert hit_area.get("pointer-events") == "all"
        x, y, w, h = [float(hit_area.get(k)) * SCALE for k in ("x", "y", "width", "height")]
        assert max(abs(f - g) for f, g in zip((x, y, x + w, y + h), drawing.links[ref], strict=True)) < .02
        assert DOCUMENTS[ref].part in anchor.find("{*}title").text
    segments = [((int(tag["x1"]), int(tag["y1"])), (int(tag["x2"]), int(tag["y2"])))
                for element in tags if (tag := element.attrib).get("data-wire") == "true"]
    assert segments == drawing.segments
    circles = {(int(e.get("cx")), int(e.get("cy"))) for e in tags if e.tag.endswith("}circle")}
    assert circles == drawing.junctions
    grounds = {(int(e.get("x1")), int(e.get("y1"))) for e in tags if e.get("data-ground") == "true"}
    assert grounds == {point for point, _, role in drawing.labels if role == "ground"}
    no_connects = [(int(e.get("data-nc-x")), int(e.get("data-nc-y"))) for e in tags
                   if e.get("data-nc-x") is not None]
    assert set(no_connects) == drawing.unconnected and len(no_connects) == 2 * len(drawing.unconnected)
    labels = {(e.text, int(e.get("x")), int(e.get("y")))
              for e in tags if e.tag.endswith("}text") and e.get("y", "").isdigit()
              and e.get("x", "").isdigit()}
    for (x, y), net, role in drawing.labels:
        if role != "ground":
            assert (net, x, y - (8 if role == "label" else 20)) in labels, (net, x, y)


def check_pdf_links(page, drawing):
    links = page.get_links()
    assert len(links) == len(drawing.links)
    visible = page.get_text()
    assert all(role in visible for ref, role in IC_ROLES.items() if ref in drawing.values)
    for ref, rect in drawing.links.items():
        matches = [link for link in links if link["kind"] == fitz.LINK_URI
                   and link["uri"] == DOCUMENTS[ref].url
                   and max(abs(x - y) for x, y in zip(link["from"], rect, strict=True)) < .02]
        assert len(matches) == 1 and page.rect.contains(rect), ref
        # The clickable rectangle lands on the displayed value, even where a URL is reused.
        assert drawing.values[ref] in page.get_textbox(rect + (-1, -1, 1, 1)), ref


def write_parts_index(rows):
    values = {row["reference"]: (row["sheet"], row["value"]) for row in rows}
    assert set(DOCUMENTS) <= set(values)
    assert set(IC_ROLES) == {ref for ref in values if ref.startswith("IC")}
    with (BASE / "parts-links.csv").open("w", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(("reference", "sheet", "published_value", "role", "status",
                         "manufacturer", "manufacturer_code_or_model", "manufacturer_document"))
        for ref, (sheet, value) in sorted(values.items()):
            document = DOCUMENTS.get(ref)
            if document is None:
                fields = ("exact_part_open", "", "", "")
            else:
                fields = (document.status, document.maker, document.part, document.url)
                if document.status == "recorded_order_code":
                    assert value == document.part, ref
            writer.writerow((ref, sheet, value, IC_ROLES.get(ref, ""), *fields))


def main():
    with (ELECTRICAL / "pin-netlist.csv").open(newline="") as file:
        rows = list(csv.DictReader(file))
    write_parts_index(rows)
    drawings = []
    with fitz.open() as result:
        for index, (layout, height, title) in enumerate(LAYOUTS, 1):
            height += 24
            canvas = Canvas(result, height)
            canvas.svg[0] = canvas.svg[0].replace("<svg ", '<svg xmlns:xlink="http://www.w3.org/1999/xlink" ', 1)
            canvas.rect(0, 0, PW, 98, INK, INK)
            canvas.text(38, 37, "GRIDPICO / FOUR-PORT DEV SUCCESSOR", 15, "#a7cbd1", True)
            canvas.text(38, 74, title, 31, WHITE, True)
            canvas.text(PW - 38, 73, f"PAPER REVIEW / {index:02} OF 04", 15, WHITE, True, "end")
            canvas.text(38, 123, "Electrical candidate 31f4c817  •  Automatic SYS5 brief c86a8d3", 15, SUB)
            canvas.line(38, 143, PW - 38, 143, EDGE)
            canvas.line(38, height - 62, PW - 38, height - 62, EDGE)
            canvas.text(38, height - 21, "Blue values open manufacturer documents. * Exact manufacturer or order code open.", 13, SUB)
            canvas.text(38, height - 42, "Matching remote labels and rails join electrically. X is deliberate NC. Fit and powered use held.", 13, SUB)
            drawing = Drawing(canvas, rows, index)
            layout(drawing)
            summary = drawing.compare(export=True)
            rejected = check_negative(drawing, BROKEN_AT[index-1])
            (BASE / "verification" / f"sheet-{index:02}-graph.txt").write_text(summary + rejected + "\n")
            svg = BASE / "drawings" / f"sheet-{index:02}.svg"
            canvas.save(svg)
            validate_svg(drawing, svg)
            drawings.append(drawing)
            for word in canvas.page.get_text("words"):
                assert canvas.page.rect.contains(fitz.Rect(word[:4])), (index, word)
            assert not (overlaps := check_pdf_text(canvas.page)), (index, overlaps)
            canvas.page.get_pixmap(matrix=fitz.Matrix(1, 1), alpha=False).save(
                BASE / "verification" / f"sheet-{index:02}.png")
        result.set_metadata({"title": "Gridpico four-port DEV · electrical successor paper candidate"})
        dest = BASE / "Gridpico-Four-Port-Simplified.pdf"
        result.ez_save(dest, no_new_id=True, deflate=True)
    with fitz.open(dest) as verified:
        for page, drawing in zip(verified, drawings, strict=True):
            check_pdf_links(page, drawing)
    print(f"PASS all {len(IC_ROLES)} role captions and {len(DOCUMENTS)} placed manufacturer links in PDF and SVG.")


if __name__ == "__main__":
    main()
