"""Reproduce selection bounds and explicitly illustrative load screens."""

from itertools import product


def limits(value):
    return (value * .999, value * 1.001)


def main():
    threshold = [
        ref * (1 + top / bottom) + leakage * top
        for top, bottom, ref, leakage in product(limits(31_600), limits(10_000),
                                                 (.92, 1.08), (-.1e-6, .1e-6))
    ]
    print(f"IC9 B5 select screen at steady state: {min(threshold):.3f} to {max(threshold):.3f} V.")
    print(f"PR1 divider nominal draw at B5 = 5.00 V: {5 / 41_600 * 1e6:.1f} µA.")
    print("At 4.75 V U2 example: 4.75 V - 125 mA × (45 + 60) mΩ = "
          f"{4.75 - .125 * (.045 + .060):.3f} V at SYS5 before cable/board loss or transients.")
    print("Prior 125 mA local use screen is 25 mA above the installed 100 mA declaration, before upstream overhead.")
    print("Illustrative LDO heating at 5.0 V to 3.3 V and 125 mA: "
          f"{(5 - 3.3) * .125 * 1000:.1f} mW before quiescent loss.")
    print("Nominal static A5 bleed through the U3/U4 enable dividers is 17.45 kΩ total.")
    for name, resistance in (("IC1", 2670), ("IC2", 6570), ("IC4", 6570), ("IC6", 5360)):
        print(f"{name} nominal breaker threshold: {3334 / resistance:.3f} A.")
    print("Example U1-only load: 300 mA U3 + 500 mA U4 + 125 mA local = 925 mA before RGB and inrush.")
    print("Nominal IC1 difference from this example: 324 mA before RGB and inrush, not an admitted ceiling.")


if __name__ == "__main__":
    main()
