"""Reproduce paper thresholds. Voltages at named connector or supply nets."""

from itertools import product
from math import log


def resistor(value, tolerance=.001):
    return [value * (1 - tolerance), value * (1 + tolerance)]


def interval(label, values):
    print(f"{label:<27} {min(values):.3f} to {max(values):.3f} V")


def stacked(top, middle, bottom, ovlo):
    values = []
    for t, m, b, ref, en_i, ov_i in product(
        resistor(top), resistor(middle), resistor(bottom), (1.183, 1.223),
        (-.1e-6, .1e-6), (-.1e-6, .1e-6),
    ):
        if ovlo:
            below_uv = ref / b + ov_i
            values.append(ref + m * below_uv + t * (below_uv + en_i))
        else:
            below_uv = (ref + ov_i * b) / (m + b)
            values.append(ref + t * (below_uv + en_i))
    return values


def one_divider(top, bottom, vrefs, leak=(0,)):
    return [ref * (1 + t / b) + i * t for t, b, ref, i in product(
        resistor(top), resistor(bottom), vrefs, leak)]


def main():
    for name, top, middle in (("U1", 32400, 2870), ("U2", 34000, 3570)):
        interval(f"{name} UVLO rise", stacked(top, middle, 10000, False))
        interval(f"{name} OVLO rise", stacked(top, middle, 10000, True))
    assert min(stacked(34000, 3570, 10000, True)) > 5.50
    assert max(stacked(34000, 3570, 10000, True)) < 6.00
    interval("A mux PR1 boundary", one_divider(34800, 10000, (.92, 1.08)))
    interval("J4 PGTH rise", one_divider(28000, 10000, (1.183, 1.223), (-.1e-6, .3e-6)))
    interval("J4 PGTH fall", one_divider(28000, 10000, (1.076, 1.116), (-.1e-6, .3e-6)))
    interval("J2 comparator screen*", one_divider(23700, 10000, (1.198, 1.286)))
    print("* Use 1.208 to 1.276 V reference spread plus an explicit +/-10 mV screen for offset and hysteresis.")
    print("PG invalid up to 1.000 V under the TI test conditions. J4 PG qualifier screen is 1.198 to 1.286 V.")
    print("No proof of zero data drive during an unqualified supply ramp or forced external J4 VBUS.")

    # These are planning bounds, including a conservative switch resistance, not fitted-port measurements.
    current = .20
    resistance = .045 + .150 + .074 + .045
    drop = current * resistance
    print(f"B forwarding silicon drop {drop*1000:.1f} mV at {current*1000:.0f} mA before trace/contact drop.")
    for j2 in (4.75, 5.10):
        print(f"J2 {j2:.2f} V gives J4 {j2-drop:.3f} V before trace/contact drop or dynamic droop.")
    print("Inherited B switch minimum ~220.8 mA. 200 mA plus 37.3 mA prior ramp allowance and 2 mA exceeds it.")
    print("Prove limited startup or reduce the admitted steady load. This screen grants no USB current.")
    for ref, r in (("IC1/R11", 2670), ("IC2/R21", 6570), ("IC4/R41", 6570), ("IC6/R61", 5360)):
        print(f"{ref} nominal circuit breaker current by TI Eq. 5 = {3334/r:.3f} A.")
    minimum_energy, maximum_energy = (0.5 * c * 5.25**2 for c in (170e-6, 400e-6))
    print(f"Local J4 output capacitor energy at 5.25 V = {minimum_energy*1000:.2f} to {maximum_energy*1000:.2f} mJ.")
    minimum, maximum = (4700 * c * log(5.25 / .8) for c in (170e-6, 400e-6))
    print(f"R66 nominal unloaded J4 decay from 5.25 to 0.80 V is {minimum:.2f} to {maximum:.2f} seconds.")


if __name__ == "__main__":
    main()
