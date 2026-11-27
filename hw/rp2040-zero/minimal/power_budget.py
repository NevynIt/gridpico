"""First-order electrical screens for the review carrier. Units are SI."""

from math import log


def main():
    source = 5.10
    input_resistance = 0.075  # 55 mΩ protector plus 20 mΩ inlet allowance
    host_a = 0.075 + 0.055 + 0.040  # mux, outlet breaker, traces/contact
    host_b = 0.135 + host_a  # B branch switch adds series resistance
    power_outlet = 0.055 + 0.040
    cable = 0.100  # loop resistance at each hop
    local = 0.125  # 120 mA at 3V3 plus 5 mA input-side design allowance

    total = local + 0.300 + 0.500
    a5 = source - total * input_resistance
    print(f"U1 full planning load {total:.3f} A: J3 {a5-0.300*power_outlet:.3f} V;"
          f" J4 {a5-0.500*host_a:.3f} V, including both outlets.")
    head_u4 = source - 3 * local * input_resistance - 2 * local * host_a
    middle_u2 = head_u4 - 2 * local * cable
    middle_u4 = middle_u2 - 2 * local * input_resistance - local * host_b
    tail_u2 = middle_u4 - local * cable
    print(f"Conditional chain from {source:.2f} V: A U4 {head_u4:.3f} V, B U2 {middle_u2:.3f} V,"
          f" B U4 {middle_u4:.3f} V, C U2 {tail_u2:.3f} V.")
    print(f"B U4 with 4.75 V at head U1: {middle_u4 - (source - 4.75):.3f} V.")
    print(f"B request screen: local + tail = {2*local*1000:.0f} mA. Installed request: 100 mA.")
    assert middle_u4 > 4.75
    assert middle_u4 - (source - 4.75) < 4.75

    # Fitted temperature, leakage and threshold/waveform tolerances still need evidence.
    cap_low, cap_high = 170e-6, 400e-6
    ramp_high = 2000 / (39000 * 0.95) * 3.82 / 2.21  # V/ms
    ramp_amps = cap_high * ramp_high * 1000
    nominal_drop = 5.25 * 10e-6 / (cap_low + 10e-6)
    off_delay = tuple(4_700 * cap * log(5.25 / 0.80) for cap in (cap_low, cap_high))
    print(f"U4 C62+C63: 170..400 uF effective design window. Fast-edge charge estimate {ramp_amps*1000:.1f} mA."
          f" Preconfig 100 mA + charging + 2 mA allowance: {(0.102+ramp_amps)*1000:.1f} mA.")
    print(f"Ideal 10 uF hot-plug share at 5.25 V: {nominal_drop*1000:.1f} mV out of 330 mV"
          " before ESR and source behavior.")
    print(f"R66-only discharge 5.25 to 0.80 V: {off_delay[0]:.2f}..{off_delay[1]:.2f} s"
          " nominal; reconnect needs actual clearance.")
    assert nominal_drop < 0.330
    assert ramp_amps + 0.102 < 0.2208

    # Reference package data is a planning screen, not the selected module's thermal evidence.
    for local_3v3 in (0.120, 0.180):
        loss = (5.25 - 3.30) * local_3v3
        estimated_junction = 45 + 250 * loss
        print(f"RT9013 screen at {local_3v3*1000:.0f} mA: {loss:.3f} W,"
              f" {estimated_junction:.1f} C at 45 C ambient and 250 C/W.")


if __name__ == "__main__":
    main()
