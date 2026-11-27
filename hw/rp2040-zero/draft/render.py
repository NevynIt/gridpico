"""Render the editable net-labeled sheets to independent SVGs and vector PDF."""
from collections import Counter, defaultdict
from pathlib import Path
from xml.sax.saxutils import escape
import csv
import sys
import textwrap

from board import sheets

BASE = Path(__file__).resolve().parent
sys.path.insert(0, str(BASE / "tools"))
import pymupdf as fitz

INK = "#162638"
SUB = "#385066"
WIRE = "#225e67"
POWER = "#9e3d14"
GREY = "#5a6570"
FAINT = "#d6e0e9"
BLUE = "#eaf1f7"
PAPER = "#ffffff"
FACE = "#f5f9fc"
PW = 1584
SCALE = 0.65

def color(s):
    return tuple(int(s[i:i + 2], 16) / 255 for i in (1, 3, 5))

def power(name):
    return any(k in name for k in ("VBUS", "_RAW", "3V3", "SYS5", "A5", "B5", "_HOST", "HOST_PRE", "_PER", "PER_PRE"))

class Canvas:
    def __init__(self, doc, height):
        self.height = height
        self.page = doc.new_page(width=PW*SCALE, height=height*SCALE)
        self.svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{PW}" height="{height}" viewBox="0 0 {PW} {height}" role="img">']
        self.rect(0, 0, PW, height, PAPER, PAPER)

    def text(self, x, y, word, size=14, ink=INK, bold=False, align="start", mono=False):
        font = "cour" if mono else "hebo" if bold else "helv"
        familia = "monospace" if mono else "Arial,Helvetica,sans-serif"
        width = fitz.get_text_length(word, fontname=font, fontsize=size)
        x_pdf = x - width if align == "end" else x - width / 2 if align == "middle" else x
        self.page.insert_text((x_pdf*SCALE, y*SCALE), word, fontname=font, fontsize=size*SCALE, color=color(ink))
        self.svg.append(f'<text x="{x}" y="{y}" fill="{ink}" font-family="{familia}" font-size="{size}" '
                        f'text-anchor="{align}" font-weight="{"700" if bold else "400"}">{escape(word)}</text>')
        return width

    def rect(self, x, y, w, h, stroke, fill, width=1):
        self.page.draw_rect(fitz.Rect(x*SCALE, y*SCALE, (x+w)*SCALE, (y+h)*SCALE),
                            fill=color(fill), color=color(stroke), width=width*SCALE)
        self.svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" stroke="{stroke}" stroke-width="{width}" fill="{fill}"/>')

    def line(self, x, y, xx, yy, stroke=WIRE, width=1.5):
        self.page.draw_line((x*SCALE,y*SCALE), (xx*SCALE, yy*SCALE), color=color(stroke), width=width*SCALE)
        self.svg.append(f'<line x1="{x}" y1="{y}" x2="{xx}" y2="{yy}" stroke="{stroke}" stroke-width="{width}"/>')

    def poly(self, points, stroke=INK, width=1.5):
        self.page.draw_polyline([(x*SCALE,y*SCALE) for x,y in points], color=color(stroke), width=width*SCALE)
        self.svg.append('<polyline points="'+' '.join(f'{x},{y}' for x,y in points)+f'" fill="none" stroke="{stroke}" stroke-width="{width}"/>')

    def nc(self, x, y):
        self.line(x-3, y-3, x+3, y+3, GREY, 1.1)
        self.line(x-3, y+3, x+3, y-3, GREY, 1.1)

    def sheet(self, sheet, number, total):
        self.rect(0,0,PW,98,INK,INK)
        self.text(38,36,"GRIDPICO / RP2040-ZERO CARRIER",16,"#a7cbd1",True)
        self.text(38,77,sheet.title,33,PAPER,True)
        self.text(PW-38,74,f"{number:02d} / {total:02d}",23,PAPER,True,"end")
        self.text(38,126-10,sheet.subtitle,14,SUB)
        for panel in sheet.cells:
            self.rect(panel.x,panel.y,panel.w,panel.h,FAINT,PAPER,1.4)
            self.rect(panel.x,panel.y,panel.w,36,FAINT,BLUE,1.1)
            self.text(panel.x+15,panel.y+24,panel.title,16,INK,True)
            for x,y,left,part in panel.placed:
                if part.kind == "ic":
                    self.part(panel.x+x,panel.y+y,left,part)
                else:
                    self.passive(panel.x+x,panel.y+y,part)
            lines = []
            for note in panel.notes:
                lines.extend(textwrap.wrap(note, width=(panel.w-35)//7, break_long_words=False))
            if lines:
                start = panel.y+panel.h-15-(len(lines)-1)*18 if panel.placed else panel.y+62
                for i,note in enumerate(lines):
                    self.text(panel.x+15,start+i*18,note,13,SUB)
        self.line(38,self.height-38,PW-38,self.height-38,FAINT)
        self.text(38,self.height-17,"Electrical design review   /   Named nets are joined across sheets   /   x = intentional NC",12,SUB)
        self.text(PW-38,self.height-17,"SCRATCH 0.1   /   2026-11-27   /   Layout and qualification pending",12,SUB,False,"end")

    def part(self,x,y,left,part):
        leftpins, rightpins = part.pins[:left], part.pins[left:]
        def max_label(seq):
            return max((fitz.get_text_length(f"{num}  {func}",fontname="helv",fontsize=13)
                        for num,func,_ in seq),default=0)
        w = max(154, max_label(leftpins) + max_label(rightpins) + 52,
                fitz.get_text_length(part.value,fontname="helv",fontsize=13)+30)
        h = 48 + max(len(leftpins), len(rightpins))*23
        if part.ref == "M1":
            w = max(w, 455)
        l, r = x-w/2, x+w/2
        self.rect(l,y,w,h,INK,FACE,1.6)
        self.text(x,y+18,part.ref,15,INK,True,"middle")
        self.text(x,y+34,part.value,12,SUB,False,"middle")
        self.line(l,y+41,r,y+41,FAINT,1)
        for sign, labels, edge in [(-1,leftpins,l),(1,rightpins,r)]:
            for i,(num,name,net) in enumerate(labels):
                yy = y+52+i*23
                desc = f"{num}  {name}" if sign<0 else f"{name}  {num}"
                self.text(edge - sign*7,yy+4,desc,13,INK,False,"start" if sign<0 else "end")
                col = POWER if net and power(net) else WIRE
                self.line(edge,yy,edge+sign*25,yy,col)
                if net:
                    self.text(edge+sign*29,yy+4,net,12,col,False,"end" if sign<0 else "start",True)
                else:
                    self.nc(edge+sign*25,yy)

    def passive(self,x,y,part):
        a, b = (pin[2] for pin in part.pins)
        self.text(x,y-12,f"{part.ref}  {part.value}",12,INK,False,"middle")
        if part.kind == "d":
            self.poly([(x-9,y+8),(x-10,y-8),(x-6,y-8)],INK)
            self.poly([(x+9,y-8),(x+9,y+8),(x-9,y),(x+9,y-8)],INK)
            gap=9
        elif part.kind == "c":
            self.line(x-4,y-8,x-4,y+8,INK)
            self.line(x+4,y-8,x+4,y+8,INK)
            gap=4
        else:
            self.poly([(x-22,y),(x-17,y-6),(x-11,y+6),(x-5,y-6),(x+1,y+6),
                       (x+7,y-6),(x+13,y+6),(x+19,y-6),(x+22,y)],INK)
            gap=22
        for net, sign in [(a,-1),(b,1)]:
            col = POWER if power(net) else WIRE
            self.line(x+gap*sign,y,x+45*sign,y,col)
            self.text(x+49*sign,y+4,net,12,col,False,"start" if sign>0 else "end",True)

    def save(self, dst):
        dst.write_text("\n".join(self.svg+["</svg>",""]),encoding="utf-8")

def export_netlist(dst):
    refs = set()
    nets = defaultdict(list)
    nc = []
    with (dst/"pin-netlist.csv").open("w",newline="",encoding="utf-8") as file:
        writer = csv.writer(file)
        writer.writerow(("sheet","reference","value","pin","function","net"))
        for index,sheet in enumerate(sheets,1):
            for panel in sheet.cells:
                for _,_,_,part in panel.placed:
                    assert part.ref not in refs, f"duplicate reference {part.ref}"
                    refs.add(part.ref)
                    seen = set()
                    for number,description,net in part.pins:
                        assert number not in seen
                        seen.add(number)
                        writer.writerow((index,part.ref,part.value,number,description,net or "NC"))
                        (nets[net] if net else nc).append(f"{part.ref}.{number}")
    with (dst/"pin-netlist.csv").open(newline="",encoding="utf-8") as source, \
         (dst/"pinout.csv").open("w",newline="",encoding="utf-8") as file:
        reader = csv.DictReader(source)
        writer = csv.DictWriter(file, fieldnames=reader.fieldnames)
        writer.writeheader()
        writer.writerows(row for row in reader if row["reference"].startswith("J") or row["reference"] == "M1")
    assert all(len(nodes)>1 for nodes in nets.values()), [k for k,v in nets.items() if len(v)<2]
    power_routes = {
        "U1_RAW": {"J1.A4,A9,B4,B9","IC1.5"},
        "U2_RAW": {"J2.A4,A9,B4,B9","IC2.5"},
        "A5": {"IC1.6","IC3.3","IC4.5","IC5.5","IC23.5"},
        "B5": {"IC2.6","IC3.6","IC7.1","IC24.5"},
        "SYS5": {"IC3.2,7","M1.P1.23","M1.T-C.VBUS"},
        "A_HOST": {"IC5.6","IC8.3"},
        "B_HOST": {"IC7.6","IC8.6"},
        "HOST_PRE": {"IC8.2,7","IC6.5"},
        "U3_VBUS": {"J3.1","IC4.6"},
        "U4_VBUS": {"J4.1","IC6.6"},
        "PER_PRE": {"IC25.2,7","IC14.1"},
    }
    for net, members in power_routes.items():
        routing = {object for object in nets[net] if object.startswith(("J","IC","M1."))}
        # Mode is an input-local strap on each mux.
        if net == "A5": routing.discard("IC3.5")
        if net == "A_HOST": routing.discard("IC8.5")
        assert routing == members, f"{net}: unexpected endpoint(s) {routing ^ members}"
    assert {"J3.2","J3.3","J1.A6,B6","J1.A7,B7","M1.T-C.CC1,CC2"}.issubset(nc)
    (dst/"design-check.txt").write_text(
        f"{len(sheets)} sheets | {len(refs)} references | {len(nets)} nets | {len(nc)} intentional NC groups\n"
        "PASS unique reference/pin, no one-ended named nets, exact power rail terminal sets, U1/U3 data NC\n"
        "Connectivity checks only. These checks do not evaluate electrical behavior or compliance.\n",
        encoding="utf-8")
    with (dst/"nets.txt").open("w",encoding="utf-8") as f:
        for net, nodes in sorted(nets.items()):
            f.write(f"{net:<16} {'  '.join(nodes)}\n")

def main():
    dst = BASE / "drawings"
    dst.mkdir(exist_ok=True)
    export_netlist(BASE)
    doc = fitz.open()
    for index,sheet in enumerate(sheets,1):
        height=max(1033, max(p.y+p.h for p in sheet.cells)+63)
        canvas = Canvas(doc,height)
        canvas.sheet(sheet,index,len(sheets))
        canvas.save(dst/f"sheet-{index:02}.svg")
    doc.set_metadata({"title":"Gridpico RP2040-Zero carrier | Electrical design review",
                      "author":"Gridpico Application Assembly"})
    doc.ez_save(BASE/"Gridpico-RP2040-Zero-Schematic.pdf",deflate=True,no_new_id=True)

if __name__=="__main__":
    main()
