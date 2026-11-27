# Gridpico RP2040-Zero carrier review package

The electrical schematic is nine net-labeled sheets. The PDF is the rendered review copy. The SVG files are editable vector drawings. Sheet 6 shows the complete module interface and the removed-receptacle data lands. The external connector pinouts are on sheets 1, 7 and 8 and in the compact CSV. The analysis records source and load envelopes, inrush, source isolation and the conditional three-unit chain.

- [Electrical schematic PDF](Gridpico-RP2040-Zero-Schematic.pdf)
- [Complete module and connector pinout CSV](pinout.csv)
- [Hardware and power review](POWER-REVIEW.md)
- [Schematic electrical source](board.py), [renderer](render.py), [all-pin connectivity](pin-netlist.csv), [named nets](nets.txt)
- [SVG schematic sheets](drawings/) and [arithmetic source](power_budget.py)

Edit the pin triples, component values and sheet placement in board.py to keep the output drawings and netlist in sync. render.py uses Python 3 and PyMuPDF 1.26.5. Run python3 render.py in this directory and python3 power_budget.py to regenerate the calculation. The SVG sheets can also be opened directly in any SVG editor. The source package contains no layout or footprint approval. The connectivity checks cover the named schematic nets and are separate from EDA ERC, prototype measurements and the pending electrical and topology reviews.
