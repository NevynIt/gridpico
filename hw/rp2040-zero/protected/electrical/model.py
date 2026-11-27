"""Editable pin model of an isolated four-port electrical successor."""
from collections import defaultdict, namedtuple
import csv
from pathlib import Path

BASE = Path(__file__).resolve().parent
FIELDS = ("sheet", "reference", "value", "pin", "function", "net")
Row = namedtuple("Row", FIELDS)


def build():
    with (BASE / "baseline/pin-netlist.csv").open(newline="") as file:
        original = [Row(**row) for row in csv.DictReader(file)]

    parts = defaultdict(list)
    for row in original:
        parts[row.reference].append(row)

    def put(sheet, ref, value, *pins):
        parts[ref] = [Row(str(sheet), ref, value, str(pin), function, net or "NC")
                      for pin, function, net in pins]

    def passive(sheet, ref, value, a, b):
        put(sheet, ref, value, (1, "", a), (2, "", b))

    def change(ref, pin, net, function=None):
        rows = parts[ref]
        pos = next(i for i, row in enumerate(rows) if row.pin == str(pin))
        rows[pos] = rows[pos]._replace(net=net or "NC", function=function or rows[pos].function)

    def move(ref, sheet):
        parts[ref] = [row._replace(sheet=str(sheet)) for row in parts[ref]]

    removed = {
        "SJ1", "Q60A", "Q60B", "Q70A", "Q70B",
        "R601", "R602", "R603", "R604", "R701", "R702", "R703", "R704",
        "R76", "IC26", "R101", "R125", "C261", "R111", "C112", "R132", "R133", "C132",
        "R14", "R15", "R24", "R25",
        "R51", "R52", "R53", "R54", "R55", "C51", "C52", "R64", "R65",
    }
    for ref in removed:
        del parts[ref]

    put(1, "D10", "SMF6.0A, exact vendor open",
        (1, "K Cathode / band", "U1_RAW"), (2, "A Anode", "GND"))
    for n, raw, top, middle in ((1, "U1_RAW", "32.4k, 0.1%", "2.87k, 0.1%"),
                                (2, "U2_RAW", "34.0k, 0.1%", "3.57k, 0.1%")):
        change(f"IC{n}", 1, f"UV{n}")
        passive(1, f"R{n}2", top, raw, f"UV{n}")
        passive(1, f"R{n}3", middle, f"UV{n}", f"OV{n}")
        passive(1, f"R{n}6", "10.0k, 0.1%", f"OV{n}", "GND")
    change("IC4", 1, "UV4")
    change("R44", 2, "UV4")
    change("R45", 1, "UV4")

    def priority_mux(ref, output, alternative, rn):
        put(2, ref, "TPS2116DRLR",
            (1, "GND", "GND"), ("2,7", "VOUT", output), (3, "VIN1", "A5"),
            (4, "PR1", f"PRI_{ref}"), (5, "MODE", "A5"),
            (6, "VIN2", alternative), (8, "ST", None))
        passive(2, f"R{rn}1", "34.8k, 0.1%", "A5", f"PRI_{ref}")
        passive(2, f"R{rn}2", "10.0k, 0.1%", f"PRI_{ref}", "GND")
    priority_mux("IC9", "SYS5", "B5", 90)
    priority_mux("IC8", "HOST5", "B_U4", 80)
    move("C31", 2)
    change("C81", 1, "HOST5")
    change("IC7", 3, "B_BRANCH_EN_H")
    change("IC7", 4, None)
    change("IC7", 6, "B_U4")
    change("C71", 1, "B_U4")
    passive(2, "R77", "100k", "B_BRANCH_EN_H", "GND")
    change("IC6", 5, "HOST5")
    change("IC6", 1, "UV6")
    passive(2, "R68", "24.9k, 1%", "HOST5", "UV6")
    passive(2, "R69", "10.0k, 1%", "UV6", "GND")
    change("IC6", 3, "J4_EFUSE_PG")
    change("IC6", 4, "J4_PGTH")
    passive(2, "R681", "28.0k, 0.1%", "U4_VBUS", "J4_PGTH")
    passive(2, "R682", "10.0k, 0.1%", "J4_PGTH", "GND")

    for ref, dp, dm, local_ok in (("IC10", "N_DP", "N_DM", "J2_PRESENT"),
                                  ("IC12", "HP_DP", "HP_DM", "J4_PATH_OK")):
        change(ref, 5, None)
        change(ref, 4, None)
        change(ref, 7, dp)
        change(ref, 6, dm)
        change(ref, 8, "GND")
        change(ref, 10, local_ok)

    def comparator(ref, sense, signal, bias):
        put(3, ref, "TLV3012BIDBVR",
            (1, "OUT", signal), (2, "V-", "GND"), (3, "IN+", sense),
            (4, "IN-", bias), (5, "REF", bias), (6, "V+", "3V3_AON"))

    comparator("IC11", "J2_PRESET", "J2_PRESENT", "J2_REF")
    passive(3, "R112", "23.7k, 0.1%", "U2_RAW", "J2_PRESET")
    passive(3, "R113", "10.0k, 0.1%", "J2_PRESET", "GND")
    passive(3, "R114", "100k", "J2_PRESENT", "GND")
    comparator("IC13", "J4_EFUSE_PG", "J4_PATH_OK", "J4_REF")
    passive(3, "R131", "47.5k, 1%", "3V3_AON", "J4_EFUSE_PG")
    passive(3, "R134", "100k", "J4_PATH_OK", "GND")

    put(3, "IC14", "SN74LVC1G175DBVR",
        (1, "CLK", "U2_AUTH_H"), (2, "GND", "GND"), (3, "D", "3V3_AON"),
        (4, "Q", "AUTH_LATCHED"), (5, "VCC", "3V3_AON"), (6, "CLR_N", "J2_PRESENT"))
    put(3, "IC15", "SN74LVC1G08DBVR",
        (1, "A", "AUTH_LATCHED"), (2, "B", "U2_AUTH_H"), (3, "GND", "GND"),
        (4, "Y", "B_BRANCH_EN_H"), (5, "VCC", "3V3_AON"))
    for ref in ("C141", "C151"):
        passive(3, ref, "100nF", "3V3_AON", "GND")
    passive(3, "R141", "100k", "U2_AUTH_H", "GND")
    change("M1", "P1.9", "U2_AUTH_H", "GP8 U2 host authorization")
    change("M1", "P1.10", None, "GP9")
    change("M1", "P2.2", "J2_PRESENT", "GP18 physical J2 present")
    change("M1", "P2.4", None, "GP20")
    change("M1", "P2.5", None, "GP21")
    put(4, "J5", "3.3 V SPI microSD breakout, exact fit open",
        (2, "Card CS", "SD_CS"), (3, "Card CMD / DI", "SD_MOSI"), (4, "VDD", "3V3_AON"),
        (5, "Card CLK", "SD_CLK"), (6, "GND", "GND"), (7, "Card DAT0 / DO", "SD_MISO"))
    return original, [row for ref in sorted(parts) for row in parts[ref]]


