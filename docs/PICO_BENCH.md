# Pico bench quick reference

Use [hardware wiring](HARDWARE.md) for the bench topology, the [Pico target contract](../api/pico-target.md) for recovery and target limits, and the [Roadmap](../ROADMAP.md#current-product-status) for current acceptance.

Jill authorizes repeated firmware flashes on the identified **DEV Pico only**, including occupied old-firmware sectors within the image’s erase range. Sector-by-sector approval and preservation of old flash contents are unnecessary. Keep a verified board-compatible firmware image for BOOTSEL recovery. Follow the [Pico flash verification procedure](../api/pico-target.md#recovery-and-release) and the [existing Pi and recorder maintenance protections](../platform/linux/PI_ROUTER.md#clear-development-connections-for-maintenance).

## Current bench build

The current normal Matrix is `3ae97ba4b2f37b486812ab389066c6bc2bc551a0`: `Gridpico RP2040 Router`, USB `1209:0001`, `bcdDevice 0104`, upstream path `1-1`. UF2 SHA-256: `a3080f33f8ce21efca7edc7c33072d3c25d8ee3e1a8ea973c8d7931889691254`. Full-flash read SHA-256: `99ad85bc79a7b9143054213f38a090d29235eb4dad533ca1fb29fed9ff7c0fbb`. Startup, USB CDC, cooperative BOOTSEL/no-write recovery and typed CLI help passed. See the [installed Patchbay evidence](../api/pico-target.md#installed-patchbay-matrix) and [shared Pico/Pi CLI contract](../api/pico-target.md#first-shared-cli-on-pico-and-pi).

## Pico Diagnostics

On the private LAN, open [Pi Diagnostics](http://192.168.0.223:18768/diagnostics/), select **Pico via Pi · LAN WebSocket CDC**, and click **Connect**. Click **Check Pico commands** before using the listed readback commands such as `help`, `inventory`, `status`, `TRACE` and `HUBTRACE`. See the [operator steps](../platform/linux/PI_ROUTER.md#pico-console-from-diagnostics), [CDC contract](../api/pico-target.md#private-lan-pico-cdc) and [typed CLI contract](../api/pico-target.md#first-shared-cli-on-pico-and-pi).

Pi route control is separate. Open `http://127.0.0.1:18767/diagnostics/` through the approved strict SSH forward and select **Loopback CLI** to inspect or change Pi routes. Those commands do not edit the Pico Patchbay. Pico route editing is not yet accepted.

## Patchbay quick start

The default top-left cell routes APC Keybed to MicroFreak. The 5×8 grid starts with the top-left 2×2 seeded. Press **Stop All Clips** to enter Arrange. The fixed pads select **Assign**, **Move**, **Learn** and **Audition**; the bottom-right pad confirms a previewed edit. To audition one destination, tap **Audition**, select the output at its **Track** edge, then press and hold **Audition**. Release it to stop. Pico placement edits are volatile. There is no Save operation, and **Keys** mode is unavailable. Physical Audition Note On and complete-phrase acceptance remain open in the [Roadmap](../ROADMAP.md#current-product-status).
