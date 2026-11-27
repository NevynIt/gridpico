"""Presentation coordinates. All values and pin functions are read from the sealed ledger."""

from drawlib import INK, SUB, EDGE


def break_line(d, y, label):
    d.canvas.line(38, y, 1546, y, EDGE)
    d.canvas.text(45, y + 33, label, 19, INK, True)


def note(d, x, y, *lines):
    for index, line in enumerate(lines):
        d.canvas.text(x, y + 21 * index, line, 14, SUB)


def stub(d, ref, pin, net, x, align=None):
    point = d.p(ref, pin)
    end = (x, point[1])
    d.wire(point, end)
    d.label(net, end, align or ("start" if x < point[0] else "end"))


def gnd_down(d, ref, pin, y):
    point = d.p(ref, pin)
    end = (point[0], y)
    d.wire(point, end)
    d.ground(end)


def rail_up(d, ref, pin, net, y):
    point = d.p(ref, pin)
    end = (point[0], y)
    d.wire(point, end)
    d.supply(net, end)


def shunt(d, ref, x, center, trunk, **options):
    d.passive(ref, x, center, **options)
    d.wire((x, trunk), d.p(ref, 1))
    d.dot((x, trunk))
    gnd_down(d, ref, 2, center + 39)


def breaker(d, number, y, vin, vout, input_branch, output_branch):
    ref = f"IC{number}"
    d.symbol(ref, 618, y + 36, 208, 165, (
        (5, "left", y + 72), (1, "left", y + 129),
        (2, "left", y + 164), (6, "right", y + 72),
        (3, "right", y + 128), (10, "right", y + 152),
        (8, "bottom", 651), (4, "bottom", 693),
        (9, "bottom", 749), (7, "bottom", 795),
    ))
    trunk = y + 72
    input_points = [(x, trunk) for _, x in input_branch]
    start = (232, trunk)
    if number == 1:
        d.symbol("J1", 42, y + 37, 140, 157, (
            (1, "right", trunk), (2, "right", y + 108),
            (3, "right", y + 144), (4, "bottom", 112),
        ))
        start = d.p("J1", 1)
        d.nc(d.p("J1", 2))
        d.nc(d.p("J1", 3))
        gnd_down(d, "J1", 4, y + 239)
    d.wire(start, *input_points, (477, trunk), d.p(ref, 5))
    if number == 1:
        d.label(vin, (477, trunk), "end")
    else:
        d.label(vin, start)
    for part, x in input_branch:
        shunt(d, part, x, y + 123, trunk, side="below" if part == "D10" else "right")

    if number in (1, 2):
        high, middle, low = (f"R{number}{i}" for i in (2, 3, 6))
        for part, cy in ((high, 100), (middle, 174), (low, 269)):
            d.passive(part, 477, y + cy, side="left")
        uv, ov = (477, y + 129), (477, y + 221)
        d.wire((477, trunk), d.p(high, 1))
        d.wire(d.p(high, 2), uv, d.p(middle, 1))
        d.wire(uv, d.p(ref, 1))
        d.wire(d.p(middle, 2), ov, d.p(low, 1))
        d.wire(ov, (568, y + 221), (568, y + 164), d.p(ref, 2))
        d.dot(uv)
        d.dot(ov)
        gnd_down(d, low, 2, y + 307)
    else:
        d.passive("R44", 477, y + 102, side="left")
        d.passive("R45", 477, y + 184, side="left")
        d.wire((477, trunk), d.p("R44", 1))
        d.wire(d.p("R44", 2), (477, y + 129), d.p(ref, 1))
        d.wire((477, y + 129), d.p("R45", 1))
        d.dot((477, y + 129))
        gnd_down(d, "R45", 2, y + 229)
        point = d.p(ref, 2)
        d.wire(point, (578, point[1]), (578, y + 222))
        d.ground((578, y + 222))
    d.dot((477, trunk))

    d.nc(d.p(ref, 3))
    d.nc(d.p(ref, 10))
    d.wire(d.p(ref, 8), (651, y + 238), (693, y + 238), d.p(ref, 4))
    d.wire((651, y + 238), (651, y + 258))
    d.dot((651, y + 238))
    d.ground((651, y + 258))
    d.passive(f"R{number}1", 749, y + 258, side="left")
    d.passive(f"C{number}1", 795, y + 258)
    d.wire(d.p(ref, 9), d.p(f"R{number}1", 1))
    d.wire(d.p(ref, 7), d.p(f"C{number}1", 1))
    gnd_down(d, f"R{number}1", 2, y + 298)
    gnd_down(d, f"C{number}1", 2, y + 298)

    output_points = [(x, trunk) for _, x in output_branch]
    if number == 4:
        d.symbol("J3", 1298, y + 37, 135, 166, (
            (1, "left", trunk), (2, "right", y + 112), (3, "right", y + 148),
            (4, "bottom", 1330), ("S", "bottom", 1402),
        ))
        end = d.p("J3", 1)
        for pin in (2, 3):
            d.nc(d.p("J3", pin))
        gnd_down(d, "J3", 4, y + 242)
        gnd_down(d, "J3", "S", y + 242)
    else:
        end = (1115, trunk)
    d.wire(d.p(ref, 6), *output_points, end)
    d.label(vout, end, "end")
    for part, x in output_branch:
        shunt(d, part, x, y + 135, trunk)


