import csv

with open("pin-netlist.csv", newline="") as file:
    pins = {(row["reference"], row["pin"]): row["net"] for row in csv.DictReader(file)}
with open("bom.csv", newline="") as file:
    bom = {row["reference"]: row["part_or_value"] for row in csv.DictReader(file)}

assert len(bom) == 54
assert {ref for ref in bom if ref.startswith("IC")} == {"IC1", "IC2", "IC4", "IC6", "IC9"}
removed = {"C161", "R162", "D101", "D121", "IC7", "IC8", *(f"IC{i}" for i in range(10, 17))}
assert removed.isdisjoint(set(bom) | {ref for ref, _ in pins})

expected = {
    "IC1": {"5": "U1_RAW", "6": "A5"},
    "IC2": {"5": "U2_RAW", "6": "B5"},
    "IC4": {"5": "A5", "6": "U3_VBUS"},
    "IC6": {"5": "A5", "6": "U4_VBUS"},
    "IC9": {"3": "B5", "5": "B5", "6": "A5", "2,7": "SYS5"},
    "R901": {"1": "B5", "2": "PRI_IC9"},
    "R902": {"1": "PRI_IC9", "2": "GND"},
    "J2": {"1": "U2_RAW", "2": "N_DM", "3": "N_DP"},
    "J3": {"1": "U3_VBUS", "2": "NC", "3": "NC"},
    "J4": {"1": "U4_VBUS", "2": "H_DM", "3": "H_DP"},
    "J8": {"1": "GND", "2": "RGB_DATA", "3": "A5"},
    "R81": {"1": "RGB_A", "2": "RGB_DATA"},
    "M1": {"P2.1": "RGB_A", "P1.23": "SYS5", "T-C.VBUS": "SYS5", "T-C.A6,B6": "N_DP", "T-C.A7,B7": "N_DM",
           "P1.15": "PIO_DP", "P1.16": "PIO_DM"},
}
for reference, terminals in expected.items():
    for terminal, net in terminals.items():
        assert pins[reference, terminal] == net, (reference, terminal, net)
assert bom["R81"] == "100R"
assert {pin for pin, net in pins.items() if net == "RGB_A"} == {("R81", "1"), ("M1", "P2.1")}
assert {pin for pin, net in pins.items() if net == "RGB_DATA"} == {("R81", "2"), ("J8", "2")}
assert {ref for (ref, _), net in pins.items() if net == "B5"} == {"C22", "IC2", "IC9", "R901"}
print("PASS exact BOM, power terminals, USB connector connections and sole RGB signal path")
