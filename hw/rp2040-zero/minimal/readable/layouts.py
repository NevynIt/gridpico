"""Presentation coordinates. All values and pin functions are read from the sealed ledger."""

from drawing import draw_gate, draw_power, INK, SUB, EDGE


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
    d.wire(start, *input_points, (453, trunk), d.p(ref, 5))
    if number == 1:
        d.label(vin, (453, trunk), "end")
    else:
        d.label(vin, start)
    for part, x in input_branch:
        shunt(d, part, x, y + 123, trunk, side="below" if part == "D10" else "right")

    en_top, en_bottom = f"R{number}4", f"R{number}5"
    d.passive(en_top, 453, y + 102, side="left")
    d.passive(en_bottom, 453, y + 184, side="left")
    d.wire((453, trunk), d.p(en_top, 1))
    d.dot((453, trunk))
    d.wire(d.p(en_top, 2), (453, y + 129), d.p(ref, 1))
    d.wire((453, y + 129), d.p(en_bottom, 1))
    d.dot((453, y + 129))
    gnd_down(d, en_bottom, 2, y + 229)
    if number in (1, 2):
        ov_top, ov_bottom = f"R{number}2", f"R{number}3"
        d.passive(ov_top, 539, y + 199, side="left")
        d.passive(ov_bottom, 539, y + 282, side="left")
        rail_up(d, ov_top, 1, vin, y + 163)
        d.wire(d.p(ov_top, 2), (539, y + 240), d.p(ov_bottom, 1))
        d.wire((539, y + 240), (584, y + 240), (584, y + 164), d.p(ref, 2))
        d.dot((539, y + 240))
        gnd_down(d, ov_bottom, 2, y + 325)
    else:
        point = d.p(ref, 2)
        d.wire(point, (578, point[1]), (578, y + 222))
        d.ground((578, y + 222))

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
    elif number == 2:
        d.symbol("SJ1", 1145, y + 37, 225, 162, (
            (3, "left", trunk), (1, "left", y + 132), (2, "right", trunk),
        ))
        end = d.p("SJ1", 3)
        d.wire(d.p("SJ1", 1), (1071, y + 132), (1071, y + 119))
        d.supply("A5", (1071, y + 119))
        d.wire(d.p("SJ1", 2), (1440, trunk), (1538, trunk))
        d.label("SYS5", (1538, trunk), "end")
        shunt(d, "C31", 1440, y + 134, trunk)
        note(d, 1110, y + 229, "Bridge 1-2 or 2-3 with power off.",
             "Default 1-2 from U1. U2 profile held.")
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
    break_line(d, 584, "U1-only protected U3 outlet")
    breaker(d, 4, 666, "A5", "U3_VBUS", (), (("C42", 942), ("R46", 1075)))
    note(d, 1000, 929, "300 mA accessory budget. Data pins open.",
         "No USB charging signature. Confirm the actual load.")
    break_line(d, 1019, "U2 protected input and module power selection")
    breaker(d, 2, 1100, "U2_RAW", "B5", (("C20", 325),), (("C22", 961),))
    note(d, 942, 1433, "B5 provides source protection and does not establish a USB current budget.")


def sheet_2(d):
    canvas = d.canvas
    canvas.text(50, 175, "U2 current-limited branch", 19, INK, True)
    canvas.text(505, 175, "Source selector", 19, INK, True)
    canvas.text(850, 175, "Final breaker, slew and output", 19, INK, True)
    draw_power(d)
    note(d, 50, 593,
         "GP8 requires an independently authorized U2 budget. B-branch settled ceiling is proposed at 200 mA.",
         "Drop GP9 before a source change. Establish the new source and actual VBUS clearance before a fresh attach.")
    break_line(d, 659, "GP8 branch grant from B5")
    canvas.text(824, 692, "GP9 port veto and enable from HOST_PRE", 19, INK, True)
    draw_gate(d, 0, "70", "B5", "U4_B_GRANT", "EN7")
    draw_gate(d, 774, "60", "HOST_PRE", "U4_PORT_EN", "EN6")


