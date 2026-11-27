# Two-port RP2040-Zero bench paper variant

[One-page PDF](bench-variant.pdf) · [SVG](bench-variant.svg)
· [Editable drawing source](bench_sheet.py) · [Pin map](pin-map.csv)
· [Fit conditions and minimum parts](README.md)

This is the separate five-file paper-design package sealed by
`c51e2d665f116e57efe4fcbdf4bf26ab65e8247e1e5225a4b40ecbf4c95a4af7`.
The original files are preserved byte-for-byte. [Original manifest](SHA256SUMS)

[Independent HAL review](review/HAL.md) accepts the diagram for paper review.
Historical attach was observed. The exact hub, cable and breakout and actual
upstream VBUS, CC and backfeed behavior remain open. The drawn open U4 VBUS
pad breaks the direct VBUS path. The series data resistors provide no
power-off isolation. Fit and powered qualification remain held.
