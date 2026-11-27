# Four-port simplification · electrical disposition

**Paper candidate.** Comparison basis is the electrical design published at
`0b431a9b7a42368096b0fd366d12a1263b321cae`. U1 remains a dedicated 5 V input, U2 a native USB Device connection
with a protected input path, U3 a U1-only output, and U4 a protected PIO Host
port usable on U1 or on explicitly authorized U2. Keep independent laptop,
adapter and instrument protection. A reset, a fresh attach after source
interruption and manual fault recovery are acceptable DEV tradeoffs.
The exact module, ports, hardware and supported power profiles remain held.

## Wide cut review

| Published block | DEV decision | Reason and minimum remaining protection |
| --- | --- | --- |
| D10 across U1_RAW | **CHANGE drawing.** Show a unidirectional shunt TVS, K/band to U1_RAW, A to GND. | It is reverse-biased at 5 V. The Littelfuse SMF6.0A example is 6 V standoff, 6.67–7.37 V breakdown and 10.3 V rated clamp. It absorbs specified transients. The eFuse sets DC cutoff. Retain the footprint and check pulse energy and actual source/cable. Exact vendor/fitted part remains open. |
| IC1 and IC2 | **KEEP protection; simplify** each UVLO/OVLO network to three series resistors. | Both inputs need independent short/inrush and reverse isolation. Revise IC2 OVLO to tolerate 5.50 V USB input in the static screen. Keep high-line surge/overshoot and recovery holds. Its eFuse threshold cannot grant upstream USB current. |
| SJ1 and proposed A5/B5 tie | **DROP SJ1; change** to an automatic A-priority TPS2116 for SYS5. **Reject** a direct tie with the present U3 tap. | Both eFuses provide ideal-diode reverse blocking, but joined outputs make B5 and the U3 A5 tap the same rail. A-priority avoids silently loading a higher-voltage laptop while the adapter is healthy. B fallback feeds the module and qualified local loads only, pending J2 attach/ROM/suspend qualification. |
| IC4 and J3 | **KEEP** U1-only protected branch and open data pins. | Feeding the branch from SYS5 or any joined A/B rail would let U2 power a U1-only outlet. Latch-off, reverse isolation and short protection remain. |
| Q70A/B, R701–704, EN7 and branch-fault readback | **DROP** the discrete driver, old EN7 network and GP21 fault reporting. **KEEP** one direct, current-attachment authorization for the B-only path and its current limit. | IC7 logic EN accepts 3.3 V. A pull-down and a small asynchronous-clear latch prevent an old high command from granting a new U2 attachment while U1 keeps the Pico alive. A falling GP8 independently withdraws permission on suspend or budget loss. IC7 stays off until a fresh valid GP8 rise. Drop the summary fault output; local latch-off remains. |
| Q60A/B, R601–604, EN6 and U4_PORT_EN | **DROP** the discrete final enable, separate GP9 veto and remote readback. | The final IC6 breaker has a local HOST5 UVLO and remains automatically enabled for an eligible source. A5 can supply U4 independently of the Pico. Authorized B alone can reach the U4 mux. Removing the user-controlled U4 off/reset function trades software recovery for the part's own trip behavior and deliberate source power cycling after a latched fault. |
| IC8 manual U4 source selection and U4_B_GRANT | **CHANGE** IC8 to independent automatic A-priority selection of A5 or the physically admitted B-only branch. | Reuse of the SYS5 output for U4 would defeat the B-only guard at A loss. The simpler hardware retains two isolated load paths and one admission signal. GPIO never selects a mux input. |
| IC10 / native data | **KEEP** powered-off data isolation. | With U1 present, native PHY/ROM may otherwise present D+ to a laptop without actual J2 VBUS. U2 VBUS isolation alone does not stop data-pin drive. The actual raw J2 connector sense qualifies the path without app involvement. |
| IC11 / native sense; IC26 and NUSB_OE_N | **CHANGE** to a positive J2 comparator with integrated reference/POR. **DROP** IC26 and the old active-low command/long dwell chain. | The positive J2 signal selects only the connected D2 switch channel, clears stale admission when low, and is a physical sense input for future app integration. Use a fail-safe input on a rail that can remain off while J2 is live. |
| IC12 and IC13 / PIO data, HUSB_OE_N | **KEEP** powered-off data isolation. **CHANGE** to IC6 PGTH/PG and one local PG qualifier. **DROP** the separate connector monitor, long dwell and remote active-low control. | With VBUS denied or latched off, a powered Pico can drive an unpowered instrument. A separately powered device can also drive unpowered Pico GPIO. Qualify the eFuse's actual local PG and J4 VBUS before connecting data. IC6's unpowered PG clamp can reach 1 V, above the mux's 0.77 V minimum high. The local comparator must qualify PG before SEL. Force-output and reverse-blocking PG behavior remains a hold. |
| D101 / D121 | **KEEP** low-capacitance connector-side ESD shunts for unrestricted attach of valued laptop/instruments. | Switch OVP/powered-off ratings do not establish board transient immunity. The stock drawing shows no external connector clamp. With controlled cables and ESD exposure Jill can review DNP for local DEV-only risk. It is outside this default fit and does not close cable surges, 5 V faults or cross-port transient effects. |
| SD, OLED, UART and RGB | **KEEP** their short 3.3 V interfaces. **DROP** duplicate carrier resistors and bypass where the fitted breakouts already provide what the peripheral requires. | J5 now needs a qualified 3.3 V SPI breakout with card-side pulls and local bypass. J6 needs qualified onboard 3.3 V I2C pulls. Keep clock damping and the UART/RGB interface. An unidentified or incomplete breakout stays out. Rail thermal and current fit gates remain. |

