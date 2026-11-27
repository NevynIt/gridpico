"""Render four locally wired review sheets and verify their network and raster parity."""

import argparse
import csv
from pathlib import Path
from xml.etree import ElementTree as ET

from drawing import BASELINE, ROOT, PW, SCALE, Canvas, Drawing, fitz, INK, SUB, EDGE, WHITE
from layouts import sheet_1, sheet_2, sheet_3, sheet_4
from parts import DOCUMENTS, IC_ROLES

OUT = ROOT / "drawings"
CHECK = ROOT / "verification"
PUBLISHED_PDF = "Gridpico-RP2040-Zero-Schematic.pdf"
TITLES = (
    ("Dedicated U1 power and protected U3", "Separate protected A5 and B5 rails. Select the module source with power off.", 1515),
    ("U4 PIO host power and cutoff", "Protected U2 branch and U1 feed converge at IC8. IC6 protects the selected supply at J4.", 1090),
    ("Native and PIO USB data", "Actual connector VBUS qualifies each independent data pair. U2 and U4 data stay separate.", 2016),
    ("RP2040-Zero and 3.3 V accessories", "The module rail supplies qualified SD and OLED loads. Keep accessory wiring short and local.", 1655),
)
NEGATIVE_CONTROLS = (
    ("A5", "B5", ("IC1", "9")),
    ("A5", "B5", ("IC6", "9")),
    ("N_DP", "N_DM", ("IC11", "5")),
    ("N_DP", "N_DM", ("M1", "P1.11")),
)


def page_frame(canvas, index, title, intro, height):
    canvas.rect(0, 0, PW, 98, INK, INK)
    canvas.text(38, 36, "GRIDPICO / MINIMUM CARRIER REV A", 16, "#a7cbd1", True)
    canvas.text(38, 75, title, 33, WHITE, True)
    canvas.text(PW - 38, 73, f"Drawing review  /  {index:02} of 04", 18, WHITE, True, "end")
    canvas.text(38, 124, intro, 15, SUB)
    canvas.line(38, 143, PW - 38, 143, EDGE)
    canvas.line(38, height - 68, PW - 38, height - 68, EDGE)
    canvas.text(38, height - 47, "Matching rail and remote labels are electrical joins. GND symbols share GND. X marks a deliberate no-connect.",
                13, SUB)
    canvas.text(38, height - 29, "Blue values open manufacturer documents. * Exact manufacturer or order code open.", 12, SUB)
    canvas.text(38, height - 12, "Published electrical basis dae330c / c664df16. Existing review holds apply.", 12, SUB)


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


def rejected(drawing, mutation):
    try:
        drawing.compare()
    except AssertionError:
        return f"Rejected {mutation}."
    raise AssertionError(f"Accepted {mutation}")


def negative_controls(drawing, old, wrong, terminal):
    lines = []
    indices = [i for i, (_, net, _) in enumerate(drawing.labels) if net == old]
    assert indices, old
    index = indices[0]
    mark = drawing.labels[index]
    drawing.labels[index] = (mark[0], wrong, mark[2])
    try:
        lines.append(rejected(drawing, f"substituted label {wrong} for {old}"))
    finally:
        drawing.labels[index] = mark

    point = drawing.p(*terminal)
    index = next(i for i, segment in enumerate(drawing.segments) if point in segment)
    wire = drawing.segments.pop(index)
    try:
        lines.append(rejected(drawing, f"opened wire at {terminal[0]}.{terminal[1]}"))
    finally:
        drawing.segments.insert(index, wire)

    point = min(drawing.unconnected)
    drawing.unconnected.remove(point)
    try:
        lines.append(rejected(drawing, "deleted deliberate no-connect mark"))
    finally:
        drawing.unconnected.add(point)
    return "\n".join(lines) + "\n"


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
    with (ROOT / "parts-links.csv").open("w", newline="") as stream:
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
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--review-output", type=Path, help="Directory for before/after PDF and audit rasters")
    review = cli.parse_args().review_output
    if review:
        review.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(exist_ok=True)
    CHECK.mkdir(exist_ok=True)
    with (BASELINE / "pin-netlist.csv").open(newline="") as source:
        rows = list(csv.DictReader(source))
    write_parts_index(rows)
    after = fitz.open()
    combined = fitz.open()
    original = fitz.open(BASELINE / PUBLISHED_PDF)
    draw = (sheet_1, sheet_2, sheet_3, sheet_4)
    summaries = []
    overlap_reports = []
    drawings = []
    for index, (layout, (title, intro, height), controls) in enumerate(zip(draw, TITLES, NEGATIVE_CONTROLS, strict=True), 1):
        name = f"sheet-{index:02}"
        canvas = Canvas(after, height)
        canvas.svg[0] = canvas.svg[0].replace("<svg ", '<svg xmlns:xlink="http://www.w3.org/1999/xlink" ', 1)
        page_frame(canvas, index, title, intro, height)
        drawing = Drawing(canvas, rows, index)
        layout(drawing)
        summary = drawing.compare(export=True)
        svg_path = OUT / f"{name}.svg"
        canvas.save(svg_path)
        validate_svg(drawing, svg_path)
        drawings.append(drawing)
        summary += negative_controls(drawing, *controls)
        drawing.compare()
        summaries.append(summary)
        overlaps = check_pdf_text(after[-1])
        if overlaps:
            overlap_reports.append((name, overlaps))
        if review:
            after[-1].get_pixmap(matrix=fitz.Matrix(1.32, 1.32), alpha=False).save(review / f"{name}-after.png")
            original[index - 1].get_pixmap(matrix=fitz.Matrix(1.32, 1.32), alpha=False).save(review / f"{name}-before.png")
            after[-1].get_pixmap(matrix=fitz.Matrix(.92, .92), alpha=False).save(review / f"{name}-review.png")

    after.set_metadata({"title": "Gridpico minimum carrier | Four-sheet drafting review",
                        "author": "Gridpico Hardware Design"})
    after.ez_save(OUT / PUBLISHED_PDF, deflate=True, no_new_id=True)
    with fitz.open(OUT / PUBLISHED_PDF) as rendered:
        for index, (page, drawing) in enumerate(zip(rendered, drawings, strict=True)):
            check_pdf_links(page, drawing)
            combined.insert_pdf(original, from_page=index, to_page=index)
            combined.insert_pdf(rendered, from_page=index, to_page=index)
    if review:
        combined.ez_save(review / "before-after.pdf", deflate=True, no_new_id=True)
        with fitz.open(review / "before-after.pdf") as comparison:
            for index, drawing in enumerate(drawings):
                check_pdf_links(comparison[2 * index + 1], drawing)
    for i in range(4):
        assert original[i].get_pixmap().samples == combined[2 * i].get_pixmap().samples
        assert after[i].get_pixmap().samples == combined[2 * i + 1].get_pixmap().samples

    (CHECK / "drawing-parity.txt").write_text("\n".join(summaries))
    for item in overlap_reports:
        print(item)
    assert not overlap_reports, "PDF text overlaps"
    print(f"PASS {len(DOCUMENTS)} PDF/SVG links and {len(IC_ROLES)} PDF/SVG role captions. Audited link placement and targets.")
    print("PASS SVG wires, junctions, labels, returns and no-connect marks and PDF text audit. All eight comparison pages raster-match their source pages.")


if __name__ == "__main__":
    main()
