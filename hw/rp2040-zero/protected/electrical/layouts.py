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
    break_line(d, 584, "U1-only protected U3 outlet")
    breaker(d, 4, 666, "A5", "U3_VBUS", (), (("C42", 942), ("R46", 1075)))
    note(d, 1000, 929, "300 mA accessory budget. Data pins open.",
         "No USB charging signature. Confirm the actual load.")
    break_line(d, 1019, "U2 protected input to B5")
    breaker(d, 2, 1100, "U2_RAW", "B5", (("C20", 325),), (("C22", 961),))
    note(d, 935, 1350, "IC2 upper trip raised to a 5.71 V nominal screen.",
         "J2 current remains bounded by the actual USB mode.")
    note(d, 960, 474, "D10 cathode and band face U1_RAW. Anode returns to GND.")


def data_path(d, ref, y):
    top, lower = y + 91, y + 223
    device = ref == 10
    plus, minus = ("C2_DP", "C2_DM") if device else ("H_DP", "H_DM")
    endpoint = "J2_PRESENT" if device else "J4_PATH_OK"
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
    left_plus, left_minus = (1, 2) if device else (7, 6)
    right_plus, right_minus = (7, 6) if device else (1, 2)
    d.symbol(mux, mux_x, y + 55, 215, 220, (
        (left_plus, "left", top), (left_minus, "left", lower), (10, "left", y + 173),
        (right_plus, "right", top), (right_minus, "right", lower),
        (5, "right", y + 139), (4, "right", y + 173),
        (3, "bottom", mux_x + 34), (8, "bottom", mux_x + 83), (9, "bottom", mux_x + 157),
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
    stub(d, mux, 10, endpoint, mux_x - 166)
    for pin in (4, 5):
        d.nc(d.p(mux, pin))
    for pin in (3, 8):
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
        stub(d, mux, 7, "N_DP", 1470)
        stub(d, mux, 6, "N_DM", 1470)
        note(d, 1174, y + 132, "Former M1 native USB lands.",
             "Keep the module series resistors.")
    else:
        for y_pin, res, net, pin in ((top, "R121", "PIO_DP", 7),
                                      (lower, "R122", "PIO_DM", 6)):
            d.passive(res, 240, y_pin, horizontal=True)
            d.wire((62, y_pin), d.p(res, 1))
            d.label(net, (62, y_pin))
            d.wire(d.p(res, 2), d.p(mux, pin))


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
    note(d, 45, 1551, "Fit J5/J6 only after combined rail qualification. J6 needs onboard 3.3 V I2C pulls. Verify connector polarity.",
         "J7 and J8 are 3.3 V data and common ground only. Connect or remove local accessories with power off.")


def mux(d, ref, x, y, other, output, cap, rn):
    d.symbol(ref, x, y, 181, 184, (
        (3, "left", y + 35), (6, "left", y + 79), (4, "left", y + 112),
        (5, "left", y + 151), ("2,7", "right", y + 35), (8, "right", y + 99),
        (1, "bottom", x + 86),
    ))
    d.nc(d.p(ref, 8))
    gnd_down(d, ref, 1, y + 224)
    # VIN1 and MODE share A5. Stage the priority divider immediately left of PR1.
    a = (x - 168, y + 35)
    d.wire(d.p(ref, 3), a)
    d.label("A5", a)
    stub(d, ref, 6, other, x - 160)
    d.wire(d.p(ref, 5), (x - 65, y + 151), (x - 65, y + 146))
    d.supply("A5", (x - 65, y + 146))
    rtop, rbot = f"R{rn}1", f"R{rn}2"
    pos = x - 249
    d.passive(rtop, pos, y + 68, side="left")
    d.passive(rbot, pos, y + 154, side="left")
    rail_up(d, rtop, 1, "A5", y + 31)
    tee = (pos, y + 112)
    d.wire(d.p(rtop, 2), tee, d.p(rbot, 1))
    d.wire(tee, d.p(ref, 4))
    d.dot(tee)
    gnd_down(d, rbot, 2, y + 195)
    branch = x + 310
    d.wire(d.p(ref, "2,7"), (branch, y + 35), (branch + 198, y + 35))
    d.label(output, (branch + 198, y + 35), "end")
    shunt(d, cap, branch, y + 88, y + 35)


def branch(d, y):
    d.symbol("IC7", 273, y + 35, 178, 181, (
        (1, "left", y + 86), (3, "left", y + 181),
        (6, "right", y + 86), (4, "right", y + 148),
        (2, "bottom", 316), (5, "bottom", 401),
    ))
    d.wire((80, y + 86), (129, y + 86), d.p("IC7", 1))
    d.label("B5", (80, y + 86))
    shunt(d, "C72", 129, y + 117, y + 86, side="left")
    d.wire((150, y + 181), (199, y + 181), d.p("IC7", 3))
    d.label("B_BRANCH_EN_H", (150, y + 181))
    shunt(d, "R77", 199, y + 234, y + 181, side="left")
    d.nc(d.p("IC7", 4))
    gnd_down(d, "IC7", 2, y + 269)
    d.passive("R75", 401, y + 271)
    d.wire(d.p("IC7", 5), d.p("R75", 1))
    gnd_down(d, "R75", 2, y + 306)
    d.wire(d.p("IC7", 6), (519, y + 86), (596, y + 86))
    shunt(d, "C71", 519, y + 143, y + 86)
    d.label("B_U4", (596, y + 86), "end")


def output_breaker(d, y):
    top = y + 89
    d.symbol("IC6", 580, y + 53, 218, 210, (
        (5, "left", top), (1, "left", y + 152), (4, "left", y + 204),
        (6, "right", top), (3, "right", y + 156), (10, "right", y + 201),
        (2, "bottom", 610), (8, "bottom", 650), (9, "bottom", 707), (7, "bottom", 770),
    ))
    d.wire((75, top), (201, top), d.p("IC6", 5))
    d.label("HOST5", (75, top))
    d.passive("R68", 201, y + 117, side="left")
    d.passive("R69", 201, y + 210, side="left")
    d.wire((201, top), d.p("R68", 1))
    d.dot((201, top))
    d.wire(d.p("R68", 2), (201, y + 152), d.p("R69", 1))
    d.wire((201, y + 152), d.p("IC6", 1))
    d.dot((201, y + 152))
    gnd_down(d, "R69", 2, y + 247)

    d.passive("R681", 407, y + 231, side="left")
    d.passive("R682", 407, y + 299, side="left")
    rail_up(d, "R681", 1, "U4_VBUS", y + 168)
    d.wire(d.p("R681", 2), (407, y + 265), d.p("R682", 1))
    d.wire((407, y + 265), (520, y + 265), (520, y + 204), d.p("IC6", 4))
    d.dot((407, y + 265))
    gnd_down(d, "R682", 2, y + 338)
    stub(d, "IC6", 3, "J4_EFUSE_PG", 942)
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

    d.symbol("J4", 1385, y + 55, 107, 190, (
        (1, "left", top), (2, "right", y + 142), (3, "right", y + 178),
        (4, "bottom", 1415), ("S", "bottom", 1461),
    ))
    tees = tuple((x, top) for x in (1008, 1158, 1260))
    d.wire(d.p("IC6", 6), *tees, d.p("J4", 1))
    d.label("U4_VBUS", (1158, top), "middle")
    for ref, x in (("C62", 1008), ("C63", 1158), ("R66", 1260)):
        shunt(d, ref, x, y + 153, top, side="below", polar=ref == "C62")
    stub(d, "J4", 2, "H_DM", 1545)
    stub(d, "J4", 3, "H_DP", 1545)
    gnd_down(d, "J4", 4, y + 276)
    gnd_down(d, "J4", "S", y + 276)


def sheet_2(d):
    d.canvas.text(45, 175, "Automatic module power", 19, INK, True)
    mux(d, "IC9", 713, 239, "B5", "SYS5", "C31", 90)
    note(d, 840, 440, "A is preferred. B fallback carries the module and qualified local loads only.",
         "J2 default, ROM/reset and suspended fallback still require measured current.")
    break_line(d, 505, "U4 source selection and U2 admission")
    branch(d, 587)
    mux(d, "IC8", 1010, 622, "B_U4", "HOST5", "C81", 80)
    note(d, 62, 940, "The B branch stays off without authorization. IC7 independently limits B current.",
         "U4 may disconnect during cutover. Reattach only after qualified power and data state.")
    break_line(d, 978, "Reverse-blocked U4 output with local power-good")
    output_breaker(d, 1048)
    note(d, 963, 1454, "B: allow 200 mA settled only after qualification.",
         "Qualify ramp, hot plug, forced J4 VBUS and latch-off recovery.")


def data_qualifier(d, x, y, native):
    ref = "IC11" if native else "IC13"
    result = "J2_PRESENT" if native else "J4_PATH_OK"
    d.symbol(ref, x + 316, y + 76, 166, 151, (
        (3, "left", y + 132), (4, "left", y + 185), (1, "right", y + 146),
        (6, "top", x + 430), (5, "bottom", x + 345), (2, "bottom", x + 418),
    ), title_at=(x + 331, y + 44))
    if native:
        d.passive("R112", x + 112, y + 94, side="left")
        d.passive("R113", x + 112, y + 188, side="left")
        rail_up(d, "R112", 1, "U2_RAW", y + 60)
        tee = (x + 112, y + 132)
        d.wire(d.p("R112", 2), tee, d.p("R113", 1))
        d.wire(tee, d.p(ref, 3))
        d.dot(tee)
        gnd_down(d, "R113", 2, y + 230)
    else:
        d.passive("R131", x + 134, y + 93, side="left")
        rail_up(d, "R131", 1, "3V3_AON", y + 56)
        end = (x + 134, y + 132)
        d.wire(d.p("R131", 2), end, d.p(ref, 3))
        d.label("J4_EFUSE_PG", end)
    d.wire(d.p(ref, 5), (x + 345, y + 277), (x + 258, y + 277), (x + 258, y + 185), d.p(ref, 4))
    gnd_down(d, ref, 2, y + 269)
    d.passive("C111" if native else "C131", x + 567, y + 84)
    bypass = "C111" if native else "C131"
    d.wire(d.p(ref, 6), (x + 430, y + 51), (x + 567, y + 51), d.p(bypass, 1))
    d.wire((x + 430, y + 51), (x + 430, y + 33))
    d.dot((x + 430, y + 51))
    d.supply("3V3_AON", (x + 430, y + 33))
    gnd_down(d, bypass, 2, y + 119)
    out = (x + 655, y + 146)
    d.wire(d.p(ref, 1), out, (x + 712, y + 146))
    d.label(result, (x + 712, y + 146), "end")
    shunt(d, "R114" if native else "R134", out[0], y + 201, y + 146)


def authorization(d, y):
    d.symbol("IC14", 491, y + 52, 173, 150, (
        (1, "left", y + 99), (6, "left", y + 157), (4, "right", y + 99),
        (2, "bottom", 550), (3, "top", 522), (5, "top", 620),
    ), title_at=(450, y + 24))
    d.symbol("IC15", 969, y + 52, 167, 150, (
        (1, "left", y + 99), (2, "left", y + 157), (4, "right", y + 99),
        (3, "bottom", 1065), (5, "top", 1059),
    ), title_at=(991, y + 24))
    start, split = (70, y + 99), (314, y + 99)
    d.wire(start, (173, y + 99), split, d.p("IC14", 1))
    d.label("U2_AUTH_H", start)
    shunt(d, "R141", 173, y + 165, y + 99)
    d.wire(split, (314, y + 283), (918, y + 283), (918, y + 157), d.p("IC15", 2))
    d.dot(split)
    stub(d, "IC14", 6, "J2_PRESENT", 348)
    d.wire(d.p("IC14", 4), d.p("IC15", 1))
    stub(d, "IC15", 4, "B_BRANCH_EN_H", 1505)
    gnd_down(d, "IC14", 2, y + 237)
    gnd_down(d, "IC15", 3, y + 237)
    d.wire(d.p("IC14", 3), (522, y + 19), (620, y + 19), d.p("IC14", 5))
    d.wire((620, y + 19), (620, y + 9))
    d.dot((620, y + 19))
    d.supply("3V3_AON", (620, y + 9))
    rail_up(d, "IC15", 5, "3V3_AON", y + 8)
    for part, x in (("C141", 735), ("C151", 1250)):
        d.passive(part, x, y + 209)
        rail_up(d, part, 1, "3V3_AON", y + 171)
        gnd_down(d, part, 2, y + 246)
    note(d, 65, y + 322,
         "GP8 low independently denies B. A sensed J2 loss clears the latch even if GP8 stays high.",
         "Only a fresh authorized GP8 rising edge can enable B. USB suspend/reset must release GP8 in firmware.")


def sheet_3(d):
    d.canvas.text(45, 176, "U2 data and relocated native connector", 19, INK, True)
    data_path(d, 10, 202)
    note(d, 48, 560, "Use J2 Rd on each Type-C CC pin. Keep the module native 27R series resistors.")
    break_line(d, 625, "U4 PIO data to host outlet")
    data_path(d, 12, 685)
    note(d, 48, 1026, "Qualify one effective 15k pull-down per host data line. Disable stock weak pulls for this profile.")
    break_line(d, 1106, "Raw J2 voltage qualifies native data")
    d.canvas.text(807, 1139, "Local U4 breaker PG qualifies PIO data", 19, INK, True)
    data_qualifier(d, 37, 1177, True)
    data_qualifier(d, 802, 1177, False)
    break_line(d, 1518, "U2 authorization resets on actual disconnect")
    authorization(d, 1617)