def data_path(d, ref, y):
    top, lower = y + 91, y + 223
    device = ref == 10
    plus, minus = ("C2_DP", "C2_DM") if device else ("H_DP", "H_DM")
    endpoint = "NUSB_OE_N" if device else "HUSB_OE_N"
    mux_x = 900 if device else 470
    esd_x = 480 if device else 1133
    if device:
        d.symbol("J2", 106, y + 55, 170, 196, (
            (1, "left", y + 98), (3, "right", top), (2, "right", lower),
            (4, "bottom", 155), ("S", "bottom", 225),
        ))
        stub(d, "J2", 1, "U2_RAW", 43)
        gnd_down(d, "J2", 4, y + 288)
        gnd_down(d, "J2", "S", y + 288)
    mux = f"IC{ref}"
    left_plus, left_minus = (1, 2) if device else (5, 4)
    right_plus, right_minus = (5, 4) if device else (1, 2)
    d.symbol(mux, mux_x, y + 55, 215, 220, (
        (left_plus, "left", top), (left_minus, "left", lower), (8, "left", y + 173),
        (right_plus, "right", top), (right_minus, "right", lower),
        (7, "right", y + 139), (6, "right", y + 173),
        (3, "bottom", mux_x + 34), (10, "bottom", mux_x + 83), (9, "bottom", mux_x + 157),
    ))
    esd = "D101" if device else "D121"
    esd_mid = esd_x + 80
    d.symbol(esd, esd_x, y + 123, 160, 69, (
        (2, "top", esd_mid - 22), (1, "bottom", esd_mid + 22), (3, "left", y + 158),
    ), title_at=(esd_x - 125, y + 126))
    # ESD pins face their respective data wires; the shared return stays in the middle.
    for line_y, dx, pin in ((top, -22, 2), (lower, 22, 1)):
        point = (esd_mid + dx, line_y)
        if device:
            jpin, mux_pin = (3, 1) if line_y == top else (2, 2)
            d.wire(d.p("J2", jpin), point, d.p(mux, mux_pin))
        else:
            mux_pin, term, end_net = (1, "R123", plus) if line_y == top else (2, "R124", minus)
            end = (1530, line_y)
            terminal_tee = (1406, line_y)
            d.wire(d.p(mux, mux_pin), point, terminal_tee, end)
            d.label(end_net, end, "end")
            shunt(d, term, 1406, line_y + 47, line_y)
        d.dot(point)
        d.wire(point, d.p(esd, pin))
    d.wire(d.p(esd, 3), (esd_x - 54, y + 158), (esd_x - 54, y + 181))
    d.ground((esd_x - 54, y + 181))
    stub(d, mux, 8, endpoint, mux_x - 166)
    for pin in (6, 7):
        d.nc(d.p(mux, pin))
    for pin in (3, 10):
        gnd_down(d, mux, pin, y + 303)
    supply_point = (mux_x + 274, y + 305)
    cap_x = mux_x + 323
    d.passive(f"C{ref}1", cap_x, y + 350)
    d.wire(d.p(mux, 9), (mux_x + 157, y + 305), supply_point, (cap_x, y + 305), d.p(f"C{ref}1", 1))
    d.wire(supply_point, (supply_point[0], y + 298))
    d.supply("3V3_AON", (supply_point[0], y + 298))
    d.dot(supply_point)
    gnd_down(d, f"C{ref}1", 2, y + 389)
    if device:
        stub(d, mux, 5, "N_DP", 1470)
        stub(d, mux, 4, "N_DM", 1470)
        note(d, 1174, y + 132, "Former M1 native USB lands.",
             "Keep the module series resistors.")
    else:
        for y_pin, res, net, pin in ((top, "R121", "PIO_DP", 5),
                                      (lower, "R122", "PIO_DM", 4)):
            d.passive(res, 240, y_pin, horizontal=True)
            d.wire((62, y_pin), d.p(res, 1))
            d.label(net, (62, y_pin))
            d.wire(d.p(res, 2), d.p(mux, pin))


