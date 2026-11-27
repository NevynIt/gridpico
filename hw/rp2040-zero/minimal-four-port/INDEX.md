# Minimal four-port

**54-reference RP2040-Zero carrier. HAL passed the paper design review.
Powered fit and use, wiring, and fabrication remain held.**

U1 supplies U3, U4 VBUS, and the external RGB 5 V pin. U2 supplies the
Pico and its local SD/OLED loads. A valid U2 supply has SYS5 priority.
U1 supplies SYS5 when protected U2 voltage is unavailable. J8 carries
common ground, direct 3.3 V GP17 data through the existing 100 Ω R81,
and U1-only 5 V. Neither USB data pair has a carrier gate or clamp.
[U1-only outlet decision](https://github.com/NevynIt/gridpico/blob/fe51cd03d2e519e5c87b7c34a9cc36f6c913b7b1/PRODUCT_BRIEF.md#L50-L53)

- [Linked schematic PDF](Gridpico-Minimal-Four-Port.pdf)
  · [Inputs and U3](drawings/sheet-01.svg)
  · [SYS5 and U4](drawings/sheet-02.svg)
  · [USB data](drawings/sheet-03.svg)
  · [Module and RGB](drawings/sheet-04.svg)
- [Editable model and replay instructions](README.md)
  · [BOM](bom.csv) · [BOM delta from Protected](bom-delta.csv)
  · [Pinout](pinout.csv) · [Pin netlist](pin-netlist.csv)
  · [Manufacturer links](parts-links.csv)
- [Power, fault and fitted-part holds](POWER-REVIEW.md)
  · [Nominal source-state checks](verification/source-states.txt)
  · [Direct-RGB revision check](verification/direct-rgb-delta.txt)
  · [47-entry electrical and drawing seal](SHA256SUMS)
- [Independent HAL receipt and exact holds](review/HAL.md)
  · [Independent pin check](review/verify-pins.py)
  · [HAL receipt manifest](review/SHA256SUMS)

SYS5 selection follows protected B5 voltage. A PC cable may stay
connected after VBUS drops, so U1 can sustain direct native data drive
toward an unpowered PC, including in BOOTSEL. The exact U2 default,
configured, startup, reset/ROM and suspend draw remain open. Other
holds include mux and module source limits, fitted SD/OLED rail budget,
reverse-current and fault behavior, and direct data injection on U4 and
GP17. The exact RGB strip must accept the Pico's 3.3 V logic at its
actual 5 V supply and tolerate the U1-off condition. The sealed power
review and HAL receipt preserve their original scratch citations.

[Hardware review index](../README.md)
· [Protected four-port (85 references)](../protected/INDEX.md)
· [Earlier carrier (102 references)](../minimal/INDEX.md)
· [Two-port bench](../bench/INDEX.md)