def save(name, fieldnames, rows):
    with (BASE/name).open("w", newline="") as file:
        writer = csv.writer(file, lineterminator="\n")
        writer.writerow(fieldnames)
        writer.writerows(rows)


def main():
    original, rows = build()
    refs = {r.reference for r in rows}
    nets = defaultdict(set)
    for row in rows:
        nets[row.net].add((row.reference, row.pin))
    assert all(len(pins) > 1 for net, pins in nets.items() if net != "NC")
    assert nets["U1_RAW"] == {("J1", "1"), ("C10", "1"), ("D10", "1"),
                              ("IC1", "5"), ("R12", "1")}
    assert ("IC4", "5") in nets["A5"] and ("IC4", "5") not in nets["SYS5"]
    assert ("D10", "2") in nets["GND"]
    assert nets["J2_PRESENT"] == {("IC11", "1"), ("IC10", "10"), ("R114", "1"),
                                   ("IC14", "6"), ("M1", "P2.2")}
    assert nets["J4_PATH_OK"] == {("IC13", "1"), ("IC12", "10"), ("R134", "1")}
    assert nets["J4_PGTH"] == {("IC6", "4"), ("R681", "2"), ("R682", "1")}
    for n in (10, 12):
        assert all((f"IC{n}", str(pin)) in nets["NC"] for pin in (4, 5))
        assert (f"IC{n}", "8") in nets["GND"]
    assert nets["B_BRANCH_EN_H"] == {("IC15", "4"), ("IC7", "3"), ("R77", "1")}
    assert nets["B_U4"] == {("IC7", "6"), ("IC8", "6"), ("C71", "1")}
    assert nets["HOST5"] == {("IC8", "2,7"), ("IC6", "5"), ("C81", "1"), ("R68", "1")}
    assert ("IC9", "6") in nets["B5"] and ("IC4", "5") not in nets["B5"]
    assert ("M1", "T-C.VBUS") in nets["SYS5"] and ("J2", "1") not in nets["SYS5"]
    assert all(("J3", str(pin)) in nets["NC"] for pin in (2, 3))
    assert not (set(nets) & {"EN6", "EN7", "U4_B_FAULT_N", "U4_B_GRANT", "U4_PORT_EN",
                             "NUSB_OE_N", "HUSB_OE_N"})
    save("pin-netlist.csv", FIELDS, sorted(rows, key=lambda r: (r.sheet,r.reference,r.pin)))
    save("pinout.csv", FIELDS, (r for r in rows if r.reference == "M1" or r.reference.startswith("J")))
    bom = {(r.reference, r.sheet, r.value) for r in rows}
    assert len(bom) == len(refs)
    save("bom.csv", ("reference", "sheet", "part_or_value"), sorted(bom))

    def records(source):
        d = defaultdict(list)
        for r in source:
            d[r.reference].append((r.value, r.sheet, r.pin, r.function, r.net))
        return {ref: sorted(items) for ref, items in d.items()}

    before, after = records(original), records(rows)
    changes = []
    for ref in sorted(before.keys() | after.keys()):
        classification = ("ADD" if ref not in before else "DROP" if ref not in after
                          else "KEEP" if before[ref] == after[ref] else "CHANGE")
        changes.append((ref, classification))
    save("bom-delta.csv", ("reference", "disposition"), changes)
    print(f"PASS {len(before)} -> {len(bom)} refs, {len(rows)} pin groups, {len(nets)-1} named nets.")
    print(f"PASS U1-only U3, separated SYS5 and gated B-to-U4, stock VBUS lands isolated from J2.")


if __name__ == "__main__":
    main()
