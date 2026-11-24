# Pico bench operator note

This is a repeatable, RAM-only bench procedure, not permission to deploy or write flash. Keep the [Pico target contract](../api/pico-target.md) and [bench wiring](HARDWARE.md) as the technical sources of truth. Final selected-route/WSS evidence is recorded in [the roadmap](../ROADMAP.md#approved-physical-pico-and-cdc-delivery).

## Separate the Pi and Pico paths

The ordinary Pi router is `http://192.168.0.223:18768/`. Its CLI and `/serial` stay on `127.0.0.1:18767`, reached only through the approved strict SSH forward using the `gridpico-rpi0` alias. This is the Pi’s existing service, not Pico firmware.

The Pico-specific HTTPS/WSS bench listener was temporary at `https://192.168.0.223:19445`, reached only through the strict CONNECT/SSH path to `gridpico-rpi0`. Its `/pico-cdc/` console connected to WSS `/cdc`. It bound only to `wlan0`, required exact Host/Origin, matching IP SAN and mTLS, and kept MIDI and CDC Maintenance grants on separate leaf allowlists. Stock Chrome passed the selected RAM test over this path. The listener and Pico RAM image have both been retired, and operator mTLS enrollment remains pending. Direct development-host TCP and operator-laptop reachability are not established. Do not expose Pi `/serial` on LAN or treat this temporary listener as a deployment. Browser-local Web Serial is a separate path.

### Read-only bridge fallback

Use the already trusted Pi SSH login for a bounded status snapshot. The helper sends only `devices` and `rules`, then closes the original bridge CDC. Run it only when no other maintenance peer owns that CDC:

```sh
ssh -o StrictHostKeyChecking=yes pi@192.168.0.223 \
  '~/.local/state/gridpico-bridge-status/7ea0716/status'
```

The helper is owner-only, versioned from source `7ea0716`, and does not use WSS, certificates, boot commands, or route changes. It takes about four seconds, sends only the two read-only queries and closes CDC. Testing and HAL independently verified it. The observed saved rules show Default All and the APC Key 25 mk2 on cable 1, any channel, excluded. This read-only view is separate from the Pi router URL above. Operator mTLS enrollment and laptop access remain pending.

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

The approved test used only an ELF RAM load. After a separately approved bench window, two independent full 2 MiB bridge reads match, the separate backup has been verified, and exact board/ROM identity and ELF digest are rechecked, select ROM and run `picotool info -a --bus "$bus" --address "$address" --vid 0x2e8a --pid 0x0003`. Re-select the device after every USB detach using its fresh ROM bus/address. Only then may the approved artifact be run with `picotool load --verify --execute "$elf" -t elf --bus "$bus" --address "$address" --vid 0x2e8a --pid 0x0003`. Require the tool to report a RAM load and RAM verification. Never substitute UF2 or a flash target. Confirm actual ROM entry and return to the original bridge, then verify the full 2 MiB backup again with read-only `picotool verify "$backup" -t bin -o 0x10000000 --bus "$bus" --address "$address" --vid 0x2e8a --pid 0x0003`.

The final exact run completed this return and the full original flash match. It was short and attended. A new approved short SRAM test is a different scope from leaving the RAM image running unattended. Only the unattended run additionally requires an independently recoverable operator connection. Permanent flash remains blocked until an independent pre-arm reset is proven. No RAM image or temporary WSS listener is currently left running.

Never write permanent firmware until an independent reset/recovery path has been proven for a failure before watchdog arming. The current watchdog evidence covers post-arm starvation only. The direct Pi USB connection has no verified independent MCU reset. Do not use a UF2/flash load for this bench recipe.

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
