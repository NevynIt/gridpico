"""Drawing primitives and endpoint graph audit for the sealed minimum carrier."""

import csv
from collections import defaultdict
from hashlib import sha256
from itertools import combinations, pairwise
from pathlib import Path
import sys
from xml.sax.saxutils import escape, quoteattr

from parts import DOCUMENTS, IC_ROLES

ROOT = Path(__file__).resolve().parent
BASELINE = ROOT.parent
sys.path.insert(0, str(BASELINE))
from render import Canvas, PW, SCALE, color, fitz  # published PDF/SVG graphics primitives

INK = "#162638"
SUB = "#385066"
WIRE = "#22635f"
RAIL = "#9a4c27"
EDGE = "#cedbe3"
FACE = "#f4f8fb"
GRAY = "#626f79"
LINK = "#075b9a"
WHITE = "#ffffff"


def point_on_segment(point, a, b):
    x, y = point
    return ((a[0] == b[0] == x and min(a[1], b[1]) <= y <= max(a[1], b[1]))
            or (a[1] == b[1] == y and min(a[0], b[0]) <= x <= max(a[0], b[0])))


class Drawing:
    def __init__(self, canvas, rows, sheet):
        self.canvas = canvas
        self.sheet = str(sheet)
        self.expected = {}
        self.values = {}
        for row in rows:
            if row["sheet"] == self.sheet:
                key = (row["reference"], row["pin"])
                assert key not in self.expected
                self.expected[key] = row
                self.values[row["reference"]] = row["value"]
        self.external_nets = {row["net"] for row in rows if row["sheet"] != self.sheet}
        self.pins = {}
        self.segments = []
        self.labels = []
        self.junctions = set()
        self.unconnected = set()
        self.bodies = []
        self.links = {}

    def artwork(self, a, b, color=INK, width=1.5):
        self.canvas.line(*a, *b, color, width)

    def wire(self, *points):
        for a, b in pairwise(points):
            assert a != b and (a[0] == b[0] or a[1] == b[1]), (a, b)
            self.segments.append((a, b))
            self.artwork(a, b, WIRE, 2.0)
            self.canvas.svg[-1] = self.canvas.svg[-1].replace("<line ", "<line data-wire=\"true\" ", 1)

    def pin(self, ref, number, point):
        key = (ref, str(number))
        assert key in self.expected and key not in self.pins, key
        self.pins[key] = point
        return point

    def p(self, ref, number):
        return self.pins[(ref, str(number))]

    def reference(self, ref, x, y, size, align="middle"):
        label = ref if ref in DOCUMENTS else f"{ref}*"
        self.canvas.text(x, y, label, size, INK, True, align)
        self.canvas.svg[-1] = self.canvas.svg[-1].replace("<text ", f'<text data-ref="{ref}" ', 1)

    def component_value(self, ref, x, y, size, align="middle"):
        value = self.values[ref]
        document = DOCUMENTS.get(ref)
        width = self.canvas.text(x, y, value, size, LINK if document else SUB, align=align)
        if not document:
            return
        start = x - width if align == "end" else x - width / 2 if align == "middle" else x
        href = quoteattr(document.url)
        description = escape(f"{document.maker} {document.part} ({document.status.replace('_', ' ')})")
        self.canvas.svg[-1] = self.canvas.svg[-1].replace("<text ", f'<text data-part-value="{ref}" ', 1)
        self.artwork((start, y + 2), (start + width, y + 2), LINK, .85)
        top, height = y - size * 1.03, size * 1.33
        # Keep the placed text visible in SVG renderers that skip active link elements.
        self.canvas.svg.append(
            f'<a data-part="{ref}" aria-label="{description}" href={href} xlink:href={href}>'
            f'<title>{description}</title><rect x="{start}" y="{top}" width="{width}" '
            f'height="{height}" fill="transparent" pointer-events="all" style="cursor:pointer"/></a>'
        )
        rect = fitz.Rect(start * SCALE, top * SCALE, (start + width) * SCALE, (top + height) * SCALE)
        self.canvas.page.insert_link({"kind": fitz.LINK_URI, "from": rect, "uri": document.url})
        assert ref not in self.links, ref
        self.links[ref] = rect

    def symbol_heading(self, ref, x, y):
        if ref in IC_ROLES:
            role = IC_ROLES[ref]
            ref_width = fitz.get_text_length(ref, fontname="hebo", fontsize=21)
            role_width = fitz.get_text_length(role, fontname="hebo", fontsize=14)
            start = x - (ref_width + 9 + role_width) / 2
            self.reference(ref, start, y, 21, "start")
            self.canvas.text(start + ref_width + 9, y, role, 14, INK, True)
            self.canvas.svg[-1] = self.canvas.svg[-1].replace("<text ", f'<text data-role-for="{ref}" ', 1)
        else:
            self.reference(ref, x, y, 21)

    def symbol(self, ref, x, y, w, h, pin_layout, *, title_at=None):
        self.canvas.rect(x, y, w, h, INK, FACE, 1.5)
        self.bodies.append((x, y, x + w, y + h, ref))
        title_x, title_y = title_at or (x + w / 2, y - 27)
        self.symbol_heading(ref, title_x, title_y)
        self.component_value(ref, title_x, title_y + 18, 12)
        for number, side, position in pin_layout:
            number = str(number)
            function = self.expected[(ref, number)]["function"]
            if side in ("left", "right"):
                edge = x if side == "left" else x + w
                step = -1 if side == "left" else 1
                anchor = (edge + 18 * step, position)
                self.artwork((edge, position), anchor)
                display = f"{number}  {function}" if step == -1 else f"{function}  {number}"
                self.canvas.text(edge - 8 * step, position + 4, display, 12, INK,
                                 align="start" if step == -1 else "end")
            else:
                edge = y if side == "top" else y + h
                step = -1 if side == "top" else 1
                anchor = (position, edge + 18 * step)
                self.artwork((position, edge), anchor)
                name_y = edge + 12 if side == "top" else edge - 16
                self.canvas.text(position, name_y, function, 11, INK, align="middle")
                self.canvas.text(position, name_y + 12, number, 10, INK, align="middle")
            self.pin(ref, number, anchor)

    def mos(self, ref, x, y, p_channel):
        """Use compact package symbols with conventional G, S and D sides."""
        self.canvas.rect(x, y, 88, 72, INK, FACE, 1.4)
        self.bodies.append((x, y, x + 88, y + 72, ref))
        self.reference(ref, x + 44, y + 18, 15)
        self.component_value(ref, x + 44, y + 59, 11)
        gate_at = (x - 16, y + 35)
        other_at = (x + 104, y + 35)
        self.artwork(gate_at, (x, y + 35))
        self.artwork((x + 88, y + 35), other_at)
        self.canvas.text(x + 7, y + 39, "1 G", 11)
        self.canvas.text(x + 81, y + 39, "D 3", 11, align="end")
        edge = y if p_channel else y + 72
        source_at = (x + 44, edge - 17 if p_channel else edge + 17)
        self.artwork((x + 44, edge), source_at)
        self.canvas.text(x + 55, edge - 5 if p_channel else edge + 15, "2 S", 10)
        self.pin(ref, 1, gate_at)
        self.pin(ref, 2, source_at)
        self.pin(ref, 3, other_at)

    def passive(self, ref, x, y, *, horizontal=False, side="right", polar=False, reverse=False):
        pos = (lambda u, v: (x + v, y + u)) if horizontal else (lambda u, v: (x + u, y + v))
        terminal1, terminal2 = pos(0, -24), pos(0, 24)
        self.pin(ref, 2 if reverse else 1, terminal1)
        self.pin(ref, 1 if reverse else 2, terminal2)
        if ref.startswith("C"):
            self.artwork(terminal1, pos(0, -4))
            self.artwork(pos(0, 4), terminal2)
            for v in (-4, 4):
                self.artwork(pos(-10, v), pos(10, v), INK, 2)
            if polar:
                self.canvas.text(x - 17, y - 12, "+", 13, INK)
        elif ref.startswith("D"):
            self.artwork(terminal1, pos(0, -11))
            self.artwork(pos(0, 11), terminal2)
            self.canvas.rect(x - 19, y - 11, 38, 22, INK, FACE, 1.5)
            self.canvas.text(x, y + 4, "TVS", 10, INK, True, "middle")
            self.canvas.text(x + 7, y - 14, "1", 9, SUB)
            self.canvas.text(x + 7, y + 21, "2", 9, SUB)
        else:
            self.artwork(terminal1, pos(0, -13))
            self.artwork(pos(0, 13), terminal2)
            corners = [pos(-6, -13), pos(6, -13), pos(6, 13), pos(-6, 13), pos(-6, -13)]
            self.canvas.poly(corners, INK, 1.55)
        if horizontal:
            self.reference(ref, x, y - 21, 14)
            self.component_value(ref, x, y + 20, 11.5)
        elif side == "below":
            self.reference(ref, x, y + 67, 14)
            self.component_value(ref, x, y + 84, 11.5)
        else:
            step = 1 if side == "right" else -1
            align = "start" if step == 1 else "end"
            self.reference(ref, x + 14 * step, y - 4, 14, align)
            self.component_value(ref, x + 14 * step, y + 13, 11.5, align)

    def label(self, net, point, align="start"):
        x, y = point
        self.labels.append((point, net, "label"))
        self.canvas.text(x, y - 8, net, 12, RAIL if net in RAILS else WIRE,
                         True, align)

    def supply(self, net, point):
        x, y = point
        self.labels.append((point, net, "power"))
        self.artwork((x, y), (x, y - 14), RAIL, 1.8)
        self.artwork((x - 8, y - 14), (x + 8, y - 14), RAIL, 1.8)
        self.canvas.text(x, y - 20, net, 13, RAIL, True, "middle")

    def ground(self, point):
        x, y = point
        self.labels.append((point, "GND", "ground"))
        self.artwork((x, y), (x, y + 10), WIRE, 1.8)
        self.canvas.svg[-1] = self.canvas.svg[-1].replace("<line ", '<line data-ground="true" ', 1)
        for offset, width in ((10, 10), (14, 7), (18, 3)):
            self.artwork((x - width, y + offset), (x + width, y + offset), WIRE, 1.5)

    def dot(self, point):
        self.junctions.add(point)
        x, y = point
        self.canvas.page.draw_circle((x * SCALE, y * SCALE), 2.5 * SCALE,
                                     color=color(WIRE), fill=color(WIRE))
        self.canvas.svg.append(f'<circle cx="{x}" cy="{y}" r="2.5" fill="{WIRE}"/>')

    def nc(self, point):
        self.unconnected.add(point)
        self.canvas.nc(*point)
        for index in (-2, -1):
            mark = f'<line data-nc-x="{point[0]}" data-nc-y="{point[1]}" '
            self.canvas.svg[index] = self.canvas.svg[index].replace("<line ", mark, 1)

    def compare(self, export=False):
        assert self.pins.keys() == self.expected.keys()
        assert len(self.pins) == len(set(self.pins.values()))
        nodes = {point for segment in self.segments for point in segment}
        nodes |= set(self.pins.values()) | self.junctions | {point for point, _, _ in self.labels}
        links = {point: set() for point in nodes}

        for a, b in self.segments:
            for x1, y1, x2, y2, ref in self.bodies:
                if a[0] == b[0]:
                    crosses = x1 < a[0] < x2 and max(min(a[1], b[1]), y1) < min(max(a[1], b[1]), y2)
                else:
                    crosses = y1 < a[1] < y2 and max(min(a[0], b[0]), x1) < min(max(a[0], b[0]), x2)
                assert not crosses, (a, b, ref)
        for first, second in combinations(self.segments, 2):
            a, b = first
            c, d = second
            if a[0] == b[0] and c[1] == d[1]:
                cross = (a[0], c[1])
            elif c[0] == d[0] and a[1] == b[1]:
                cross = (c[0], a[1])
            else:
                constant_axis = 0 if a[0] == b[0] else 1
                varying_axis = 1 - constant_axis
                if a[constant_axis] == c[constant_axis]:
                    a_range = sorted((a[varying_axis], b[varying_axis]))
                    b_range = sorted((c[varying_axis], d[varying_axis]))
                    assert max(a_range[0], b_range[0]) >= min(a_range[1], b_range[1]), (first, second)
                continue
            if point_on_segment(cross, a, b) and point_on_segment(cross, c, d):
                assert cross in self.junctions or cross in (set(first) & set(second)), (cross, first, second)

        for a, b in self.segments:
            swept = sorted(point for point in nodes if point_on_segment(point, a, b))
            for u, v in pairwise(swept):
                links[u].add(v)
                links[v].add(u)
        assert {point for point, conn in links.items() if len(conn) >= 3} == self.junctions
        for key, point in self.pins.items():
            if self.expected[key]["net"] == "NC":
                assert point in self.unconnected and not links[point], key
            else:
                assert point not in self.unconnected and links[point], key
        assert {self.pins[key] for key in self.expected if self.expected[key]["net"] == "NC"} == self.unconnected

        named = defaultdict(list)
        for point, net, _ in self.labels:
            assert links[point] and point not in self.unconnected, (point, net)
            named[net].append(point)
        for points in named.values():
            for u, v in pairwise(points):
                links[u].add(v)
                links[v].add(u)

        parts = {point: key for key, point in self.pins.items()}
        point_names = defaultdict(set)
        for point, net, _ in self.labels:
            point_names[point].add(net)
        expected_members = defaultdict(set)
        for key, row in self.expected.items():
            if row["net"] != "NC":
                expected_members[row["net"]].add(key)

        after, local_nets, seen = {}, [], set()
        for point in sorted(nodes):
            if point in seen:
                continue
            group = {point}
            pending = [point]
            while pending:
                for next_point in links[pending.pop()] - group:
                    group.add(next_point)
                    pending.append(next_point)
            seen |= group
            group_pins = {parts[q] for q in group if q in parts}
            assert group_pins, ("wire or label without part", group)
            baseline_names = {self.expected[key]["net"] for key in group_pins}
            assert len(baseline_names) == 1, (baseline_names, group_pins)
            net = baseline_names.pop()
            actual_names = {name for q in group for name in point_names[q]}
            if net == "NC":
                assert len(group) == len(group_pins) == 1 and not actual_names
            else:
                assert actual_names in (set(), {net}), (net, actual_names)
                assert group_pins == expected_members[net], (net, group_pins, expected_members[net])
                if not actual_names:
                    assert net not in self.external_nets, ("missing off-sheet name", net)
                    local_nets.append(net)
            for key in group_pins:
                after[key] = dict(self.expected[key], value=self.values[key[0]], net=net)

        if not export:
            return
        fieldnames = ["sheet", "reference", "value", "pin", "function", "net"]
        for stem, ledger in (("before-connectivity", self.expected), ("after-connectivity", after)):
            with (ROOT / "verification" / (f"sheet-{self.sheet}-" + stem + ".csv")).open("w", newline="") as file:
                writer = csv.DictWriter(file, fieldnames=fieldnames, lineterminator="\n")
                writer.writeheader()
                writer.writerows(ledger[key] for key in sorted(ledger))
        before_bytes = (ROOT / "verification" / f"sheet-{self.sheet}-before-connectivity.csv").read_bytes()
        assert before_bytes == (ROOT / "verification" / f"sheet-{self.sheet}-after-connectivity.csv").read_bytes()
        result = (
            f"PASS exact sheet {self.sheet} connectivity comparison\n"
            f"{len(self.values)} component references; {len(after)} source pin groups; "
            f"{len(expected_members)} named nets; {len(self.unconnected)} intentional no-connects\n"
            f"{len(self.segments)} orthogonal wire segments; {len(self.junctions)} marked junctions\n"
            "Unlabeled local nets traced by continuous wires: " + ", ".join(sorted(local_nets)) + "\n"
            "Matching normalized ledger SHA-256: " + sha256(before_bytes).hexdigest() + "\n"
            "Verified terminal/net membership and drawing geometry only. No electrical qualification or native EDA ERC.\n"
        )
        return result


