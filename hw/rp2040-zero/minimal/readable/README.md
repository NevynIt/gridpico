# RP2040-Zero minimum carrier · readable drawings

[Four-sheet PDF](drawings/Gridpico-RP2040-Zero-Schematic.pdf)
· [Sheet 1 SVG](drawings/sheet-01.svg) · [Sheet 2 SVG](drawings/sheet-02.svg)
· [Sheet 3 SVG](drawings/sheet-03.svg) · [Sheet 4 SVG](drawings/sheet-04.svg)

Local parts are drawn with direct wires, power symbols and branch dots.
Shared and remote signals retain explicit net labels. Each IC has a short
description beside its reference. Click a blue underlined value to open
the manufacturer's document.

An asterisk means the exact manufacturer or order code is open. The
[part and link index](parts-links.csv) records the status of every reference.
C61 links to the manufacturer specification for the recorded catalog basis.
C62 links to the specification covering the recorded polymer capacitor
candidate. M1 links to the Waveshare module schematic, with the actual
module revision still pending. These links preserve the fit conditions
in the [electrical review](../REVIEW.md) and [HAL disposition](../review/HAL.md).

## Electrical and drawing verification

The [sealed electrical source](../board.py), [BOM](../bom.csv),
[netlist](../pin-netlist.csv), [pinout](../pinout.csv) and original
drawings remain in the [published electrical package](../INDEX.md). See
the [original publication seal](https://github.com/NevynIt/gridpico/blob/dae330c1e37697fb243f6a9bc90b3801298261ae/hw/rp2040-zero/minimal/SHA256SUMS) and
[current manifest](../SHA256SUMS). The README archival link is the only
change in the current electrical package.

- The [graph audit](verification/drawing-parity.txt) covers 102 references,
  340 pin groups and all 33 deliberate no-connects. Every redrawn sheet
  rejects a swapped label, opened wire and removed no-connect mark.
- All 11 IC captions and 16 placed document links were checked in the PDF
  and SVG. The [manufacturer link audit](verification/manufacturer-links.txt)
  records the exact code found in each manufacturer's document.
- The [replay receipt](verification/source-render-parity.txt) covers source and
  render parity. The [drawing generator](redraw.py) uses the published
  primitives. Run `./verify-readable.sh` with Python 3.11 and PyMuPDF 1.26.5
  to replay both electrical and presentation packages in temporary storage.
  [Presentation manifest](SHA256SUMS)
