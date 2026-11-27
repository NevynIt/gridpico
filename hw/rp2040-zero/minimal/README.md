# RP2040-Zero minimum carrier · Rev A

Paper review for Jill's ordinary DEV carrier direction in [Product Brief](../../../PRODUCT_BRIEF.md), source commit `8acb1af9ec67f2745da5ef971b19752451da90a8`, blob `dff3f7be49e8c640ccae2dec42b4cffcba27d922`. This package retains all four connector roles, including protected power from either U1 or U2 to U4. The [historical draft](https://github.com/NevynIt/gridpico/blob/db8a0c2165c04e021234a4c83f52c976e1a0dd74/hw/rp2040-zero/draft/ARCHIVE.md) is the comparison basis.

- [One-page comparison](carrier-comparison.pdf) and [editable SVG](carrier-comparison.svg)
- [Minimum-carrier overview](carrier-overview.pdf) and [editable SVG](carrier-overview.svg)
- [Four-sheet net-labeled schematic](Gridpico-RP2040-Zero-Schematic.pdf) and [editable SVG sheets](drawings/)
- [Connector and module pinout](pinout.csv), [full netlist](pin-netlist.csv), [reduced BOM](bom.csv), and [part notes](BOM.md)
- [Design choices and qualification holds](REVIEW.md) and [power screen](power-budget.txt)

The drawing source is [board.py](board.py). Python 3 and PyMuPDF 1.26.5 regenerate the review outputs.

```sh
python3 render.py
python3 overview.py
python3 comparison.py
python3 power_budget.py > power-budget.txt
python3 verify.py >> design-check.txt
sha256sum -c SHA256SUMS
```

Fabrication and powered validation require separate authorization and closure of the relevant electrical and exact-part holds.