def sheet_1(d):
    d.canvas.text(45, 176, "U1 protected input", 19, INK, True)
    breaker(d, 1, 222, "U1_RAW", "A5", (("C10", 260), ("D10", 346)), (("C12", 959),))
    note(d, 1050, 408, "Dedicated 5 V adapter and keyed or retained cable.",
         "Qualify voltage, startup and current with the selected adapter.")
    break_line(d, 584, "Protected U1-only U3 outlet")
    breaker(d, 4, 666, "A5", "U3_VBUS", (), (("C42", 942), ("R46", 1075)))
    note(d, 1000, 929, "J3 takes its power from U1 only.",
         "Data pins stay open. Verify actual outlet and aggregate load.")
    break_line(d, 1019, "U2 protected input to B5")
    breaker(d, 2, 1100, "U2_RAW", "B5", (("C20", 325),), (("C22", 961),))
    note(d, 935, 1350, "IC2 upper trip raised to a 5.71 V nominal screen.",
         "J2 current remains bounded by the actual USB mode.")
    note(d, 960, 474, "D10 cathode and band face U1_RAW. Anode returns to GND.")


def mux(d):
    ref, x, y = "IC9", 700, 247
    d.symbol(ref, x, y, 181, 219, (
        (3, "left", y + 42), (5, "left", y + 77),
        (6, "left", y + 125), (4, "left", y + 177),
        ("2,7", "right", y + 42), (8, "right", y + 100),
        (1, "bottom", x + 86),
    ))
    d.nc(d.p(ref, 8))
    gnd_down(d, ref, 1, y + 249)
    source = (x - 290, y + 42)
    divider = (x - 230, y + 42)
    mode = (x - 84, y + 42)
    d.wire(source, divider, mode, d.p(ref, 3))
    d.label("B5", source)
    d.wire(mode, (mode[0], y + 77), d.p(ref, 5))
    d.dot(mode)
    stub(d, ref, 6, "A5", x - 133)
    d.passive("R901", divider[0], y + 135, side="left")
    d.passive("R902", divider[0], y + 224, side="left")
    tap = (divider[0], y + 177)
    d.wire(divider, d.p("R901", 1))
    d.dot(divider)
    d.wire(d.p("R901", 2), tap, d.p("R902", 1))
    d.wire(tap, d.p(ref, 4))
    d.dot(tap)
    gnd_down(d, "R902", 2, y + 265)
    branch = x + 310
    d.wire(d.p(ref, "2,7"), (branch, y + 42), (branch + 198, y + 42))
    d.label("SYS5", (branch + 198, y + 42), "end")
    shunt(d, "C31", branch, y + 95, y + 42)