RAILS = {"A5", "B5", "B_HOST", "HOST_PRE", "U4_VBUS", "3V3_AON",
         "U1_RAW", "U2_RAW", "U3_VBUS", "SYS5"}


def draw_power(d):
    # Keep the complete source-to-outlet path on one horizontal row.
    d.symbol("IC7", 174, 230, 146, 185, (
        (1, "left", 270), (3, "left", 365), (6, "right", 270), (4, "right", 365),
        (2, "bottom", 211), (5, "bottom", 279),
    ))
    d.symbol("IC8", 514, 230, 154, 226, (
        (3, "left", 270), (6, "left", 310), (4, "left", 414), (5, "right", 326),
        (1, "bottom", 553), ("2,7", "right", 270), (8, "right", 365),
    ))
    d.symbol("IC6", 855, 230, 215, 198, (
        (5, "left", 270), (1, "left", 375), (6, "right", 270),
        (3, "right", 350), (10, "right", 379), (2, "bottom", 876),
        (8, "bottom", 914), (4, "bottom", 952), (9, "bottom", 996), (7, "bottom", 1046),
    ))
    d.symbol("J4", 1387, 230, 95, 184, (
        (1, "left", 270), (2, "right", 328), (3, "right", 365),
        (4, "bottom", 1410), ("S", "bottom", 1459),
    ))

    d.passive("C72", 86, 326)
    d.wire((86, 242), (86, 270), d.p("IC7", 1))
    d.supply("B5", (86, 242))
    d.wire((86, 270), d.p("C72", 1))
    d.dot((86, 270))
    d.wire(d.p("C72", 2), (86, 360))
    d.ground((86, 360))
    d.wire((103, 396), (134, 396), (134, 365), d.p("IC7", 3))
    d.label("EN7", (103, 396))
    d.wire(d.p("IC7", 2), (211, 474))
    d.ground((211, 474))
    d.passive("R75", 279, 487)
    d.wire(d.p("IC7", 5), d.p("R75", 1))
    d.wire(d.p("R75", 2), (279, 525))
    d.ground((279, 525))

    d.wire(d.p("IC7", 6), (369, 270), d.p("IC8", 3))
    d.passive("C71", 369, 309)
    d.wire((369, 270), d.p("C71", 1))
    d.dot((369, 270))
    d.wire(d.p("C71", 2), (369, 341))
    d.ground((369, 341))
    d.passive("R76", 399, 436, reverse=True)
    d.wire(d.p("IC7", 4), (399, 365), (493, 365))
    d.label("U4_B_FAULT_N", (493, 365), "end")
    d.wire((399, 365), d.p("R76", 2))
    d.dot((399, 365))
    d.wire(d.p("R76", 1), (399, 485), (470, 485), (470, 473))
    d.supply("3V3_AON", (470, 473))

    d.wire((434, 310), d.p("IC8", 6))
    d.label("A5", (434, 310))
    d.wire((460, 414), d.p("IC8", 4))
    d.label("U4_B_GRANT", d.p("IC8", 4), "end")
    d.wire(d.p("IC8", 5), (710, 326), (710, 312))
    d.supply("3V3_AON", (710, 312))
    d.wire(d.p("IC8", 1), (553, 478))
    d.ground((553, 478))
    d.nc(d.p("IC8", 8))

    d.wire(d.p("IC8", "2,7"), (752, 270), d.p("IC6", 5))
    d.label("HOST_PRE", (752, 270), "middle")
    d.passive("C81", 752, 323)
    d.wire((752, 270), d.p("C81", 1))
    d.dot((752, 270))
    d.wire(d.p("C81", 2), (752, 360))
    d.ground((752, 360))
    d.wire((775, 395), (810, 395), (810, 375), d.p("IC6", 1))
    d.label("EN6", (775, 395))
    d.nc(d.p("IC6", 3))
    d.nc(d.p("IC6", 10))
    for pin, x in ((2, 876), (8, 914), (4, 952)):
        d.wire(d.p("IC6", pin), (x, 478))
    d.wire((876, 478), (914, 478), (952, 478))
    d.dot((914, 478))
    d.wire((914, 478), (914, 504))
    d.ground((914, 504))

    d.passive("R61", 996, 513, side="left")
    d.wire(d.p("IC6", 9), d.p("R61", 1))
    d.wire(d.p("R61", 2), (996, 554))
    d.ground((996, 554))
    d.passive("R67", 1046, 513)
    d.passive("C61", 1046, 586)
    d.wire(d.p("IC6", 7), d.p("R67", 1))
    d.wire(d.p("R67", 2), d.p("C61", 1))
    d.wire(d.p("C61", 2), (1046, 625))
    d.ground((1046, 625))

    d.wire(d.p("IC6", 6), (1148, 270), (1253, 270), (1317, 270), d.p("J4", 1))
    d.label("U4_VBUS", (1253, 270), "middle")
    for ref, x, ground_y in (("C62", 1148, 376), ("C63", 1253, 376), ("R66", 1317, 376)):
        d.passive(ref, x, 346, side="below", polar=ref == "C62")
        d.wire((x, 270), d.p(ref, 1))
        d.dot((x, 270))
        d.wire(d.p(ref, 2), (x, ground_y))
        d.ground((x, ground_y))
    for pin, net in ((2, "H_DM"), (3, "H_DP")):
        _, y = d.p("J4", pin)
        d.wire(d.p("J4", pin), (1544, y))
        d.label(net, (1544, y), "end")
    d.wire(d.p("J4", 4), (1410, 451), (1459, 451), d.p("J4", "S"))
    d.wire((1410, 451), (1410, 483))
    d.dot((1410, 451))
    d.ground((1410, 483))


