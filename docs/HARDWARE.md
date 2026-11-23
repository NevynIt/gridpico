# Hardware bench topology

This page records the owner-supplied Gridpico bench setup. Gridpico hardware validation is pending.

## Connections

Connect the Waveshare RP2040-Zero's native USB-C port to the Pi/computer used for SSH. This supplies board power.

Connect GPIO label GP14 to D+ and GPIO label GP15 to D− on the four-pin USB-C breakout. These are GPIO labels, not header positions. Join common ground and plan one 22 Ω series resistor on each data line. Connect the breakout to the separately powered hub's upstream data connection with its matching cable.

Connect USB MIDI devices, including Circuit Tracks, to the hub's downstream data ports. Power the hub from its own supply.

Leave the breakout's V/VBUS lead unconnected to the board or any external 5 V feed. This leaves the breakout power-feed path open. Do not link breakout V to the board's 5 V pin. The RP2040-Zero is supplied through its native USB-C connection to the Pi/computer. The PIO host link carries D+, D− and common ground only.

Any future connected 5 V feed needs a defined host-side source and a protection design that isolates it from native USB VBUS. A series diode is one candidate for reverse-current isolation, not a chosen part. Validate the component and its polarity, voltage drop under load, current limiting, and USB-C attach behavior in the intended plug orientations before adding a feed. It is not part of this topology.

## Short bench checks

Before applying power, verify GP14/D+, GP15/D−, common ground and the planned series resistors. Confirm the breakout's V/VBUS lead remains unconnected to the board or any external 5 V feed.

Then check native USB enumeration separately from SSH access to the Pi/computer. With the hub separately powered and the breakout's V/VBUS lead still unconnected to the board or any external 5 V feed, check downstream device enumeration and MIDI traffic.

## Evidence and references

The [rpico-bridge hardware notes](https://github.com/NevynIt/rpico-bridge/blob/main/docs/HARDWARE.md) describe two separate observations:

- **rpico-bridge recorded trial:** an empty, bus-powered hub was detected with a temporary breakout-V-to-board-5-V jumper, a charger on native USB, no laptop and no downstream devices.
- **rpico-bridge owner-reported run:** two MIDI devices routed through a separately powered seven-port hub with the PIO VBUS jumper disconnected. The Pico's native USB-C power source for that run was unconfirmed.

Neither observation verifies Gridpico hardware or establishes protection and current capacity for this setup.

The [Pico-PIO-USB README at pinned commit `b186e36`](https://github.com/sekigon-gonnoc/Pico-PIO-USB/blob/b186e36bea0f3bcc2ee94ca1fa81e7f71281443a/README.md) describes the PIO USB GPIO pair and recommends 22 Ω series resistors. The [Waveshare RP2040-Zero schematic](https://files.waveshare.com/upload/4/4c/RP2040_Zero.pdf) is the board pinout reference. Neither source is evidence of a Gridpico bench test.
