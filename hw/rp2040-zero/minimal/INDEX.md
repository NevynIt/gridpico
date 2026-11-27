# Minimal RP2040-Zero carrier review

**Paper design only. Fabrication, wiring, power-up and chain qualification are on hold.**

The selected schematic keeps all four USB roles. U3 draws from U1 only. U4 uses either U1 or independently authorized U2. The 102-reference BOM compares with 180 in the historical carrier. The three-way comparison includes an alternative that removes U2-powered U4 and gives up the single-supply cascade.

- [Readable four-sheet PDF with manufacturer links](readable/drawings/Gridpico-RP2040-Zero-Schematic.pdf)
  and [SVGs, source and link audit](readable/README.md)
- [Published electrical model, original drawings, pinout and BOM](README.md)
- [One-page comparison](carrier-comparison.pdf) and [editable SVG](carrier-comparison.svg)
- [Hardware Design qualification holds](REVIEW.md)
- [Independent HAL electrical disposition](review/HAL.md)

## Review provenance

HAL reviewed the electrical package sealed by manifest SHA-256 `f5702c50f64ec696c68058555489c38947d8970552a158495ef66efc40f9a34a`. The published comparison package adds the comparison and related text and verification. The original schematic, pinout, netlist, BOM and power model match the reviewed electrical package byte-for-byte. The readable drawing is a separate presentation of the same model. The [original publication manifest](https://github.com/NevynIt/gridpico/blob/dae330c1e37697fb243f6a9bc90b3801298261ae/hw/rp2040-zero/minimal/SHA256SUMS) has SHA-256 `c664df16eb3543174eb113eb00247987c43e401cca2c5ca8010c57c2796f181d`. The [current manifest](SHA256SUMS) has SHA-256 `f3fb48a66b668eee909210acc3944bb5537095314d2a35f49475eee1373aacbb`. Its only file-digest change is the historical link in README.md.

The original HAL receipt has SHA-256 `bb4f69e742c3068c2b4b7938deb0334ff7d3766f653bfb2c7648cbba1c8a57de`. The tracked copy updates its archive pointer to this index.
