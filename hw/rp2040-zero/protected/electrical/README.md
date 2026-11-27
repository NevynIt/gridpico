# Four-port RP2040-Zero DEV simplification

[Keep / change / drop review](REVIEW.md)
· [Direct-wired four-sheet PDF](Gridpico-Four-Port-Simplified.pdf)
· [SVG sheets](drawings/)
· [BOM](bom.csv)
· [Source/current/fault matrix](CURRENT-FAULT.md)
· [Reference-by-reference delta](bom-delta.csv)
· [Pinout](pinout.csv)
· [Pin/net model export](pin-netlist.csv)

This separate paper successor is based on the four-port electrical design
published at `0b431a9`. The four electrical source/export files used by the
generator and both review records are pinned under [baseline](baseline/). The carrier BOM falls from **102 to 85 references**.
Active packages fall from 15 to 13. The decrease includes eliminating the old dual enable networks, command and
fault diagnostics, and carrier SD/OLED passives whose functions must be
present on the selected modules. SD, OLED, local 3.3 V UART and RGB wiring are
shown. Exact accessory choice and fit remain open. The electrical reference
and the two-port bench package remain separate.

## Drawn current and signal boundaries

- **U1 → A5 and U3.** D10 is a shunt TVS from U1_RAW to GND, band/cathode
  toward U1_RAW. IC4 provides a separately protected U1-only output.
- **U2 → B5 and native Device.** The relocated USB-C keeps J2 VBUS off
  module SYS5 and feeds only IC2 and J2 voltage sense. Actual J2 VBUS must
  qualify the native USB data switch, including BOOTSEL. IC2's raised OVLO
  has a screened 5.612–5.834 V rising threshold. An abnormal source above
  the 5.50 V recommended mux window still needs transient and safe operating
  evidence. Staying below the 6 V absolute limit is only a static check.
- **SYS5.** IC9 prefers A5. B5 becomes a module-only fallback while A is
  unavailable. U2-only/default/suspended load and reset/ROM remain held.
- **U4 Host.** IC8 prefers A5 or uses the independent B branch. IC6 contains
  output reverse feed and faults. GP8 carries one active-high B permission
  on the fitted B profile. GP18 reads actual J2 voltage presence high.
  IC14 forgets the old grant after sensed J2 loss, and IC15 requires GP8
  high even after the latch has set. A valid B grant funds the current
  attachment only. Board hardware cannot infer configured current or
  USB suspend from VBUS. GP9, GP20 and GP21 are spare in this proposal.
- **Data isolation.** Both muxes select a wired channel only when the
  corresponding local hardware condition is high. The unused channel is
  open. U4's local IC6 PG and PGTH drive a qualifier which distinguishes
  the unpowered PG clamp from valid high. Measure forced-output, dropout
  and rail-ramp behavior before approving the interlock.
- **Accessory fit.** The 3.3 V rail feeds a qualified microSD breakout
  containing required card pulls and bypass, and an OLED with existing
  3.3 V I²C pulls. Maintain the fitted regulator's measured total rail
  envelope and the stricter USB bus-power envelope when applicable.

[Paper thresholds and current screen](verification/power-screen.txt)
· [Boolean source and latch check](verification/state-check.txt)
· [Model verification](verification/model-check.txt)
· [Manufacturer sources and risk disposition](REVIEW.md)

## Reproduce

Run with Python 3.11 and PyMuPDF 1.26.5.

```sh
python3 model.py
python3 screen.py
python3 check_states.py
python3 render_candidate.py
sha256sum -c SHA256SUMS
```

The renderer checks each sheet's drawn terminal groups against the editable
candidate model, verifies the SVG wire segments, and rejects deliberate
wire breaks. PDF text bounds/overlap and raster inspection passed.
The checks do not establish electrical qualification. Fit, layout release,
wiring, powered validation and fabrication remain held.
