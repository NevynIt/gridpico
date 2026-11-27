# Protected four-port

**85-reference RP2040-Zero carrier. HAL passed the paper design review.
Powered fit and use, wiring, and fabrication remain held.**

U3 draws power only from U1. U4 accepts protected power from U1 or a
separately admitted U2 branch. SYS5 selects automatically. Native U2 and
PIO U4 retain hardware data isolation and connector protection.
[Protected and Minimal Product Brief decision](https://github.com/NevynIt/gridpico/blob/f56ed7d98d394a989cc3a387cfe88645ffb718a4/PRODUCT_BRIEF.md#L52-L53)

- [Linked four-sheet schematic PDF](presentation/Gridpico-Four-Port-Simplified.pdf)
  · [SVG sheets](presentation/drawings/) · [Part and datasheet index](presentation/parts-links.csv)
- [Editable electrical and drawing sources](electrical/README.md)
  · [BOM](electrical/bom.csv) · [102-to-85 reference delta](electrical/bom-delta.csv)
  · [Pinout](electrical/pinout.csv) · [Pin netlist](electrical/pin-netlist.csv)
- [Independent HAL disposition and powered holds](review/HAL.md)
  · [Current and fault matrix with the reviewed J2 wording](review/CURRENT-FAULT.md)
  · [Replay and exact payload identities](HANDOFF.md)

The open holds cover total J2 current at default, suspend, reset and
BOOTSEL, including U1 loss. They also cover the IC2 cutoff versus IC9's
5.5 V operating window, B5 recovery while raw J2 remains valid without a
fresh grant, and forced J4 behavior of the power, data isolation and
reverse paths during faults and ramps. Trip order, the exact fitted parts,
and the relocated J2 receptacle with its original VBUS lands insulated
remain open. The HAL receipt and reviewed matrix record the exact cases.

The archived electrical basis is `31f4c817`. The electrical,
presentation and review manifests cover 44, 28 and 3 entries respectively.
The preserved review records include original scratch citations. The
links above lead to the repository copies of the design, HAL receipt and
reviewed matrix.

[Hardware review index](../README.md)
