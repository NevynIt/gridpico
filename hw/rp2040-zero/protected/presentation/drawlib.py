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
ELECTRICAL = ROOT.parent / "electrical"
BASELINE = ELECTRICAL / "baseline"
sys.path.insert(0, str(BASELINE))
from render import Canvas, PW, SCALE, color, fitz  # published PDF/SVG graphics primitives

INK = "#162638"
SUB = "#385066"
WIRE = "#22635f"
RAIL = "#9a4c27"
EDGE = "#cedbe3"
FACE = "#f4f8fb"
WHITE = "#ffffff"
LINK = "#075b9a"


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
            self.artwork(terminal1, (x, y - 9))
            self.artwork((x, y + 9), terminal2)
            self.canvas.poly([(x - 10, y + 9), (x, y - 9), (x + 10, y + 9),
                              (x - 10, y + 9)], INK, 1.55)
            self.canvas.poly([(x - 13, y - 5), (x - 9, y - 9), (x + 9, y - 9),
                              (x + 13, y - 13)], INK, 1.55)
            self.canvas.text(x + 14, y - 9, "1 K", 10, SUB)
            self.canvas.text(x + 14, y + 16, "2 A", 10, SUB)
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
         "U1_RAW", "U2_RAW", "U3_VBUS", "SYS5", "HOST5", "B_U4"}
