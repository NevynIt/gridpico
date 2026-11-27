"""Render a single-page electrical design overview as vector PDF and SVG."""

from render import BASE, Canvas, PW, fitz

DARK = "#132b3b"
TEXT = "#203c4c"
MUTED = "#455b69"
BORDER = "#d8e1e6"
WHITE = "#ffffff"
A = "#0d665f"
B = "#4a478c"
HOST = "#284c71"
HOLD = "#9b4924"


def panel(canvas, x, y, w, h, title, body, tint=TEXT):
    canvas.rect(x, y, w, h, BORDER, WHITE)
    canvas.rect(x, y, 5, h, tint, tint)
    canvas.text(x + 22, y + 37, title, 22, tint, bold=True)
    for index, line in enumerate(body):
        canvas.text(x + 22, y + 69 + 25 * index, line, 16, TEXT)


def main():
    doc = fitz.open()
    canvas = Canvas(doc, 1020)
    canvas.rect(0, 0, PW, 1020, "#f2f5f7", "#f2f5f7")
    canvas.rect(0, 0, PW, 112, DARK, DARK)
    canvas.text(41, 57, "RP2040-Zero minimum carrier", 41, WHITE, bold=True)
    canvas.text(42, 88, "Ordinary DEV design review  /  Brief 8acb1af  /  102 references, down from 180", 18, "#bdd9df")
    canvas.text(PW - 42, 58, "Electrical qualification open", 19, "#ffd1a9", bold=True, align="end")

    canvas.text(41, 159, "Two inputs and independent protected rails", 26, TEXT, bold=True)
    panel(canvas, 41, 180, 738, 121, "U1  /  5 V power in", (
        "Dedicated qualified 5 V adapter. D+ and D- open.",
        "IC1 reverse-blocking breaker feeds A5.",
    ), A)
    panel(canvas, 805, 180, 738, 121, "U2  /  Native USB Device", (
        "Laptop data uses native D+ and D- with actual J2 VBUS sensing.",
        "IC2 reverse-blocking breaker feeds B5.",
    ), B)

    for x, label, color in ((680, "A5", A), (1444, "B5", B)):
        canvas.rect(x - 20, 220, 73, 41, color, color)
        canvas.text(x + 17, 247, label, 19, WHITE, bold=True, align="middle")
    canvas.text(41, 354, "Three loads with distinct power rules", 26, TEXT, bold=True)
    panel(canvas, 41, 374, 482, 186, "U3  /  Power only", (
        "A5 through an independent breaker.",
        "U1 only. USB-A data contacts open.",
        "300 mA proposed accessory envelope.",
    ), A)
    panel(canvas, 551, 374, 482, 186, "U4  /  PIO USB Host", (
        "A5 or current-limited B5 via a source mux.",
        "Final breaker, soft start and reverse blocking.",
        "GP9 enables power and can force data open.",
        "500 mA on A or 200 mA on authorized B.",
    ), HOST)
    panel(canvas, 1061, 374, 482, 186, "Module and 3.3 V loads", (
        "One power-off bridge selects A5 or B5.",
        "A5 populated for the default DEV build.",
        "SD/OLED on the module rail if qualified.",
        "No automatic module supply transfer.",
    ), B)

    canvas.text(41, 611, "Simple connectors and retained protection", 26, TEXT, bold=True)
    panel(canvas, 41, 632, 738, 292, "Four functional signals", (
        "5 V, D-, D+, GND. Jill's exact connector order is pending.",
        "U1 and U2 USB-C breakouts need verified passive CC termination.",
        "U2 data must work in both plug orientations.",
        "U3 and U4 use downstream USB-A.",
        "The module's stock VBUS lands are on VSYS. The separate J2",
        "requires the stock receptacle absent and those lands insulated.",
        "J2 and J4 keep full-speed data gates and local ESD protection.",
    ), HOST)
    panel(canvas, 805, 632, 738, 292, "Conditions for the proposed build", (
        "120 mA total on the module 3.3 V rail is a thermal screening target.",
        "At the illustrative 70 mA base load, about 50 mA remains for SD/OLED.",
        "Exact module, card, OLED and adapter evidence is pending.",
        "A powered cascade still requires eligible U2 module builds and",
        "separate USB current admissions. The installed profile offers 100 mA.",
        "Check backfeed, short, inrush, droop, discharge and USB suspend.",
        "This paper design is held for fabrication and powered qualification.",
    ), HOLD)

    canvas.text(41, 974, "Use the four-sheet schematic and pinout for exact connections and conditional fit.", 15, MUTED)
    canvas.text(PW - 42, 974, "Gridpico Hardware Design", 15, MUTED, align="end")
    canvas.save(BASE / "carrier-overview.svg")
    doc.set_metadata({"title": "Gridpico RP2040-Zero minimum carrier overview",
                      "author": "Gridpico Hardware Design"})
    doc.ez_save(BASE / "carrier-overview.pdf", deflate=True, no_new_id=True)


if __name__ == "__main__":
    main()
