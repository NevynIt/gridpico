# Minimal RP2040-Zero carrier review

**Paper design only. Fabrication, wiring, power-up and chain qualification are on hold.**

The selected schematic keeps all four USB roles. U3 draws from U1 only. U4 uses either U1 or independently authorized U2. The 102-reference BOM compares with 180 in the historical carrier.

- [Schematic, editable sheets, pinout, BOM and one-page overview](README.md)
- [Hardware Design qualification holds](REVIEW.md)
- [Independent HAL electrical disposition](review/HAL.md)

## Review provenance

The [published electrical package](SHA256SUMS) matches HAL's exact reviewed manifest SHA-256 `f5702c50f64ec696c68058555489c38947d8970552a158495ef66efc40f9a34a`.

The original HAL receipt has SHA-256 `bb4f69e742c3068c2b4b7938deb0334ff7d3766f653bfb2c7648cbba1c8a57de`. The tracked copy updates its archive pointer to this index.
