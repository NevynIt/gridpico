# RP2040-Zero hardware reviews

- **Four-port minimum carrier** · [Readable schematic PDF with datasheet links](minimal/readable/drawings/Gridpico-RP2040-Zero-Schematic.pdf)
  · [SVGs, drawing source and verification](minimal/readable/README.md)
  · [Electrical model, BOM and review holds](minimal/INDEX.md)
- **Two-port bench variant** · [One-page wiring PDF](bench/bench-variant.pdf)
  · [SVG, source, fit conditions and HAL disposition](bench/INDEX.md)

Both designs are paper reviews. Fabrication, wiring and powered qualification
remain held. The two-port variant uses stock USB-C to the PC and a separately
powered hub on U4 with its VBUS pad left open. Its fit remains conditional on
the actual hub, cable, breakout and accessories.

The [historical carrier archive](https://github.com/NevynIt/gridpico/blob/7d5fb9b2146998d2ba935f79b391cacd2d146a5f/hw/rp2040-zero/draft/ARCHIVE.md) and the
[corrected overview](https://github.com/NevynIt/gridpico/tree/db8a0c2165c04e021234a4c83f52c976e1a0dd74/hw/rp2040-zero/draft/review/overview) are pinned to their publication commits.
