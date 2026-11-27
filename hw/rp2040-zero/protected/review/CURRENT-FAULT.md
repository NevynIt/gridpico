# Source, current and fault trace for HAL

**Paper candidate based on `0b431a9b7a42368096b0fd366d12a1263b321cae`.**
The published baseline board generator, renderer, BOM, pin netlist and prior
holds are in [baseline](../electrical/baseline/). At `0b431a9` the electrical files are
byte-identical to `b0b6cd1`. The earlier differences are archival citations,
index and verification metadata.

## Connector and source states

A means usable positive DC after IC1, and B means usable positive DC after
IC2. A cabled source can still be off or faulted. This table covers passive
current path intent. It counts neither finite leakage nor switching
transients as a valid supply.

| A | B | B permission | SYS5 and module | J3/U3 | J4/U4 | Upstream source expectation |
| --- | --- | --- | --- | --- | --- | --- |
| Off | Off | Any stale logic | Off after retained charge clears | Off | Off | IC1/2 reverse block any retained internal rail voltage toward J1/J2. Check off-state voltage with both floating inputs and with both held at zero. Data muxes isolate with AON at zero. |
| On | Off | None | IC9 selects A5 | IC4 conducts from A5 | IC8 selects A5 and IC6 supplies J4 | IC2 blocks reverse to off or unplugged J2. J2 D± are isolated when J2_PRESENT is low. |
| Off | On | Denied, including default/current unknown/suspend | IC9 feeds B5 to the module subject to the separate fallback hold | Off | IC7 is disabled, so U4 has no B feed | J1 remains isolated. J2 supplies the base load, losses and divider current. The base load must itself satisfy zero-discretionary-load attach, reset and suspend limits. |
| Off | On | Separately admitted | IC9 feeds B5 to the module | Off | IC7 → IC8 → IC6 feeds J4 | J2 covers module, losses, N-device inrush and settled J4 within the actual grant. J1 is off with no B-to-A path. |
| On | On | Either | IC9 prefers A5 | IC4 conducts from A5 only | IC8 prefers A5. An admitted standby B branch may be eligible on A loss. | Healthy A is preferred even if J2 voltage is higher. Do not assume current sharing. B-side divider and quiescent load remain. |
| On → Off | On | Denied or suspended | IC9 can transfer to B5 immediately | Loses A5. IC4 blocks reverse if its port remains externally charged. | With B denied, IC8 has no eligible secondary source. IC6/PG turns off after its local conditions fall. | This is the highest-risk laptop fallback case for base SYS5. Capture J2 current, SYS5 and AON while suspended with actual J2 data/link state. |
| On → Off | On | Valid independent B grant | Module may transfer to B5 | Loses A5 | Can transfer to admitted B. If PG drops, the data pair opens. | Measure J2 current step and U4 droop. The same valid session needs sufficient grant for the new aggregate, including the local load that was on A. |
| On | On → Off or unplugged | Previous high may remain on GP8 | A remains the preferred module source | A continues | A can continue. B loses input. Raw J2 loss asynchronously clears permission. | Test live A and unpowered cabled laptop. A reset of J2 while raw VBUS stays high requires firmware grant retirement. |
| Off | On → Off or unplugged | Any | B5 and SYS5 decay | Off | The B branch loses supply. Charged J4 decays through R66 and attached load. | Reverse-block and discharge thresholds must be checked with actual capacitance and cable conditions. |

