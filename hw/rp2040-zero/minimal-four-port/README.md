# Minimal four-port

**U1-only outlets, 54 references. This is a paper candidate for independent
review. Powered fit/use, wiring and fabrication remain held.**

| Port | Drawn role and source |
| --- | --- |
| U1 | Dedicated 5 V input. Its protected A5 rail supplies U3, U4 and external RGB. It also supplies SYS5 when U2 is unavailable. |
| U2 | Native USB Device with direct data. Protected B5 powers the module and its small 3.3 V SD/OLED loads. Valid B5 takes SYS5 priority. |
| U3 | Independently protected U1-only 5 V outlet. Data pins are open. |
| U4 | PIO USB Host, direct data with series termination and an independently protected U1-only VBUS. |
| RGB | J8 pin 1 common GND, pin 2 direct 3.3 V GP17 data through the existing 100 Ω series resistor, pin 3 U1-only A5. Exact load-only connector and pixel fit are open. |

This replaces the buffered 57-reference candidate `a578fb3e` with the
direct RGB path Jill approved. IC16, its bypass C161 and its input
pulldown R162 are removed. R81 remains the original 100 Ω series resistor.
The earlier tied-rail `3a1d454e` and the buffered `a578fb3e` are historical
scratch seals. This variant removes 31 references from Protected, with no
new reference. The U1-only source scope is pinned to the
[Product Brief](https://github.com/NevynIt/gridpico/blob/fe51cd03d2e519e5c87b7c34a9cc36f6c913b7b1/PRODUCT_BRIEF.md#L50-L53),
exact blob `6609e47237007a7705f816512f8b856685aaeeff`.

The independent [Protected four-port](https://github.com/NevynIt/gridpico/blob/ef985b2a918e05adc0a414b4e9f22fccae6caba5/hw/rp2040-zero/protected/INDEX.md)
remains the 85-reference U1-only U3 design with U4 source selection and
gated USB data. The [two-port bench variant](https://github.com/NevynIt/gridpico/blob/f56ed7d98d394a989cc3a387cfe88645ffb718a4/hw/rp2040-zero/bench/INDEX.md)
keeps its separate paper scope and fit holds.

## Drawings, electrical model and holds

- [Four-sheet linked PDF](Gridpico-Minimal-Four-Port.pdf) ·
  [Inputs and U3](drawings/sheet-01.svg) · [SYS5 and U4](drawings/sheet-02.svg)
  · [Direct USB data](drawings/sheet-03.svg) ·
  [Module and RGB](drawings/sheet-04.svg)
- [Pinout](pinout.csv) · [Exact netlist](pin-netlist.csv) · [BOM](bom.csv)
  · [BOM delta from Protected](bom-delta.csv) · [Part/document index](parts-links.csv)
- [Source policy, current, faults and fitted holds](POWER-REVIEW.md).
  The mux responds to protected B5 voltage. A cabled PC can lose VBUS
  while direct native data remain connected. The U2 current budget still
  includes module, accessories, startup, reset, BOOTSEL and suspend.
  Direct U4 injection, RGB input-level compatibility and RGB off-state
  injection remain fit/use holds.
- [Editable circuit model](model.py) · [Drawing layouts](layouts.py) ·
  [Renderer and graph/link verifier](render_candidate.py) ·
  [Source-state graph test](check_power.py) · [Current arithmetic](screen.py)

Each PDF/SVG set contains five plain-language IC captions and eight
clickable component values across five manufacturer documents.
The 46 unselected exact parts are starred. C61's catalog basis, C62's
conditional candidate and M1's model schematic leave actual fitted
parts open. [Manufacturer document audit](verification/manufacturer-links.txt)

## Reproduce

Use Python and PyMuPDF 1.26.5. The original dev review tool bundle can
provide PyMuPDF when absent. Run from this directory.

```sh
export PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH=/tmp/gridpico-rp2040-zero-board.DuB4iV/tools
python3 model.py > verification/model-check.txt
python3 check_power.py > verification/source-states.txt
python3 screen.py > verification/power-screen.txt
python3 render_candidate.py
sha256sum --quiet -c SHA256SUMS
```

The paper verifier checks endpoint graphs, intentional opens and
active link rectangles against visible part values. A removed wire per
sheet and a forbidden B5-to-A5 feed are negative controls. Analog
behavior and fitted safe use remain qualification holds.
[Exact buffer-removal verification](verification/direct-rgb-delta.txt) · [Exact seal](SHA256SUMS)
