"""Minimal four-port pin model derived from the sealed Protected four-port design."""

from collections import defaultdict, namedtuple
import csv
from pathlib import Path

ROOT = Path(__file__).resolve().parent
FIELDS = ("sheet", "reference", "value", "pin", "function", "net")
Row = namedtuple("Row", FIELDS)
REMOVED = {
    "IC7", "IC8", "IC10", "IC11", "IC12", "IC13", "IC14", "IC15", "D101", "D121",
    "C71", "C72", "R75", "R77", "C81", "R801", "R802", "C101", "C111", "C121", "C131", "C141", "C151",
    "R112", "R113", "R114", "R131", "R134", "R141", "R681", "R682",
}
AUX_NETS = {
    "A5_B_U4", "B_U4", "HOST5", "PRI_IC8", "IL7", "B_BRANCH_EN_H", "J2_PRESENT",
    "J4_EFUSE_PG", "J4_PGTH", "J4_PATH_OK", "J2_REF", "J4_REF",
    "J2_PRESET", "AUTH_LATCHED", "U2_AUTH_H", "C2_DP", "C2_DM", "HP_DP", "HP_DM",
}


def load():
    with (ROOT / "baseline/protected-pin-netlist.csv").open(newline="") as stream:
        return [Row(**row) for row in csv.DictReader(stream)]


def build(original):
    changes = {
        ("IC9", "3"): ("B5", "VIN1 priority"),
        ("IC9", "5"): ("B5", "MODE"),
        ("IC9", "6"): ("A5", "VIN2 fallback"),
        ("R901", "1"): ("B5", ""),
        ("J8", "2"): ("RGB_DATA", "3.3 V DATA OUT"),
        ("IC6", "3"): ("NC", "PG"),
        ("IC6", "4"): ("GND", "PGTH"),
        ("M1", "P1.9"): ("NC", "GP8"),
        ("M1", "P2.2"): ("NC", "GP18"),
        ("J2", "2"): ("N_DM", "D-"),
        ("J2", "3"): ("N_DP", "D+"),
        ("R121", "2"): ("H_DP", ""),
        ("R122", "2"): ("H_DM", ""),
    }
    result = []
    for row in original:
        if row.reference in REMOVED:
            continue
        net = "A5" if row.net == "HOST5" else row.net
        net, function = changes.get((row.reference, row.pin), (net, row.function))
        value = {"R901": "31.6k, 0.1%", "J8": "5 V RGB load-only header"}.get(row.reference, row.value)
        sheet = "3" if row.reference == "J4" else row.sheet
        result.append(row._replace(net=net, function=function, value=value, sheet=sheet))
    result.append(Row("4", "J8", "5 V RGB load-only header", "3", "U1-only 5 V OUT", "A5"))
    return result


def verify(rows):
    nets = defaultdict(set)
    for row in rows:
        nets[row.net].add((row.reference, row.pin))
    assert len(rows) == sum(len(group) for group in nets.values())
    assert all(len(pins) >= 2 for net, pins in nets.items() if net != "NC")
    assert not AUX_NETS.intersection(nets)
    assert nets["A5"] == {
        ("IC1", "6"), ("C12", "1"), ("IC4", "5"), ("R44", "1"),
        ("IC6", "5"), ("R68", "1"), ("IC9", "6"),
        ("J8", "3"),
    }
    assert nets["B5"] == {("IC2", "6"), ("C22", "1"), ("IC9", "3"), ("IC9", "5"), ("R901", "1")}
    assert nets["RGB_A"] == {("M1", "P2.1"), ("R81", "1")}
    assert nets["RGB_DATA"] == {("R81", "2"), ("J8", "2")}
    assert ("J8", "1") in nets["GND"]
    assert nets["SYS5"] == {("IC9", "2,7"), ("C31", "1"),
                            ("M1", "P1.23"), ("M1", "T-C.VBUS")}
    assert nets["U3_VBUS"] == {("IC4", "6"), ("C42", "1"), ("R46", "1"), ("J3", "1")}
    assert nets["U4_VBUS"] == {("IC6", "6"), ("C62", "1"), ("C63", "1"),
                              ("R66", "1"), ("J4", "1")}
    assert nets["N_DP"] == {("J2", "3"), ("M1", "T-C.A6,B6")}
    assert nets["N_DM"] == {("J2", "2"), ("M1", "T-C.A7,B7")}
    assert nets["PIO_DP"] == {("M1", "P1.15"), ("R121", "1")}
    assert nets["PIO_DM"] == {("M1", "P1.16"), ("R122", "1")}
    assert nets["H_DP"] == {("J4", "3"), ("R121", "2"), ("R123", "1")}
    assert nets["H_DM"] == {("J4", "2"), ("R122", "2"), ("R124", "1")}
    assert {("J1", "2"), ("J1", "3"), ("J3", "2"), ("J3", "3")} <= nets["NC"]
    return len(nets) - 1


def save(name, header, rows):
    with (ROOT / name).open("w", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(header)
        writer.writerows(rows)


def by_ref(rows):
    parts = defaultdict(list)
    for row in rows:
        parts[row.reference].append(row)
    return {ref: sorted(pins) for ref, pins in parts.items()}


def main():
    old = load()
    new = build(old)
    nets = verify(new)
    protected, minimal = by_ref(old), by_ref(new)
    assert protected.keys() - minimal.keys() == REMOVED
    assert not minimal.keys() - protected.keys()
    save("pin-netlist.csv", FIELDS, sorted(new, key=lambda r: (r.sheet, r.reference, r.pin)))
    save("pinout.csv", FIELDS, (r for r in sorted(new) if r.reference == "M1" or r.reference.startswith("J")))
    save("bom.csv", ("reference", "sheet", "part_or_value"),
         sorted({(r.reference, r.sheet, r.value) for r in new}))
    save("bom-delta.csv", ("reference", "disposition"),
         [(ref, "DROP" if ref not in minimal else "CHANGE" if minimal[ref] != protected[ref] else "KEEP")
          for ref in sorted(protected)])
    print(f"PASS {len(protected)} → {len(minimal)} references. {len(new)} terminal groups, {nets} named nets.")
    print("PASS A5 feeds U3, U4 and RGB independently of B5. U2-priority SYS5 and both direct USB data pairs.")
    print("PASS four input/output breakers retained. J8: common GND, direct GP17 through R81, U1-only A5.")


if __name__ == "__main__":
    main()