**Bare-tie rejection.** IC1 and IC2 ideal-diode action supports joining
isolated outputs for an ORing load. The currently protected A5 and B5
nodes would then become one net. With B as the only supply, current would
flow **J2 → IC2 → tied A5 → IC4 → J3**. It would also feed SYS5 and any
eligible U4 load. With both sources on, control responds to rail voltage
and drops, so it cannot guarantee A preference or withhold U2 demand before
authorization. TI shows finite typical reverse and OUT leakage and
specified reverse-comparator timing. Those data support the raw-input
boundary and do not establish zero reverse current at either assembled
connector. Test slow ramp, hot-plug and steady leakage independently.
[TPS25947 Rev. C, §§6.5–6.6, 7.3.7, 8.2.4](https://www.ti.com/lit/ds/symlink/tps25947.pdf)
· [TPS2116 Rev. A, §§7.3.4 and 7.6](https://www.ti.com/lit/ds/symlink/tps2116.pdf)

## Shorts, reverse feed and recovery

| Fault or action | Intended current path and protection | Qualification still needed |
| --- | --- | --- |
| Short at raw J1 VBUS before IC1, any J2 state | Raw J1 is on the source side of the eFuse. The dedicated adapter's own current protection covers that raw-side short. When B powers internal loads, it reaches neither raw J1 nor U3 through A5. | Exact adapter and captive/keyed cable. Verify it limits/cuts without overheating. Check B-to-J1 reverse leakage with J1 shorted and on release. |
| Short at raw J2 VBUS before IC2, any J1 state | The upstream USB port protects its own connector and cable side. On a live A rail, IC2 blocks internal reverse feed, and actual J2 voltage falls to disqualify native data and clear the B grant. | Check J2 source behavior and backdrive at VBUS, D+ and D− with the exact PC/cable. Confirm cleared admission on recovery. |
| Internal A5 short | IC1 protects the A charge/current path. U3 loses its only supply. IC9 and IC8 can use their eligible B inputs independently. | Check that the disabled A mux leg prevents B from forwarding into the A5 short. Base fallback and aggregate-B gates apply. |
| Internal B5 short | IC2 protects the B charge/current path. A may remain healthy. IC8 cannot supply its B input through IC7 under nominal source direction. | Raw J2 may stay valid while protected B5 collapses. Hold B recovery with GP8 high. Demonstrate a retained lawful grant or safe grant removal and fresh admission before any restart. |
| J3 short, A valid | A5 → IC4 supplies the initial fault current. Fast trip/foldback or persistent-overload response acts at J3. B cannot reach J3. | Test short already present at power-on and short introduced at run time. The actual input/output trip order and A5 disturbance remain open. |
| J3 short, A absent, with or without B | There is no forward B source into IC4/J3. The output capacitance and attached load can discharge locally. | Check B-only residual J3 voltage, both raw ports and initially charged load. |
| J4 short, A valid, B off/denied or admitted | A5 → IC8 → IC6 sees the load. IC6 must limit or cut the fault. A5 may droop or trip IC1. If A is lost, only already-admitted B can reach IC6. | No selectivity is claimed. Exercise both input states, including simultaneous grants and A droop. Prove a persistent fault does not become repeated cross-source reassignment in a supported mode. |
| J4 short, A absent, B authorized | J2 → IC2 → IC7 → IC8 → IC6 is the fault path. IC7 is the tighter B-side nominal current threshold; IC2, IC7 and IC6 can respond on different time scales. | Measure peak, duration and latch/thermal/foldback response. A 200 mA settled load with full startup overlap exceeds the prior B switch minimum screen. |
| J4 short, A absent, B denied or off | There is no nominal positive input feed to J4. R66, C62/C63 and any external source define residual outlet energy. | No energized port claim. Qualify leakage and cold start into the fault before any approval. |
| Externally drive J3 VBUS, A/B independently off or on | IC4 blocks an output high relative to its input. J3 D± are open. At near-equal voltages, characterize transient reverse exchange while both supplies source. | Measure J1, J2, A5, B5 and connector current with source-off, cabled-off, both-on and ramped external feed. The external source remains responsible for its own output short. |
| Externally drive J4 VBUS, A/B independently off or on | IC6 is the primary reverse barrier. In reverse-comparator blocking, TI Table 7-5 specifies PG low. When IC6 is unpowered and AON still live, its PG clamp can rise toward 1 V, so IC13 compares PG to its own ~1.24 V ref before selecting USB data. With AON zero, IC12 uses powered-off isolation. | Force J4 above HOST5 and around equal voltages at all source states. Observe actual PG, AON, D± and raw J1/J2. A driven outlet may remain above the PGTH threshold. Prove intended data behavior during the transition, clamp conditions, output foldback and brownout. |
| J4 VBUS short to D+ or D−, or charged/sourcing downstream data with J4 disabled | IC12's common-side OVP and powered-off isolation are independent of IC6's VBUS path. D121 is a local low-capacitance clamp. Pico USB signal pins and laptop native data remain on separate pairs. | Test duration, fault voltage and source limits, ESD layout and paired-port backdrive. TVS and mux ratings are not assembly qualification. |

The eFuse tables distinguish **persistent overcurrent** from **hard short**.
A TPS259474L can latch off after a circuit-breaker fault, but TI describes
hard-short fast trip followed by current limit/foldback, with thermal
shutdown possible. Open ITIMER minimizes persistent-overcurrent delay.
U4 output capacitance discharges into a local fault even when the supply
opens. The 170–400 µF interval at 5.25 V represents about 2.34–5.51 mJ
stored before wiring inductance and source delivery. Measure the outcome
at the actual port. [TPS25947 Rev. C, §§7.3.5 and 7.3.11](https://www.ti.com/lit/ds/symlink/tps25947.pdf)
· [TPS2553 latch and current limit](https://www.ti.com/lit/ds/symlink/tps2553.pdf)
· [TMUXHS221F powered-off isolation](https://www.ti.com/lit/ds/symlink/tmuxhs221f.pdf)

## Current settings and unverified conditions

| Element | Recorded proposed paper fit | What remains conditional |
| --- | --- | --- |
| IC1, TI TPS259474LRPWR | R11 2.67 kΩ. TI Eq. 5 gives ~1.25 A nominal. A load screen is 300 mA J3 + 500 mA J4 + 125 mA local before losses/ramp. | Exact adapter/current limit, cable, fitted thresholds, ripple and both ports ramping. Nominal trip does not establish usable current. |
| IC2, TI TPS259474LRPWR | R21 6.57 kΩ. ~0.507 A nominal. Datasheet's nearby 6.65 kΩ test point spans 0.425–0.575 A. | Exact fitted trip and timing. The fuse is far above a 100 mA USB session declaration and above the 2.5 mA electrical suspend ceiling. Higher U2 power needs a real authorization. |
| IC4, TI TPS259474LRPWR | R41 6.57 kΩ. ~0.507 A nominal. Candidate J3 load is 300 mA. | Actual J3 load and independently measured reverse current/short result. |
| IC6, TI TPS259474LRPWR | R61 5.36 kΩ. ~0.622 A nominal. C61 39 nF with R67 100 Ω. Baseline effective C62 interval 170–400 µF. | Fitted transient/steady limits, startup/inrush, output cap type and loss/reconnect. Reverse PG result is qualified only under the exact conditions. |
| IC7, TI TPS2553DBVR-1 | R75 105 kΩ. Inherited approximately 220.8–292.1 mA limits. | 200 mA steady ceiling candidate. Combined 200 mA + 37.3 mA inherited ramp + 2 mA allowance exceeds the lower threshold. A 175 mA steady paper screen with the same ramp terms leaves 6.5 mA. Establish actual load overlap, limit and upstream USB admission. |
| IC8/IC9, TI TPS2116DRLR | A5 on VIN1 and MODE, exact 34.8 kΩ/10.0 kΩ PR1 divider. Nominal threshold 4.48 V. The alternate is B_U4 or B5 respectively. | Confirm both-source rail settling, failover and 5.5 V recommended operating limit. The eFuse OVLO ceiling up to 5.834 V only screens against a 6 V absolute maximum. Choose a qualified input envelope or different parts if excursions violate recommended conditions. |
| IC11/IC13, TI TLV3012BIDBVR | J2 divider and IC6 PG conditioning with fail-safe inputs and low POR output. | POR sequencing with both data switches alive, real J2 discontinuity, short/drop and external J4 behavior. |
| IC10/IC12 and D101/D121 | TI TMUXHS221FRSWR and TPD2E2U06QDBZRQ1. The former has powered-off high-Z. The latter is the low-C connector shunt. | Exact protected layout, USB full-speed waveform and immunity. Exact module revision and original data-land wiring remain open. |
| IC14/15, TI SN74LVC1G175DBVR and SN74LVC1G08DBVR | Positive-edge D flip-flop and AND. J2_PRESENT low clears asynchronously. GP8 low independently disables IC7. | Rail ramp, true connector-loss capture, reset with VBUS still high, and protected B5 falling or recovering while RAW remains high. |
| Local module/accessories | Waveshare RP2040-Zero family, J5/J6 exact fit open. | Module regulator and actual SD/OLED peaks, load in ROM and actual USB suspend. Current Pico7 declares 100 mA bus-powered and cannot establish this B forwarding profile. |

[SN74LVC1G175 pin and clear function](https://www.ti.com/lit/ds/symlink/sn74lvc1g175.pdf)
· [SN74LVC1G08 pin and truth table](https://www.ti.com/lit/ds/symlink/sn74lvc1g08.pdf)
· [Current screen](../electrical/verification/power-screen.txt)
· [USB-IF suspend requirement](https://compliance.usb.org/index.asp?UpdateFile=Electrical)
· [Exact Pico7 evidence](/tmp/gridpico-rev-a-firmware-evidence.0IuPzz/RESULT.md)