[D10 manufacturer example](https://www.littelfuse.com/assetdocs/tvs-diodes-smf-datasheet?assetguid=7eb8a5b6-bdd0-4561-8f19-0c3cc6f9b2af)
· [Input and output eFuse](https://www.ti.com/lit/ds/symlink/tps25947.pdf)
· [U2 branch switch](https://www.ti.com/lit/ds/symlink/tps2553.pdf)
· [Priority mux](https://www.ti.com/lit/ds/symlink/tps2116.pdf)
· [Fail-safe comparator](https://www.ti.com/lit/ds/symlink/tlv3011.pdf)
· [USB data switch](https://www.ti.com/lit/ds/symlink/tmuxhs221f.pdf)
· [Connector ESD shunt](https://www.ti.com/lit/ds/symlink/tpd2e2u06-q1.pdf)

## A5/B5 tie and four source states

IC1/IC2 use the exact TPS259474LRPWR. TI §7.3.7 describes linear ideal-diode
ORing, integrated back-to-back FETs and a fast reverse comparator. The spec
shows typical 4.86 µA OUT leakage with IN at 0 V and OUT at 12 V, typical
234 µA at OUT = IN + 1 V in enabled reverse-blocking, and typical 1 µs
fast comparator response at its stated overdrive. These entries show finite
leakage and transients under test conditions. They are not upper bounds for
the exact 5 V assembly or attached floating connectors. One eFuse being off,
in reverse blocking, disabled or unpowered separates its **input** connector
from its **output**. It does not split the two wires tied at its output.

| Sources allowed at their connectors | A5/B5 hard tie with present U3 tap | Candidate separate paths |
| --- | --- | --- |
| Both absent/off | No forward supply. Check charged/externally driven J3/J4 and data leakage into a floating USB input. | U3/U4 eFuses and powered-off data muxes contain both external outlets. Confirm measured static/transient backfeed. |
| U1 only, U2 absent or cabled with VBUS off | IC2's reverse block protects J2 VBUS subject to leakage/transient qualification. J2 data remains a separate potential source of drive under U1. | A supplies SYS5, U3 and U4. J2 data isolated without physical J2 VBUS. |
| U2 only, U1 absent or off | IC1 blocks reverse to raw J1, while U2 powers the joined internal A5, thus U3. Base module, startup and any unguarded U4 load move to J2. | B may feed SYS5 within the qualified profile. U3 stays off. B reaches U4 only through the independently latched upstream authorization and B branch current limit. |
| Both on, either is higher, either sheds load or loses power | No deterministic preference or current sharing. Higher effective voltage tends to lead, so B can supply the U3 load and SYS5 even with U1 plugged in. Reverse block does not implement a USB current contract. | Each mux prioritizes valid A. U3 uses A only. After A loss, SYS5 may use B subject to its own qualification. J4 stays off on B without a valid retained B grant. Break-before-make droop and attachment loss are reviewed as fresh-session cases. |

The input eFuse's finite reverse current and the external connector condition
must both be tested in the eventual approved validation. A reverse-block
part reduces the path from the output to the raw source and does not provide
galvanic isolation. The four ports share ground.
[TI reverse blocking, tables and priority ORing examples](https://www.ti.com/lit/ds/symlink/tps25947.pdf)

## Native USB, PIO and exposure

The Waveshare RP2040-Zero drawing shows stock Type-C VBUS wired to VSYS and
the 5 V pad, two 5.1 kΩ Rd resistors, duplicate USB 2.0 data contacts, two
27 Ω series resistors to the dedicated RP2040 USB PHY and a 3.3 V RT9013-33.
It shows no distinct native VBUS-to-GPIO measurement, no series VBUS isolation
and no connector TVS. The Raspberry Pi RP2040 boot ROM source explicitly
forces the VBUS-detect override and enables the native pull-up in USB boot.
A carrier gate remains necessary to prevent local drive with J2 off while
U1 keeps the device powered. Preserve original 27 Ω resistors when using the native
data lands. Retain the published relocated J2 assumption. Original receptacle
absent, its VBUS lands tied to SYS5 and insulated. Confirm exact PCB/lands
before fit. A stock accessible receptacle with external SYS5 on P1.23 and
unbroken VBUS land ties SYS5 straight to a laptop, bypassing IC2. J2 needs
its own verified Rd and orientation wiring.
[Waveshare schematic](https://files.waveshare.com/upload/4/4c/RP2040_Zero.pdf)
· [RP2040 PHY and USB registers](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)
· [RP2040 boot ROM USB setup](https://github.com/raspberrypi/pico-bootrom-rp2040/blob/ef22cd8ede5bc007f81d7f2416b48db90f313434/usb_device_tiny/usb_device.c#L1313-L1320)
· [USB-IF upstream back-voltage](https://www.usb.org/sites/default/files/USB-IFTestProc1_3.pdf)

GP14/15 are ordinary RP2040 GPIO and do not inherit the module USB-C CC
network, dedicated USB PHY or its termination. Keep 22 Ω series damping and
one effective host pull-down per line. The stock PIO library enables weak
pulls. Disable them if qualified external 15 kΩ parts are fitted, and confirm
full-speed electrical behavior and exact installed firmware. A 22 Ω resistor
alone does not isolate a powered or unpowered peer. The sheet shows the
external 15 kΩ profile with that firmware hold.
[PIO USB upstream implementation](https://github.com/sekigon-gonnoc/Pico-PIO-USB/blob/b186e36bea0f3bcc2ee94ca1fa81e7f71281443a/README.md)

## U2 authorization and fallback hold

The installed Pico7 declares 100 mA bus-powered. Existing firmware forces
native VBUS present, leaves suspend/resume callbacks weak, and has no GP8
admission policy. Mark U2-fed SYS5 and U2-funded U4 as held use profiles.
Native physical attach and BOOTSEL do not grant budget. J2 valid is a voltage
condition only. Future firmware would admit the exact current USB session
and drive GP8 high only after a budget sufficient for base plus ramp and
downstream draw. GP8 must drop on suspend/reset, loss, rejected configuration,
or release. The latch blocks an old high across a sensed J2 loss, while the
AND gate interrupts on GP8 low independently of the latched state. Scope
qualified J2 voltage loss versus short connector bounce, host resets that
retain VBUS, power ramp and brownout. Retire upstream and downstream owners
before restoring an eligible profile. Hardware alone cannot detect USB
suspend or bus reset while VBUS remains high. A B5 interruption with raw J2
still valid also does not clear the latch. That case requires proof that
the current grant persists legitimately through the fault, or that GP8
falls before the branch can re-energize. Add a qualified B5-loss clear if
the permitted source can recover before firmware reliably withdraws GP8.

Measure the complete J2 draw with no discretionary U4 load at cold attach,
both sources, loss of U1 while active or suspended, U2-only electrical
suspend, reset and BOOTSEL. Measure load and transient separately. USB 2.0
suspend permits 2.5 mA at J2. A stock RP2040-Zero with attached accessories
or ROM can exceed it. No default laptop U2-only or automatic fallback claim
until this passes with the exact module/image and fitted load. If it fails,
gate the base and accessory power to satisfy the actual bus state or retain
an explicit supported-mode hold. No hardware scheme shown claims to close
that dependency by merely limiting or switching U4.
[USB-IF Suspend Current Limit Changes](https://compliance.usb.org/index.asp?UpdateFile=Electrical)
· [Exact installed-image receipt](/tmp/gridpico-rev-a-firmware-evidence.0IuPzz/RESULT.md)

Both paths must pass ready voltage, reverse/leakage, downstream short and
inrush, off-port back-drive, latch-off/discharge/re-attach and permitted
current at the actual connectors. U2 operation at the low end of allowed
input cannot guarantee 4.75 V on a loaded U4 through passive drops. A named
adequate upstream source envelope, use limits or a separate regulated path
is required. The three-unit chain remains held.

## DEV simplification limit

The modifiable drawing cuts 17 carrier references and a net two active
packages. A controlled DEV cut can also leave the ESD footprints empty
after Jill accepts the exposed transient risk and restricts the setup.
The hardware gate on each exposed data pair remains in the design with
both independent sources, ROM access and a switched downstream rail.
Retiring the B admission circuit would require a distinct operating
restriction that never exposes high-load B on a default or suspended
upstream session. It cannot be inferred from USB-C, an eFuse setting or
normal enumeration. The paper design preserves either-source U4 and the
U1-only U3 function Jill asked to keep.

[Attachment latch](https://www.ti.com/lit/ds/symlink/sn74lvc1g175.pdf)
· [Independent GP8 low veto](https://www.ti.com/lit/ds/symlink/sn74lvc1g08.pdf)

## Current and fault review

The [source and fault matrix](CURRENT-FAULT.md) separates upstream connector
shorts, source-off leakage, reverse-fed downstream ports and local load
shorts. TI distinguishes persistent overload latch-off from the fast-trip
and foldback path for a hard short. A hard short can have further current
while the local part responds, and output capacitors discharge locally.
No selective trip order or unconditional one-shot cutoff is assumed.
[TPS25947 Rev. C, §§7.3.5 and 7.3.11](https://www.ti.com/lit/ds/symlink/tps25947.pdf)
