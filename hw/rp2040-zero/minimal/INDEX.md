# Minimal RP2040-Zero carrier review

**Paper design only. Fabrication, wiring, power-up and chain qualification are on hold.**

The selected schematic keeps all four USB roles. U3 draws from U1 only. U4 uses either U1 or independently authorized U2. The 102-reference BOM compares with 180 in the historical carrier. The three-way comparison includes an alternative that removes U2-powered U4 and gives up the single-supply cascade.

- [Schematic, editable sheets, pinout and BOM](README.md)
- [One-page comparison](carrier-comparison.pdf) and [editable SVG](carrier-comparison.svg)
- [Hardware Design qualification holds](REVIEW.md)
- [Independent HAL electrical disposition](review/HAL.md)

## Review provenance

HAL reviewed the electrical package sealed by manifest SHA-256 `f5702c50f64ec696c68058555489c38947d8970552a158495ef66efc40f9a34a`. The published comparison package adds the comparison and related text and verification. The schematic, pinout, netlist, BOM and power model match the reviewed electrical package byte-for-byte. The [published manifest](SHA256SUMS) has SHA-256 `c664df16eb3543174eb113eb00247987c43e401cca2c5ca8010c57c2796f181d`.

The original HAL receipt has SHA-256 `bb4f69e742c3068c2b4b7938deb0334ff7d3766f653bfb2c7648cbba1c8a57de`. The tracked copy updates its archive pointer to this index.
