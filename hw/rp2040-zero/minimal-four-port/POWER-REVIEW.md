# Minimal four-port with U1-only outlets

**Paper candidate. Powered fit and use remain held.** Product Brief
`fe51cd03d2e519e5c87b7c34a9cc36f6c913b7b1`, exact blob
`6609e47237007a7705f816512f8b856685aaeeff`.
[Published decision](https://github.com/NevynIt/gridpico/blob/fe51cd03d2e519e5c87b7c34a9cc36f6c913b7b1/PRODUCT_BRIEF.md#L50-L53)

## Paths and source policy

- **U1** feeds IC1 → A5. A5 alone feeds IC4 → U3, IC6 → U4, and the
  load-only RGB header J8. A5 also feeds IC9 VIN2 for SYS5 fallback.
  D10 shunts raw U1 VBUS to GND with its band/cathode on U1_RAW.
- **U2** feeds IC2 → B5. B5 feeds IC9 VIN1, MODE and the PR1 divider.
  SYS5 feeds the RP2040-Zero, which supplies SD and OLED from its own
  3.3 V rail. There is no tie or forwarding switch from B5 to A5.
  IC7 and its four support components are removed. IC2 remains a
  connector reverse-current and fault barrier. Its ~507 mA nominal
  breaker setting is separate from the USB operating current budget.
- **IC9** is a TPS2116DRLR wired for U2 priority. B5 sets MODE and
  R901/R902 feed PR1. The threshold screens at 3.818–4.503 V over the
  datasheet reference/leakage and fitted resistor tolerances. At loaded
  B5 above the worst-case upper bound the module prefers U2. Qualify an
  actual margin above that bound. If B5 becomes unavailable while A5 is
  healthy, IC9 selects A5. Each rail stays within the mux's recommended
  1.6–5.5 V specifications when used. The module has a narrower useful
  input envelope for a regulated 3.3 V output. Under weak or slow B5
  alone, hold startup near threshold because an alternate A5 may be
  absent. Near-threshold dwell, transfer droop and reset remain open.
  IC9 switches break-before-make and can delay selecting a lower input
  until SYS5 has fallen. C12/C22 are the A5/B5 input bypass reservoirs.
  Place them close to the mux and the associated input eFuses, with
  effective capacitance qualified. [TI TPS2116, §§6.5, 7.3–7.6, 9–10](https://www.ti.com/lit/ds/symlink/tps2116.pdf)

| Usable U1 | Usable U2 | SYS5 and local loads | U3, U4 and RGB supply |
| --- | --- | --- | --- |
| No | No | Off after retained charge drains. External signal injection is outside this nominal case. | Off after retained charge drains. |
| Yes | No | U1 → IC1 → A5 → IC9 VIN2. | U1 only. |
| No | Yes | U2 → IC2 → B5 → IC9 VIN1. | Off at the supply pins. Direct U4 D± and GP17 can still drive attached inputs and need injection checks. |
| Yes | Yes | U2 → IC2 → B5 → IC9 VIN1 while B5 meets the priority threshold. | U1 only. |

These are steady-state source-availability cases. **Cable unplug and
VBUS absent mean different things.** The mux has no connector-presence
input. An unplugged U2 normally leaves B5 unavailable after discharge.
A cabled sleeping, off, current-limited or faulted PC, a marginal supply,
or local IC2 cutoff can also leave B5 unavailable. The mux falls back from
either condition. A cabled USB link can also suspend with VBUS valid,
keeping U2 selected. B5 selection cannot establish USB attach, suspend,
current permission or physical unplug. Direct J2 D± can therefore remain
connected and driven from U1 while the PC VBUS is absent. RP2040 ROM
overrides VBUS detection and enables the pull-up in BOOTSEL. The recorded
Pico7 firmware also forces VBUS present. Hold this cabled-off/invalid
case and ordinary attach/detach until qualified. A policy that permits U1
fallback only after physical unplug would require a separate way to
establish connector state. No such dependency is drawn into the mux.
[RP2040 boot ROM source](https://github.com/raspberrypi/pico-bootrom-rp2040/blob/ef22cd8ede5bc007f81d7f2416b48db90f313434/usb_device_tiny/usb_device.c#L1313-L1320)

## Reverse current and fault paths

| Case | Path and remaining hold |
| --- | --- |
| Healthy source versus other input cabled at zero or unplugged | IC9 separates A5 and B5 with single-channel selection and reverse-current blocking. IC1 and IC2 separately block toward the raw connectors. Verify transient/steady VBUS leakage at both connectors, high-Z and forced-off. Datasheet reverse leakage and comparator timing are specified under particular conditions. |
| Both inputs healthy but mismatched, rising or falling | IC9 switches between sources without deliberate overlap. Local charge can keep the old rail high. Capture both connector currents and SYS5/3.3 V during both connection orders, live removal, weak/slow and fast collapse, recovery and handback. Check reverse delay, common-ground return and any restart/chatter. |
| U1 or A5 fault while U2 remains healthy | The U1 path and the three outlet supplies can drop. The U2 mux leg can sustain the module. U2 has no intended VBUS feed to the A5 short or any outlets. The direct GP14/15 data path remains a different exposure. |
| U2 or B5 fault while U1 remains healthy | IC2 is the local fault/reverse barrier. U1 may take SYS5 via VIN2 even with the PC cable attached. Verify J2 VBUS reverse, native D± state, actual source cutoff, attach and BOOTSEL. The power switch cannot open D±. |
| Raw J1 or J2 short | The fault is before the carrier eFuse and depends on the exact external supply/cable protection. Check the other source's reverse and data paths. |
| U3 or U4 short / externally driven VBUS | IC4/IC6 retain independent reverse and fault protection. Hard-short fast trip, recovery into current limit and persistent-overload latch are distinct. Check actual trip order, loss of A5 and local cap energy. A supplied downstream U4 D± pin can inject despite an open VBUS path. At the minimum source voltage, allow for all positive drops before claiming usable J4 voltage. |
| RGB short or externally powered harness | J8 takes A5 behind IC1. Its load and fault share the U1 breaker with U3, U4 and fallback SYS5. A short can collapse all A5 loads. There is no branch fuse or reverse element between J8 and A5. Fit only a load-only harness. A forced supply on J8 could energize A5 and the other outlets; keep external supply or bidirectional data at J8 outside the fitted mode. |
| U2 only with U4 peripherals or an RGB harness attached | U4 VBUS and J8's 5 V supply have no U2 feed. GP14/15 can still inject through direct USB D±, depending on peer and software state. GP17 can also drive an unpowered RGB input whenever the module remains on. A clamp in the fitted RGB load can then draw current from the Pico and may raise the load's supply rail. The 100 Ω series resistor provides no off-state isolation. Measure ghost voltages, off-state currents and ramp behavior with actual peers and leads before fitting. |

[TI TPS25947, §§7.3.5, 7.3.7 and 7.3.11](https://www.ti.com/lit/ds/symlink/tps25947.pdf)
· [TI TPS2116 reverse and transfer behavior](https://www.ti.com/lit/ds/symlink/tps2116.pdf)

## U2 current and fitted SD/OLED

The J2 budget covers the complete module and regulator, onboard pixel,
SD/OLED, B5 divider (~0.12 mA at 5 V), eFuse/mux and other leakage,
plus startup. U2 supplies no outlet VBUS load. Any current exported
through GP17 when U2 supplies the module counts toward the J2 budget.
Measure J2 at the actual connector and separately verify the module's
3.3 V output and thermal headroom.

| Condition | Paper limit or result |
| --- | --- |
| Default and configured | Standard USB 2.0 default budget is one 100 mA unit load. The recorded Pico7 image advertises 100 mA bus powered in configuration. The earlier 125 mA local estimate would already exceed it by 25 mA before upstream overhead. It is an illustrative rejection screen. A hardware trip near 507 mA cannot establish valid USB admission. |
| Attach, reset, BOOTSEL and source transfer | Measure the fitted module and actual accessories including SD startup/write, OLED startup/brightness, inherited IC2 ramp, C31, internal caps and hot transfer. The mux uses soft start below its output threshold. A live transfer can bypass startup slew. Verify actual ROM draw and inrush separately from the normal descriptor. |
| Electrical suspend | USB-IF specifies 2.5 mA maximum after the suspend window. With valid B5 the module continues drawing from U2. The legacy MIDI suspend fixture checked service state and provides no electrical proof. A failing actual suspend or reset envelope requires a supported-mode or power design decision before powered use. |
| Module 3.3 V | Waveshare's reference shows RT9013-33 and 27 Ω native USB series resistors. Confirm the actual board revision/regulator. The vendor's headline 500 mA is a component rating, separate from board thermal/dropout margin and USB current. At a 5 V input, a 3.3 V linear regulator passing 125 mA dissipates about 213 mW before quiescent loss. Check exact SD card/breakout, OLED, pulls/capacitance, module/LED use and ambient conditions. |
| Mux/source envelope | IC2's inherited OVLO screen is 5.612–5.834 V, above IC9's 5.50 V recommended ceiling. Both U1 and U2 need a qualified voltage/transient envelope at the mux. Measure input cable drop, fixed load and transients. |

[USB-IF current and inrush guidance](https://compliance.usb.org/index.asp?UpdateFile=Electrical)
· [Exact installed Pico7 configuration evidence](/tmp/gridpico-rev-a-firmware-evidence.0IuPzz/RESULT.md)
· [Waveshare module schematic](https://files.waveshare.com/upload/4/4c/RP2040_Zero.pdf)
· [Richtek RT9013 ratings](https://www.richtek.com/en/Products/Linear%20Regulator/Single%20Output%20Linear%20Regulator/RT9013.aspx)
· [Arithmetic](verification/power-screen.txt)

## RGB and direct USB interfaces

J8 pin 1 is common GND. Pin 2 is nominal 3.3 V data directly from
RP2040 GP17 (M1.P2.1) through existing 100 Ω R81. Pin 3 is U1-only
A5. The keyed, load-only harness, strip protocol, supply range,
peak current and connector fit remain open. The strip data input must
accept the GPIO's guaranteed output levels with margin at the actual
5 V strip supply, lead impedance and temperature. Raspberry Pi lists
a 2.62 V minimum output high at 3.3 V IOVDD and its stated drive/load
conditions. A strip requiring 0.7 × VDD, for example, would need 3.5 V
at VDD = 5.0 V, above even the nominal 3.3 V GPIO level. That threshold
is an illustration. No exact strip is currently selected. Fit is held
until the actual manufacturer's input conditions and waveform margin
are known. Keep the connector data lead within RP2040 pin limits, including
on an unpowered module. The 5 V contact supplies the strip only.
[Raspberry Pi RP2040 datasheet, §5.5.3](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)

Jill's direct-data choice removes IC16, C161 and R162. J8 data can
remain active with U1 off and the Pico running from U2. The fitted RGB
input must tolerate that condition without unacceptable GPIO injection,
phantom power or current above the J2 budget. A5 decay, reverse injection
from the actual harness, source startup and shutdown need qualification.
This is a separate RGB fit/use hold, independent of SYS5 selection and
U1-only outlet supply routing.

J2 native data goes to the original module signal lands with the stock
27 Ω resistors. Physically isolate the original module receptacle/VBUS
lands from raw J2 VBUS and verify both Type-C orientations and both Rd
on the actual breakout. J4 GP14/15 run directly through 22 Ω series and
15 kΩ host pulldowns. Qualify exactly one effective pull-down profile;
the recorded firmware's weak PIO pulls are not the selected profile.
There are no carrier USB data gates or clamps. Data-line ESD, VBUS-to-
data faults and powered-off injection defer to actual module and peers.
No powered or cascade claim transfers from the Protected or two-port
review.
