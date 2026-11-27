# RP2040-Zero hardware reviews

- **Protected four-port (85 references)** · [Linked schematic PDF](protected/presentation/Gridpico-Four-Port-Simplified.pdf)
  · [Electrical source, BOM, pinout and powered-use holds](protected/INDEX.md)
- **Minimal four-port (54 references)** · [Linked schematic PDF](minimal-four-port/Gridpico-Minimal-Four-Port.pdf)
  · [U1-only outlets, direct RGB, electrical source and powered-use holds](minimal-four-port/INDEX.md)
- **Earlier four-port minimum carrier (102 references)** · [Readable schematic PDF with datasheet links](minimal/readable/drawings/Gridpico-RP2040-Zero-Schematic.pdf)
  · [SVGs, drawing source and verification](minimal/readable/README.md)
  · [Electrical model, BOM and review holds](minimal/INDEX.md)
- **Two-port bench variant** · [One-page wiring PDF](bench/bench-variant.pdf)
  · [SVG, source, fit conditions and HAL disposition](bench/INDEX.md)

These designs are paper reviews. Fabrication, wiring and powered qualification
remain held. The two-port variant uses stock USB-C to the PC and a separately
powered hub on U4 with its VBUS pad left open. Its fit remains conditional on
the actual hub, cable, breakout and accessories.

The [historical carrier archive](https://github.com/NevynIt/gridpico/blob/db8a0c2165c04e021234a4c83f52c976e1a0dd74/hw/rp2040-zero/draft/ARCHIVE.md) and the
[corrected overview](https://github.com/NevynIt/gridpico/tree/db8a0c2165c04e021234a4c83f52c976e1a0dd74/hw/rp2040-zero/draft/review/overview) are pinned to their publication commits.
