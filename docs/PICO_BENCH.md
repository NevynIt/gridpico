# Pico bench operator note

This note preserves the historical RAM-only recipe and records the current owner-approved persistent image. Keep the [Pico target contract](../api/pico-target.md) and [bench wiring](HARDWARE.md) as the technical sources of truth. Final selected-route/WSS evidence is recorded in [the roadmap](../ROADMAP.md#approved-physical-pico-and-cdc-delivery).

## Current flashed image

The bench Pico now runs the owner-verified permanent Patchbay image from published source [`a834ca6`](https://github.com/NevynIt/gridpico/commit/a834ca69c309610ad7a7d349c747b0d01949ea9c), build/image ID `d1887187`, USB `1209:0001`, `bcdDevice 0104`. Independent live native-panel Arrange Move and restore passed. The Audition fixture passed, but its physical trace contains an exact Off acknowledgement, an unqualified On record, and an early abort before the owed release. Physical Audition delivery and completed release remain unverified. The earlier `a3a6112` image readback (`88a12a212fb69104f55a75755e20ca15372bc351d4fcbffeac6765e1e263d048`) and its startup-fault trial (4.203 seconds to ROM, fault-image SHA-256 `5743ab69b561532f7ae0aa6793f6bfe77b3693899d2be450965ffb659c3a8f17`) are historical and do not identify this current image.

The separate same-board startup-fault trial for the earlier `a3a6112` image returned from the exact fault app to ROM without host CDC intervention in 4.203 seconds. Its fault-image full-flash digest was `5743ab69b561532f7ae0aa6793f6bfe77b3693899d2be450965ffb659c3a8f17`; the selected-trial composite digest was `5a0fef1d12cddbb66572fc758cb424b6e424336dd23838d37cc2df45a04f7c6a`. This bounded watchdog result was accepted for that exact image. Residual: there is no separate external MCU reset path, and the trial does not guarantee recovery from every hardware or flash failure. It does not establish recovery behavior for the current Patchbay build.

## Morning Patchbay quick start

The default top-left cell routes APC Keybed to MicroFreak. The logical page has five source rows by eight destination columns. The top-left two-by-two source/destination area is seeded, and the other cells are empty.

Press **Stop All Clips** to enter Arrange. Its four fixed top-left pads select **Assign**, **Move**, **Learn**, and **Audition**. The bottom-right pad confirms a previewed edit. To audition one destination only, tap **Audition** to select that action, press its **Track** edge to select the output, then press and hold **Audition** again. Release that pad to stop. The gesture describes the interface operation, not a complete physical Audition result; the physical On and owed release are not yet verified.

Placement edits are volatile. There is no Save operation, and **Keys** mode is not available yet. Use the [same-origin Diagnostics Pico console](../platform/linux/PI_ROUTER.md#pico-console-from-diagnostics) for read-only `TRACE`/`HUBTRACE`. Route changes remain in the separate loopback CLI over the approved SSH path described below.

## Separate the Pi and Pico paths

The ordinary Pi router is `http://192.168.0.223:18768/`. Its CLI and `/serial` stay on `127.0.0.1:18767`, reached only through the approved strict SSH forward using the `gridpico-rpi0` alias. This is the Pi’s existing service, not Pico firmware.

The direct private-LAN Pico console is live at `http://192.168.0.223:18768/pico-cdc/`. Chromium passed Connect, TRACE, Send, and Disconnect/Reconnect against the permanently flashed firmware. The console uses the overlay from source `0fece4a` (archive SHA-256 `5f5b884bc5226b3f635359732d7dcbef58e2d7816a64fdf837149136820e727b`). The earlier temporary HTTPS/WSS listener at `https://192.168.0.223:19445` has been retired. Its mTLS enrollment and the original-bridge SSH status helper are superseded for the currently flashed Gridpico firmware. Browser-local Web Serial remains separate. The ordinary Pi `/serial` CLI stays on loopback and is not exposed on LAN.

### Historical original-bridge status helper

This read-only helper was verified against the original RPICO bridge, before the Gridpico firmware was installed. It is not the CLI for the currently flashed Gridpico router and must not be run against it. Its historical invocation was:

```sh
ssh -o StrictHostKeyChecking=yes pi@192.168.0.223 \
  '~/.local/state/gridpico-bridge-status/7ea0716/status'
```

The owner-only helper at source `7ea0716` sent only `devices` and `rules`, took about four seconds, and closed CDC. Testing and HAL independently verified that historical behavior. Its captured rules showed Default All and APC Key 25 mk2 on cable 1, any channel, excluded.

## Rebuild and verify the RAM artifact

Use a fresh checkout at the exact published source commit. Do not switch an active worktree to this revision.

```sh
git clone https://github.com/NevynIt/gridpico.git gridpico-pico-bench
cd gridpico-pico-bench
git checkout --detach 5b63765
```

Use the selected reproducibility profile from the repository root. It uses Pico SDK 2.2.0, TinyUSB 0.21.0, Pico-PIO-USB 0.7.3 and arm-none-eabi-gcc 12.2.1. CMake does not create the artifact SHA manifest, so compare against the two checked values below.

```sh
cmake -S platform/rp2040/router -B build/rp2040-selected-led-refresh -G Ninja \
  -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DPICO_BOARD=waveshare_rp2040_zero \
  -DPICO_SDK_PATH=/opt/pico/pico-sdk \
  -DPICO_TINYUSB_PATH=/opt/pico/tinyusb-0.21.0 \
  -DPICO_PIO_USB_PATH=/opt/pico/Pico-PIO-USB \
  -DGRIDPICO_RP2040_BENCH_TRACE=ON \
  -DGRIDPICO_RP2040_PIO_DIAGNOSTICS=ON \
  -DGRIDPICO_RP2040_HCD_TRACE=ON \
  -DGRIDPICO_RP2040_HUB_TRACE=ON \
  -DGRIDPICO_RP2040_TWO_HUB_BENCH=ON \
  -DGRIDPICO_APC_VID=0x09e8 -DGRIDPICO_APC_PID=0x004e \
  -DGRIDPICO_APC_INTERFACE=1 \
  -DGRIDPICO_SYNTH_VID=0x1c75 -DGRIDPICO_SYNTH_PID=0x0601 \
  -DGRIDPICO_SYNTH_INTERFACE=2
cmake --build build/rp2040-selected-led-refresh \
  --target gridpico_rp2040_router --parallel 4
python3 platform/rp2040/check_ram_image.py \
  build/rp2040-selected-led-refresh/gridpico_rp2040_router.elf \
  build/rp2040-selected-led-refresh/gridpico_rp2040_router.uf2
printf '%s  %s\n' \
  3e48411b278ce578dbebdde2a4489063a6cce0361397118826ae39efdbc8f3e9 \
  build/rp2040-selected-led-refresh/gridpico_rp2040_router.elf \
  f9f11cf6bc742e5f2f09e5730d6431f4217823508433ba7a04af563092a1f3a1 \
  build/rp2040-selected-led-refresh/gridpico_rp2040_router.uf2 \
  | sha256sum --check -
```

The target is `no_flash`. Verify ELF/UF2 SRAM ranges and digests before any run. The reviewed candidate identifies as Gridpico RP2040 Router `1209:0001`, `bcdDevice 0103`, without iSerial, and carries the cooperative `BOOTSEL` command. Select it by exact USB path and descriptors; do not rely on VID/PID alone. The selected ELF is `3e48411b278ce578dbebdde2a4489063a6cce0361397118826ae39efdbc8f3e9`; its UF2 is `f9f11cf6bc742e5f2f09e5730d6431f4217823508433ba7a04af563092a1f3a1`.

The final independent audit passed for the exact ELF above, built from published source `5b63765`. Stock Chrome over the temporary SSH/mTLS WSS path saw Green on startup and rebind with zero MIDI TX until a user action. It then sent one exact C4 note pair and two Pad 00 Off/Active cycles. Browser receive/paint and PIO USB endpoint acknowledgements matched. The RAM image and listener were retired. The original bridge returned and its full flash matched. The human’s separate sighting of the bottom-left green pad was not time-correlated. These checks do not establish acoustic response, a physical APC key press, or a visible physical LED.

Separately, stock Chrome passed an exact C4 acknowledgement with the final-flash firmware in the temporary SSH/WSS fixture. This was a distinct fixture check and did not change the live Pi LAN console deployment.

The approved historical test used only an ELF RAM load. After a separately approved bench window, two independent full 2 MiB bridge reads match, the separate backup has been verified, and exact board/ROM identity and ELF digest are rechecked, select ROM and run `picotool info -a --bus "$bus" --address "$address" --vid 0x2e8a --pid 0x0003`. Re-select the device after every USB detach using its fresh ROM bus/address. Only then may the approved artifact be run with `picotool load --verify --execute "$elf" -t elf --bus "$bus" --address "$address" --vid 0x2e8a --pid 0x0003`. Require the tool to report a RAM load and RAM verification. Never substitute UF2 or a flash target. Confirm actual ROM entry and return to the original bridge, then verify the full 2 MiB backup again with read-only `picotool verify "$backup" -t bin -o 0x10000000 --bus "$bus" --address "$address" --vid 0x2e8a --pid 0x0003`.

The final historical SRAM run completed bridge return and full original-flash matching. A new approved short SRAM test is distinct from leaving a RAM image running unattended; only the unattended run additionally requires an independently recoverable operator connection. Do not use the RAM-only commands above to reproduce or replace the current persistent image.

## Keep endpoint evidence scoped

| Browser role | Direction | Pico native USB MIDI path |
| --- | --- | --- |
| Keybed TX `/midi/1` | Send to Pico | Cable 0 → Graph |
| Panel TX `/midi/2` | Send to Pico | Cable 1 → APC frontend |
| APC LED RX `/midi/3` | Receive from Pico | Cable 1 |
| Physical Keybed export `/midi/5` | Receive from Pico | Cable 0; scoped verification pending |

The two-hub candidate configures two hubs, three ordinary device slots and four MIDI slots. The current native service shares 32 PIO endpoint entries. Observed descriptor shapes imply, but do not instrument, a high-water estimate of 10 steady entries (11 during another address-zero enumeration) or 15/16 at configured slot ceilings. USB-native endpoints are separate. These figures do not prove future instrument capacity.

The tested physical endpoints were APC `09e8:004e`, interface 1, and MicroFreak `1c75:0601`, interface 2. The C4 check used synthetic Keybed TX `/midi/1`, cable 0, through the graph to the selected MicroFreak USB MIDI output. Pad 00 Off/Active used Panel TX `/midi/2`, cable 1, with LED feedback on `/midi/3`, cable 1. Captured USB acknowledgements prove endpoint traffic only. They do not prove a physical key press, audible response, or visible APC light. The human’s separate green-pad sighting was not time-correlated. Yamaha P-95 signal and MiniFuse 4 presence/path remain unknown. Seven hub sockets are physical positions, not a verified capacity count.

For the exact physical evidence boundaries and final gate state, use the [Pico delivery record](../ROADMAP.md#approved-physical-pico-and-cdc-delivery). Do not copy certificate material, keys, private config, or enrollment values into this repository.