def output_breaker(d, y):
    top = y + 89
    d.symbol("IC6", 580, y + 53, 218, 210, (
        (5, "left", top), (1, "left", y + 152), (4, "left", y + 204),
        (6, "right", top), (3, "right", y + 156), (10, "right", y + 201),
        (2, "bottom", 610), (8, "bottom", 650), (9, "bottom", 707), (7, "bottom", 770),
    ))
    d.wire((75, top), (201, top), d.p("IC6", 5))
    d.label("A5", (75, top))
    d.passive("R68", 201, y + 117, side="left")
    d.passive("R69", 201, y + 210, side="left")
    d.wire((201, top), d.p("R68", 1))
    d.dot((201, top))
    d.wire(d.p("R68", 2), (201, y + 152), d.p("R69", 1))
    d.wire((201, y + 152), d.p("IC6", 1))
    d.dot((201, y + 152))
    gnd_down(d, "R69", 2, y + 247)

    d.wire(d.p("IC6", 4), (523, y + 204), (523, y + 247))
    d.ground((523, y + 247))
    d.nc(d.p("IC6", 3))
    d.nc(d.p("IC6", 10))
    d.wire(d.p("IC6", 2), (610, y + 303), (650, y + 303), d.p("IC6", 8))
    d.wire((610, y + 303), (610, y + 320))
    d.dot((610, y + 303))
    d.ground((610, y + 320))
    d.passive("R61", 707, y + 321, side="left")
    d.wire(d.p("IC6", 9), d.p("R61", 1))
    gnd_down(d, "R61", 2, y + 363)
    d.passive("R67", 770, y + 321)
    d.passive("C61", 770, y + 393)
    d.wire(d.p("IC6", 7), d.p("R67", 1))
    d.wire(d.p("R67", 2), d.p("C61", 1))
    gnd_down(d, "C61", 2, y + 432)

    tees = tuple((x, top) for x in (1008, 1158, 1260))
    end = (1510, top)
    d.wire(d.p("IC6", 6), *tees, end)
    d.label("U4_VBUS", end, "end")
    for ref, x in (("C62", 1008), ("C63", 1158), ("R66", 1260)):
        shunt(d, ref, x, y + 153, top, side="below", polar=ref == "C62")


def sheet_4(d):
    d.canvas.text(440, 178, "Module and directly wired accessories", 19, INK, True)
    d.canvas.text(45, 245, "Power, controls and USB", 17, INK, True)
    remote_pins = (
        "P1.21", "P1.22", "P1.23", "T-C.VBUS", "T-C.A6,B6", "T-C.A7,B7",
        "P1.15", "P1.16", "P1.9", "P1.10", "P2.2", "P2.4", "P2.5", "P3.5",
        "P1.3", "P1.4", "P1.7", "P1.8", "P1.17", "P1.18", "P1.19",
        "P1.20", "P2.3", "P3.1", "P3.2", "P3.3", "P3.4", "T-C.CC1,CC2", "INT",
    )
    right = (
        ("P1.14", 357), ("P1.12", 468), ("P1.13", 579), ("P1.11", 687),
        ("P1.6", 978), ("P1.5", 1089),
        ("P1.1", 1220), ("P1.2", 1307), ("P2.1", 1430),
    )
    d.symbol("M1", 435, 241, 450, 1230,
             tuple((p, "left", 285 + i * 40) for i, p in enumerate(remote_pins))
             + tuple((p, "right", y) for p, y in right))
    for p in remote_pins:
        net = d.expected[("M1", p)]["net"]
        if net == "NC":
            d.nc(d.p("M1", p))
        elif net == "GND":
            y = d.p("M1", p)[1]
            d.wire(d.p("M1", p), (387, y), (387, y + 14))
            d.ground((387, y + 14))
        else:
            stub(d, "M1", p, net, 260)
    note(d, 45, 921, "Former USB-C receptacle absent.",
         "Insulate former VBUS lands. They carry SYS5.",
         "Keep BOOTSEL and RESET accessible.")
    note(d, 45, 1051, "X marks an unused pin or isolated pad.",
         "Built-in GP16 pixel has no external link.")

    d.symbol("J5", 1240, 283, 274, 446, (
        (2, "left", 357), (3, "left", 468), (7, "left", 579), (5, "left", 687),
        (4, "bottom", 1290), (6, "bottom", 1415),
    ))
    for p, jp in (("P1.14", 2), ("P1.12", 3), ("P1.13", 7)):
        d.wire(d.p("M1", p), d.p("J5", jp))
    d.passive("R56", 1115, 687, horizontal=True)
    d.wire(d.p("M1", "P1.11"), d.p("R56", 1))
    d.wire(d.p("R56", 2), d.p("J5", 5))
    gnd_down(d, "J5", 6, 779)
    d.wire(d.p("J5", 4), (1290, 793), (1182, 793), (1182, 771))
    d.supply("3V3_AON", (1182, 771))
    note(d, 965, 832, "Numbers identify card functions. Confirm actual breakout pad order.",
         "Use an SD assembly with 3.3 V logic, card pulls and local bypass.")

    d.symbol("J6", 1240, 925, 274, 203, (
        (3, "left", 978), (4, "left", 1089), (2, "bottom", 1320), (1, "bottom", 1415),
    ))
    d.wire(d.p("M1", "P1.6"), d.p("J6", 3))
    d.wire(d.p("M1", "P1.5"), d.p("J6", 4))
    d.wire(d.p("J6", 2), (1320, 1170), (1200, 1170), (1200, 1154))
    d.supply("3V3_AON", (1200, 1154))
    d.wire(d.p("J6", 1), (1415, 1154), (1526, 1154), (1526, 1175))
    d.ground((1526, 1175))

    d.symbol("J7", 1240, 1200, 274, 137, (
        (2, "left", 1220), (3, "left", 1307), (1, "bottom", 1415),
    ))
    for p, connector, pin, ref, y, reverse in (
        ("P1.1", "J7", 2, "R71", 1220, False),
        ("P1.2", "J7", 3, "R72", 1307, True),
    ):
        d.passive(ref, 1115, y, horizontal=True, reverse=reverse)
        d.wire(d.p("M1", p), d.p(ref, 2 if reverse else 1))
        d.wire(d.p(ref, 1 if reverse else 2), d.p(connector, pin))
    d.wire(d.p("J7", 1), (1415, 1363), (1526, 1363), (1526, 1381))
    d.ground((1526, 1381))
    rgb(d)
    note(d, 45, 1705, "J5/J6 use the module 3.3 V rail. Fit only after combined startup, active and suspend checks.",
         "J7 is 3.3 V UART. J8 has U1-only 5 V supply, direct 3.3 V GP17 data and common ground.")