def monitor(d, x, y, ref, raw, reset, mr):
    def at(dx, dy):
        return x + dx, y + dy

    ic = f"IC{ref}"
    d.symbol(ic, x + 308, y + 73, 171, 148, (
        (1, "left", y + 143), (3, "left", y + 184),
        (6, "right", y + 156), (4, "top", x + 379),
        (2, "bottom", x + 353), (5, "bottom", x + 437),
    ), title_at=(x + 269, y + 42))
    d.passive(f"R{ref}2", *at(122, 105), side="left")
    d.passive(f"R{ref}3", *at(122, 199), side="left")
    rail_up(d, f"R{ref}2", 1, raw, y + 67)
    d.wire(d.p(f"R{ref}2", 2), at(122, 143), d.p(ic, 1))
    d.wire(at(122, 143), d.p(f"R{ref}3", 1))
    d.dot(at(122, 143))
    gnd_down(d, f"R{ref}3", 2, y + 238)
    stub(d, ic, 3, mr, x + 169)
    d.passive(f"C{ref}1", *at(549, 89))
    d.wire(d.p(ic, 4), at(379, 52), at(549, 52), d.p(f"C{ref}1", 1))
    d.wire(at(379, 52), at(379, 38))
    d.dot(at(379, 52))
    d.supply("3V3_AON", at(379, 38))
    gnd_down(d, f"C{ref}1", 2, y + 126)
    gnd_down(d, ic, 2, y + 285)
    d.passive(f"C{ref}2", *at(437, 271))
    d.wire(d.p(ic, 5), d.p(f"C{ref}2", 1))
    gnd_down(d, f"C{ref}2", 2, y + 312)
    d.passive(f"R{ref}1", *at(651, 108))
    rail_up(d, f"R{ref}1", 1, "3V3_AON", y + 70)
    d.wire(d.p(ic, 6), at(651, 156), at(715, 156))
    d.wire(d.p(f"R{ref}1", 2), at(651, 156))
    d.dot(at(651, 156))
    d.label(reset, at(715, 156), "end")


def enable_logic(d, y):
    d.symbol("IC26", 660, y + 64, 244, 165, (
        (1, "left", y + 99), (2, "left", y + 134),
        (5, "left", y + 174), (6, "left", y + 210),
        (7, "right", y + 104), (3, "right", y + 190),
        (4, "bottom", 744), (8, "bottom", 820),
    ))
    d.wire(d.p("IC26", 1), (533, y + 99), (533, y + 134), d.p("IC26", 2))
    d.wire((405, y + 99), (533, y + 99))
    d.dot((533, y + 99))
    d.label("N_VBUS_GOOD", (405, y + 99))
    stub(d, "IC26", 5, "H_VBUS_GOOD", 405)
    stub(d, "IC26", 6, "U4_PORT_EN", 405)
    gnd_down(d, "IC26", 4, y + 280)
    d.passive("C261", 1023, y + 310)
    d.wire(d.p("IC26", 8), (820, y + 268), (950, y + 268), (1023, y + 268), d.p("C261", 1))
    d.wire((950, y + 268), (950, y + 255))
    d.dot((950, y + 268))
    d.supply("3V3_AON", (950, y + 255))
    gnd_down(d, "C261", 2, y + 352)
    for ref, pin, x, y_out, net in (("R101", 7, 1060, y + 104, "NUSB_OE_N"),
                                    ("R125", 3, 1330, y + 190, "HUSB_OE_N")):
        d.passive(ref, x, y_out - 41)
        rail_up(d, ref, 1, "3V3_AON", y_out - 78)
        d.wire(d.p("IC26", pin), (x, y_out), (x + 185, y_out))
        d.wire(d.p(ref, 2), (x, y_out))
        d.dot((x, y_out))
        d.label(net, (x + 185, y_out), "end")


def sheet_3(d):
    d.canvas.text(45, 176, "U2 device data to module", 19, INK, True)
    data_path(d, 10, 202)
    note(d, 48, 560, "Verify passive Rd and both USB-C orientations on the exact J2.")
    break_line(d, 625, "U4 PIO data to host outlet")
    data_path(d, 12, 685)
    note(d, 48, 1026, "Use one qualified 15k pull-down per J4 signal. Verify stock weak pulls and full-speed levels.")
    break_line(d, 1106, "U2 connector sense and dwell")
    d.canvas.text(805, 1139, "U4 connector sense and dwell", 19, INK, True)
    monitor(d, 37, 1170, 11, "U2_RAW", "N_VBUS_GOOD", "3V3_AON")
    monitor(d, 802, 1170, 13, "U4_VBUS", "H_VBUS_GOOD", "U4_PORT_EN")
    break_line(d, 1518, "Active-low data switch commands")
    enable_logic(d, 1568)
    note(d, 48, 1872, "GP18/GP20 report switch commands. GP9 low vetoes U4 data.",
         "Both data switches have power-off protection.")


