"""Render the one-page minimum carrier comparison."""

from render import BASE, Canvas, PW, fitz

DARK = "#132b3b"
INK = "#1a3445"
MUTED = "#446071"
LINE = "#cedce3"
WHITE = "#ffffff"
ARCHIVE = "#5a6c80"
HELD = "#935232"
CURRENT = "#086660"


def text_lines(canvas, x, y, lines, color=INK, fontsize=15, spacing=21):
    for i, line in enumerate(lines):
        canvas.text(x, y + i * spacing, line, fontsize, color)


def main():
    doc = fitz.open()
    canvas = Canvas(doc, 1020)
    canvas.rect(0, 0, PW, 1020, "#f3f6f8", "#f3f6f8")
    canvas.rect(0, 0, PW, 112, DARK, DARK)
    canvas.text(42, 57, "RP2040-Zero carrier comparison", 42, WHITE, bold=True)
    canvas.text(43, 88, "Historical draft, held dual-source Rev B and Jill's minimum Rev A", 18, "#c7e0e8")
    canvas.text(PW - 42, 58, "Paper review", 19, "#ffd1a9", bold=True, align="end")

    headings = (
        ("Historical draft", "Published 7d5fb9b  /  Manifest 25dbfc04", "180 references  /  22 ICs", ARCHIVE),
        ("Held dual-source Rev B", "Manifest feda1dac", "183 references  /  23 ICs", HELD),
        ("Jill's minimum Rev A", "Published brief 8acb1af  /  dff3f7be", "102 references  /  11 ICs", CURRENT),
    )
    starts = (254, 691, 1128)
    for x, (title, pin, count, color) in zip(starts, headings):
        canvas.rect(x, 139, 415, 107, LINE, WHITE)
        canvas.rect(x, 139, 415, 5, color, color)
        canvas.text(x + 16, 174, title, 23, color, bold=True)
        canvas.text(x + 16, 197, pin, 14, MUTED)
        canvas.text(x + 16, 225, count, 20, INK, bold=True)

    rows = (
        ("Inputs and", "module supply",
         ("Independent USB-C sensing and breakers.", "Automatic module source mux."),
         ("Separate protected inputs and automatic", "SYS5 source transfer.", "U1-loss / U2 suspend hold remains."),
         ("Passive CC when the breakout qualifies.", "Independent input breakers. Power-off", "A5/B5 bridge, U1 default.")),
        ("U3 / U4", "outlet power",
         ("U3 fed by U1 only.", "U4 has separately granted A/B branches,", "a source mux and final breaker."),
         ("Same protected paths. U4 adds slew", "resistor and revised passive discharge", "screen. C62 selection open."),
         ("U3 from U1 only. U4 selects A5 directly", "or granted current-limited B via one mux", "and one final protected outlet.")),
        ("U2 / U4", "data protection",
         ("Separate VBUS supervisors and switches.", "Loaded U4 detector has an electrical hold."),
         ("Independent VBUS sense and hard U4 veto.", "GP21 reports the powered IC7 fault.", "Powered qualification open."),
         ("Carries VBUS-qualified gates, GP9 veto", "and GP21. Two USB ESD arrays replace", "four discrete data clamps.")),
        ("SD / OLED", "and 3.3 V",
         ("Separate switched regulator and", "peripheral rail. Per-source grants."),
         ("Retains switched, buffered peripheral rail.", "Actual module and accessory parts open."),
         ("Direct module 3.3 V rail on qualified builds.", "120 mA provisional total screen, including", "the Pico, pixel and accessories.")),
    )
    for n, (label, detail, *cols) in enumerate(rows):
        y = 266 + n * 119
        canvas.rect(41, y, 1502, 109, LINE, WHITE)
        canvas.text(57, y + 38, label, 21, INK, bold=True)
        canvas.text(57, y + 65, detail, 18, MUTED)
        for x, copy in zip(starts, cols):
            canvas.line(x - 1, y + 13, x - 1, y + 96, LINE, 1.2)
            text_lines(canvas, x + 15, y + 32, copy)

    canvas.rect(41, 757, 1502, 110, LINE, WHITE)
    canvas.text(58, 790, "U1-only U4 option", 21, INK, bold=True)
    text_lines(canvas, 58, 820, (
        "Dropping the U4 B branch and selector simplifies host power and preserves a protected U1 outlet.",
        "It gives up U2-powered U4 and the single-supply powered cascade. Routed MIDI hops can use local host supplies.",
    ))
    canvas.rect(41, 881, 1502, 109, LINE, WHITE)
    canvas.text(58, 914, "Open before pinout lock or powered qualification", 20, HELD, bold=True)
    text_lines(canvas, 58, 943, (
        "Exact module and native lands, supplied connector type and pin order, USB-C Rd and both orientations, and a qualified U1 supply.",
        "Measure SD/OLED startup and rail margin, J2 current/suspend, and outlet backfeed, short protection, inrush, droop and discharge.",
    ))
    canvas.save(BASE / "carrier-comparison.svg")
    doc.set_metadata({"title": "Gridpico carrier comparison | Minimum Rev A",
                      "author": "Gridpico Hardware Design"})
    doc.ez_save(BASE / "carrier-comparison.pdf", deflate=True, no_new_id=True)


if __name__ == "__main__":
    main()
