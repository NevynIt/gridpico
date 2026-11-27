"""Screening arithmetic for the disclosed design assumptions, in SI units."""
idle, local, port_a, port_b, power_only = .070, .180, .500, .200, .300
source = 5.10
efuse, mux, b_switch = .055, .075, .135
inlet, outlet, cable = .020, .040, .100
host_a = efuse + mux + efuse + outlet
host_b = b_switch + mux + efuse + outlet
r_in = efuse + inlet
a4 = source - (3*local)*r_in - (2*local)*host_a
b2 = a4 - (2*local)*cable
b4 = b2 - (2*local)*r_in - local*host_b
c2 = b4 - local*cable
c5 = c2 - local*(r_in + mux + inlet)
u3 = source - (local+power_only+port_a)*r_in - power_only*(efuse+outlet)
u4_a_full = source - (local+power_only+port_a)*r_in - port_a*host_a
b4_loss = source-b4
r = 105
ilim_b_min = 25.230/(r*1.01)**1.016
ilim_b_typ = 23.950/r**.977
ilim_b_max = 22.980/(r*.99)**.94
dvd_cap_min_pf = 10000*.95
sr_upper = 2000/dvd_cap_min_pf*3.82/2.21 # nominal law scaled by source-current corner
ramp_upper_ma = sr_upper * 180
assert port_b < ilim_b_min < ilim_b_typ < ilim_b_max
assert local + ilim_b_max < .500 and 2*local < .425
assert a4 > 4.75 and b4 > 4.75
assert 4.75-b4_loss < 4.75
print(f"Local envelopes: always-on {idle*1000:.0f} mA, active {local*1000:.0f} mA")
print(f"A peak continuous plan: U3 {power_only*1000:.0f} + U4 {port_a*1000:.0f} + local {local*1000:.0f} = {(local+power_only+port_a)*1000:.0f} mA")
print(f"A concurrent full plan from {source:.2f} V: U3 {u3:.3f} V, U4 {u4_a_full:.3f} V")
print(f"B limiter screening from TPS2553 fit at 105k +/-1%: {ilim_b_min*1000:.1f}/{ilim_b_typ*1000:.1f}/{ilim_b_max*1000:.1f} mA min/typ/max")
print(f"Three units: head U1 {source:.2f} V -> head U4 {a4:.3f} V -> mid U2 {b2:.3f} V")
print(f"             -> mid U4 {b4:.3f} V -> tail U2 {c2:.3f} V -> tail SYS5 {c5:.3f} V")
print(f"Mid U4 static headroom over 4.75 V: {(b4-4.75)*1000:.1f} mV")
print(f"Head minimum for mid U4 >= 4.75 V in this model: {4.75+b4_loss:.3f} V")
print(f"Mid U4 with head U1 4.75 V: {4.75-b4_loss:.3f} V")
print(f"U4 10nF ramp with 180uF, nominal: 36.0 mA at 0.20 V/ms")
print(f"U4 conservative screening ramp corner: {ramp_upper_ma:.1f} mA at {sr_upper:.3f} V/ms, verify by measurement")
