# RP2040-Zero two-port bench variant

One-page paper schematic derived from the stock-module bench hookup on Gridpico
`dae330c`. The drawing retains the PC-to-stock-U2 cable, a data-only PIO
connection to a separately powered hub, SPI microSD and I²C OLED. The hub
adapter connects exclusively to the hub. R1 and R2 are the only added discrete
parts, each 22 Ω in series with its PIO data line. Fit the exact bench USB-C
breakout/cable, a suitable 3.3 V SPI microSD breakout and a suitable 3.3 V I²C
OLED. The fitted models and revisions remain open.

## Hub and backfeed fit gate

The repo bench record describes GP14/15, common ground and an intentionally open
breakout VBUS pad. The routed rig identified a Realtek hub function (`0bda:5411`);
the exact enclosure, breakout, CC arrangement and cable remain unidentified.
Match that combination or establish equivalent attach behavior before fitting.
Verify the connector role, CC terminations and both usable Type-C orientations.
Some self-powered hub designs need upstream host VBUS to attach. The open pad
isolates the U4 VBUS conductor from the module's U2 VBUS. Stock M1 P1.23 is tied
to U2 VBUS internally and stays unused externally. A powered hub can still
expose a data-pin back-power path into an unpowered Pico. Qualify upstream
VBUS/data behavior and rail rise with the PC physically disconnected. If this
hub needs VBUS, hold the data-only fit until a separate, qualified forward
VBUS feed with current limiting and reverse blocking is available. If it drives
data into the unpowered module, hold direct wiring pending a qualified
power-off-isolating data path. Verify full-speed signaling and effective host
pull-down/termination on the exact PIO build.
[Bench hookup](https://github.com/NevynIt/gridpico/blob/dae330c/docs/HARDWARE.md)
· [Routed rig](https://github.com/NevynIt/gridpico/blob/dae330c/api/pico-target.md)
· [PIO USB reference](https://github.com/sekigon-gonnoc/Pico-PIO-USB/blob/b186e36bea0f3bcc2ee94ca1fa81e7f71281443a/README.md)
· [Example upstream VBUS attach requirement, Microchip USB2503A §5.1.2](https://ww1.microchip.com/downloads/en/DeviceDoc/00002249A.pdf)
· [USB-IF upstream back-voltage test, §F](https://www.usb.org/sites/default/files/USB-IFTestProc1_3.pdf)

## SD, OLED and U2 power fit gate

P1.21 is the module's regulated 3.3 V output. Verify the stock module revision
and accessory voltage limits, pad order, SD pull-ups and bypass, and OLED
I²C pull-ups to 3.3 V. SD numbers are card contacts, and the OLED labels are
functions. Verify them against the purchased breakouts. With both accessories
fitted, qualify the regulator voltage and thermal margin alongside U2 connector
current at attach, inrush, active transfer/SD write, reset/BOOTSEL and USB
suspend. The existing installed native image declares 100 mA bus-powered.
Qualify to the actual installed declaration and applicable USB preconfiguration
and suspend limits. The USB 2.0 default suspend-current ceiling is 2.5 mA.
Accessory fit remains open until both module-rail margin and the USB budget
are demonstrated.
[Waveshare stock schematic](https://files.waveshare.com/upload/4/4c/RP2040_Zero.pdf)
· [SD Association simplified specifications](https://www.sdcard.org/downloads/pls/)
· [USB-IF electrical compliance updates](https://compliance.usb.org/index.asp?UpdateFile=Electrical)

## Review artifacts

- `bench-variant.pdf` and `bench-variant.svg` are the same one-page drawing.
- `bench_sheet.py` uses the published drawing renderer and checks all eleven
  listed M1 header positions and functions against the published pinout.
- `pin-map.csv` records the named endpoints, including the isolated U4 VBUS
  cable segment and stock internal U2-to-P1.23 connection.
- `SHA256SUMS` seals the drawing, source, pin map and this note.

Separate scratch paper design. The four-port design remains at its published
basis. No physical work or new bench measurements were performed.