def draw_gate(d, shift, ref, supply, input_signal, output):
    def point(x, y):
        return x + shift, y

    def resistor(number, x, y, **options):
        d.passive(f"R{ref}{number}", *point(x, y), **options)

    def res_pin(number, pin):
        return d.p(f"R{ref}{number}", pin)

    n, p = f"Q{ref}A", f"Q{ref}B"
    d.mos(n, *point(225, 828), False)
    d.mos(p, *point(442, 828), True)
    resistor(1, 140, 913)
    resistor(2, 377, 817)
    resistor(3, 589, 863, horizontal=True)
    resistor(4, 655, 921)

    d.wire(point(67, 863), point(140, 863), d.p(n, 1))
    d.label(input_signal, point(67, 863))
    d.wire(point(140, 863), res_pin(1, 1))
    d.dot(point(140, 863))
    d.wire(res_pin(1, 2), point(140, 962))
    d.ground(point(140, 962))
    d.wire(d.p(n, 2), point(269, 962))
    d.ground(point(269, 962))

    d.wire(d.p(n, 3), point(377, 863), d.p(p, 1))
    d.wire(res_pin(2, 2), point(377, 863))
    d.dot(point(377, 863))
    d.wire(res_pin(2, 1), point(377, 768), point(486, 768), d.p(p, 2))
    d.wire(point(486, 768), point(486, 753))
    d.dot(point(486, 768))
    d.supply(supply, point(486, 753))

    d.wire(d.p(p, 3), res_pin(3, 1))
    d.wire(res_pin(3, 2), point(655, 863), point(744, 863))
    d.label(output, point(744, 863), "end")
    d.wire(point(655, 863), res_pin(4, 1))
    d.dot(point(655, 863))
    d.wire(res_pin(4, 2), point(655, 974))
    d.ground(point(655, 974))