def rgb(d):
    d.symbol("J8", 1240, 1460, 274, 125, (
        (2, "left", 1510), (3, "top", 1415), (1, "bottom", 1415),
    ), title_at=(1310, 1413))
    d.passive("R81", 1115, 1510, horizontal=True)
    d.wire(d.p("M1", "P2.1"), (950, 1430), (950, 1510), d.p("R81", 1))
    d.wire(d.p("R81", 2), d.p("J8", 2))
    rail_up(d, "J8", 3, "A5", 1415)
    gnd_down(d, "J8", 1, 1635)
    note(d, 975, 1657, "Verify 3.3 V data against the fitted 5 V pixel input.",
         "GP17 can drive DATA while U1 is off. Fit/use held for injection.")


def sheet_2(d):
    d.canvas.text(45, 175, "Automatic Pico power, with qualified U2 preferred", 19, INK, True)
    mux(d)
    note(d, 850, 520, "U2 feeds SYS5 only. A5 takes over when B5 falls below the selection threshold.",
         "Switchover can dip or reset the module. Check actual rail limits and J2 current.")
    break_line(d, 574, "Independent U1-only U4 output protection")
    output_breaker(d, 617)
    note(d, 945, 1005, "IC6 and U4 draw only from A5, regardless of the selected SYS5 source.",
         "Direct U4 D+/D- remain connected when U1 is off. Check attached-device behavior.")


def sheet_3(d):
    d.canvas.text(45, 176, "U2 native Device with no intervening data switch", 19, INK, True)
    d.symbol("J2", 140, 237, 230, 221, (
        (1, "left", 288), (3, "right", 315), (2, "right", 387),
        (4, "bottom", 203), ("S", "bottom", 303),
    ))
    stub(d, "J2", 1, "U2_RAW", 50)
    stub(d, "J2", 3, "N_DP", 990)
    stub(d, "J2", 2, "N_DM", 990)
    gnd_down(d, "J2", 4, 501)
    gnd_down(d, "J2", "S", 501)
    note(d, 1040, 321, "N_DP / N_DM go to the original module data lands.",
         "Keep both stock 27R module series resistors.",
         "Fit verified Rd and duplicate USB-C data contacts.")
    note(d, 680, 464, "Keep the original receptacle absent and the former VBUS pads insulated.",
         "U1 can keep the native D+ pull-up active while J2 VBUS is off.")

    break_line(d, 567, "U4 direct PIO Host through series termination")
    d.symbol("J4", 1158, 665, 235, 260, (
        (1, "left", 710), (3, "left", 763), (2, "left", 883),
        (4, "bottom", 1210), ("S", "bottom", 1345),
    ))
    stub(d, "J4", 1, "U4_VBUS", 1010)
    for pin, y, series, pull, net in ((3, 763, "R121", "R123", "PIO_DP"),
                                      (2, 883, "R122", "R124", "PIO_DM")):
        d.passive(series, 475, y, horizontal=True)
        d.wire((245, y), d.p(series, 1))
        d.label(net, (245, y))
        d.wire(d.p(series, 2), (876, y), d.p("J4", pin))
        shunt(d, pull, 876, y + 48, y)
    gnd_down(d, "J4", 4, 968)
    gnd_down(d, "J4", "S", 968)
    note(d, 50, 977, "GP14 is D+ and GP15 is D-. Qualify one effective host pull-down per data line.",
         "When U4 VBUS is off, direct data can still inject current in either direction. Qualify the exact peripheral.")
