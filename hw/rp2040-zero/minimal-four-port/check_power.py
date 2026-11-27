"""Check source reachability through the drawn steady-state power paths."""

from collections import defaultdict
from itertools import product
import csv
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def reachable(start, edges):
    visited = {start}
    pending = [start]
    while pending:
        for target in edges[pending.pop()]:
            if target not in visited:
                visited.add(target)
                pending.append(target)
    return visited


def power_edges(nets, sel, short_tie=False):
    edges = defaultdict(set)
    for ref, inp, out in (("IC1", "5", "6"), ("IC2", "5", "6"),
                          ("IC4", "5", "6"), ("IC6", "5", "6")):
        edges[nets[ref, inp]].add(nets[ref, out])
    if sel is not None:
        edges[nets["IC9", sel]].add(nets["IC9", "2,7"])
    edges[nets["M1", "P1.23"]].add(nets["M1", "P1.21"])
    if short_tie:
        edges[nets["IC9", "3"]].add(nets["IC9", "6"])
    return edges


def check(fault=False):
    with (ROOT / "pin-netlist.csv").open(newline="") as stream:
        nets = {(row["reference"], row["pin"]): row["net"] for row in csv.DictReader(stream)}
    loads = {(ref, pin): nets[ref, pin] for ref, pin in
             (("M1", "P1.23"), ("J5", "4"), ("J6", "2"), ("J3", "1"), ("J4", "1"), ("J8", "3"))}
    outlets = {(ref, pin) for ref, pin in loads if ref in ("J3", "J4", "J8")}
    module = loads.keys() - outlets
    for a, b in product((False, True), repeat=2):
        select = "3" if b else "6" if a else None
        edges = power_edges(nets, select, short_tie=fault)
        from_a = reachable(nets["J1", "1"], edges) if a else set()
        from_b = reachable(nets["J2", "1"], edges) if b else set()
        actual_a = {pin for pin, net in loads.items() if net in from_a}
        actual_b = {pin for pin, net in loads.items() if net in from_b}
        assert actual_a == (outlets | (set() if b else module) if a else set())
        assert actual_b == (module if b else set()), "U2 reaches a U1-only outlet"
        assert nets["J2", "1"] not in from_a and nets["J1", "1"] not in from_b
        for ref, pin in (("J3", "1"), ("J4", "1")):
            reverse = reachable(nets[ref, pin], edges)
            assert "A5" not in reverse
        if not fault:
            print(f"PASS U1 {'on' if a else 'off'}, U2 {'on' if b else 'off'}"
                  f"  SYS5={'U2' if b else 'U1' if a else 'off'}"
                  f"  outlet power={'U1' if a else 'off'}")


def main():
    check()
    try:
        check(fault=True)
    except AssertionError as error:
        assert str(error) == "U2 reaches a U1-only outlet"
        print("PASS deliberate B5-to-A5 feed rejected")
    else:
        raise AssertionError("U2 outlet negative control passed")
    print("Static nominal reachability under ideal switch states only. Read POWER-REVIEW.md for leakage, ramps and faults.")


if __name__ == "__main__":
    main()