def signal_pull(d, terminal, connector, pin, resistor, x, *, sd=False):
    y = terminal[1]
    d.wire(terminal, (x, y), d.p(connector, pin))
    d.passive(resistor, x, y - 37, reverse=sd)
    d.wire(d.p(resistor, 1 if sd else 2), (x, y))
    d.dot((x, y))
    rail_up(d, resistor, 2 if sd else 1, "3V3_AON", y - 71)


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

    d.symbol("J5", 1240, 273, 274, 456, (
        (2, "left", 357), (3, "left", 468), (7, "left", 579), (5, "left", 687),
        (4, "bottom", 1275), (6, "bottom", 1350), ("S", "bottom", 1415),
        ("CD", "bottom", 1481), (1, "top", 1300), (8, "top", 1450),
    ))
    for p, jp, r in (("P1.14", 2, "R53"), ("P1.12", 3, "R54"), ("P1.13", 7, "R55")):
        signal_pull(d, d.p("M1", p), "J5", jp, r, 1115, sd=True)
    d.passive("R56", 1115, 687, horizontal=True)
    d.wire(d.p("M1", "P1.11"), d.p("R56", 1))
    d.wire(d.p("R56", 2), d.p("J5", 5))
    for j_pin, r, x in ((1, "R51", 1300), (8, "R52", 1450)):
        d.passive(r, x, 215, reverse=True)
        d.wire(d.p(r, 1), d.p("J5", j_pin))
        rail_up(d, r, 2, "3V3_AON", 181)
    for jp in (6, "S"):
        gnd_down(d, "J5", jp, 779)
    d.nc(d.p("J5", "CD"))
    d.wire(d.p("J5", 4), (1275, 793), (1182, 793), (1055, 793), (945, 793))
    d.wire((1182, 793), (1182, 771))
    d.dot((1182, 793))
    d.supply("3V3_AON", (1182, 771))
    d.passive("C51", 945, 834)
    d.passive("C52", 1055, 834)
    for ref, x in (("C51", 945), ("C52", 1055)):
        d.wire((x, 793), d.p(ref, 1))
        gnd_down(d, ref, 2, 870)
    d.dot((1055, 793))

    d.symbol("J6", 1240, 925, 274, 203, (
        (3, "left", 978), (4, "left", 1089), (2, "bottom", 1320), (1, "bottom", 1415),
    ))
    signal_pull(d, d.p("M1", "P1.6"), "J6", 3, "R65", 1115)
    signal_pull(d, d.p("M1", "P1.5"), "J6", 4, "R64", 1115)
    d.wire(d.p("J6", 2), (1320, 1170), (1200, 1170), (1200, 1154))
    d.supply("3V3_AON", (1200, 1154))
    d.wire(d.p("J6", 1), (1415, 1154), (1526, 1154), (1526, 1175))
    d.ground((1526, 1175))

    d.symbol("J7", 1240, 1200, 274, 137, (
        (2, "left", 1220), (3, "left", 1307), (1, "bottom", 1415),
    ))
    d.symbol("J8", 1240, 1395, 274, 77, ((2, "left", 1430), (1, "bottom", 1415)))
    for p, connector, pin, ref, y, reverse in (
        ("P1.1", "J7", 2, "R71", 1220, False),
        ("P1.2", "J7", 3, "R72", 1307, True),
        ("P2.1", "J8", 2, "R81", 1430, False),
    ):
        d.passive(ref, 1115, y, horizontal=True, reverse=reverse)
        d.wire(d.p("M1", p), d.p(ref, 2 if reverse else 1))
        d.wire(d.p(ref, 1 if reverse else 2), d.p(connector, pin))
    d.wire(d.p("J7", 1), (1415, 1363), (1526, 1363), (1526, 1381))
    d.ground((1526, 1381))
    gnd_down(d, "J8", 1, 1500)
    note(d, 45, 1551, "Fit J5 and J6 only after combined current and thermal qualification. Check OLED pull-ups and connector polarity.",
         "J7 and J8 are 3.3 V data and common ground only. Connect or remove local accessories with power off.")
