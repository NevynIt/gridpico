# Final paper-review handoff · RP2040-Zero four-port successor

- [Linked four-sheet PDF](presentation/Gridpico-Four-Port-Simplified.pdf)
  · [Sheet 1 SVG](presentation/drawings/sheet-01.svg)
  · [Sheet 2 SVG](presentation/drawings/sheet-02.svg)
  · [Sheet 3 SVG](presentation/drawings/sheet-03.svg)
  · [Sheet 4 SVG](presentation/drawings/sheet-04.svg)
- [Electrical model, BOM, pinout and power screen](electrical/README.md)
  · [Original electrical manifest](electrical/SHA256SUMS)
- [Independent HAL disposition](review/HAL.md)
  · [Current and fault matrix with HAL's clarification](review/CURRENT-FAULT.md)
  · [Review manifest](review/SHA256SUMS)
- [Part and link index](presentation/parts-links.csv)
  · [Manufacturer audit](presentation/verification/manufacturer-links.txt)
  · [Presentation manifest](presentation/SHA256SUMS)

HAL passed the exact 44-entry `31f4c817` electrical package for paper
design review and retained powered fit and use holds. The earlier
`83edec59` draft and the HAL-reviewed electrical snapshot keep their
original seals. The linked presentation reads the exact
`electrical/pin-netlist.csv` and is sealed independently. The current and
fault matrix reading copy implements HAL's clarification that J2 D± are
isolated when J2_PRESENT is low. Its two source-relative links now point
into the retained electrical snapshot. The sealed original remains at
[electrical/CURRENT-FAULT.md](electrical/CURRENT-FAULT.md).

The PDF and SVGs retain 13 IC role captions, 18 manufacturer links and
67 clearly marked open exact parts. Electrical wire/junction/NC geometry
and all four graph ledgers match the HAL-reviewed donor. The Product Brief
pin is `c86a8d3650f0039125e2403fe05f6e399a5e2103`, exact blob
`0d4f4de6f24762f6f5a545fcbfa6ef68662363c2`.

The J2 current/suspend and source-envelope holds, raw-valid B5 grant
recovery, forced-J4 isolation and trip order stay open. Exact fitted parts
and the relocated J2 receptacle and insulated original lands remain
dependencies. [Replay receipt](review/PRESENTATION-CHECK.txt)

Run with Python 3.11 and PyMuPDF 1.26.5.

```sh
./presentation/verify-linked.sh
(cd presentation && sha256sum -c SHA256SUMS)
(cd review && sha256sum -c SHA256SUMS)
```
