#!/usr/bin/env bash
set -euo pipefail
fixture=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source=$(cd -- "$fixture/../../.." && pwd)
retained=$(realpath "${1:?retained Pico7 source root}")
assets=$(realpath "${2:?detached 4e74242 asset source root}")
installed=$(realpath "${3:?installed Pico7 ELF}")
output=$(realpath -m "${4:?new fixture output directory}")
tinyusb=${PICO_TINYUSB_PATH:-/opt/pico/tinyusb-0.21.0}/src
mkdir "$output"
test "$(git -C "$assets" rev-parse HEAD)" = 4e7424265c8dd095d9e74d6de2b1392e09562361
cmp "$assets/app/stage_gridpico_assets.cmake" "$source/app/stage_gridpico_assets.cmake"
cmake -Dsource_dir="$assets" -Dasset_dir="$output/staged" -P "$source/app/stage_gridpico_assets.cmake"
python3 "$source/app/pack_pico_usb_drive.py" "$output/staged" "$output/tools.fat"
python3 "$source/app/pack_pico_usb_drive.py" "$output/staged" "$output/repack.fat"
cmp "$output/tools.fat" "$output/repack.fat"
python3 "$fixture/extract_fat.py" "$output/tools.fat" "$output/staged" "$output/extracted"

router=$source/platform/rp2040/router
stock_router=$retained/platform/rp2040/router
cmp "$router/firmware.c" "$stock_router/firmware.c"
# Preserve the exact application event fence around the stock TinyUSB dispatcher.
python3 - "$router/firmware.c" "$output/native_event.inc" <<'PY'
import pathlib
import sys
source, output = map(pathlib.Path, sys.argv[1:])
text = source.read_text()
state = text[text.index("enum { NATIVE_EVENT_IDLE"):text.index("static uint32_t native_mounted_epoch")]
state = state.replace(", native_accepted_reset_epoch", "")
wrapper = text[text.index("static bool native_bus_loss("):text.index("void tud_mount_cb(void)")]
output.write_text("enum { DEVICE_ROOT = 0 };\n" + state + wrapper)
PY
sed "s|@gridpico_usb_drive_image@|$output/tools.fat|g" "$router/usb_drive_image.S.in" > "$output/image.S"

compile_device() {
    local source_router=$1 binary=$2
    shift 2
    "${CC:-cc}" -std=c11 -g -Og -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
        -fsanitize=address,undefined -fno-sanitize-recover=all -fno-pie -no-pie \
        -Wl,--gc-sections,--wrap=dcd_event_handler \
        -DCFG_TUSB_MCU=OPT_MCU_RP2040 -DCFG_TUSB_OS=OPT_OS_NONE \
        '-D__not_in_flash(group)=' \
        -DGRIDPICO_RP2040_FLASH_ROUTER -DGRIDPICO_RP2040_MATRIX_CLI \
        -I"$output" -I"$fixture/mock" -I"$source_router" -isystem "$tinyusb" \
        "$fixture/device.c" "$source_router/usb_descriptors.c" \
        "$tinyusb/tusb.c" "$tinyusb/common/tusb_fifo.c" "$tinyusb/device/usbd.c" \
        "$tinyusb/class/cdc/cdc_device.c" "$tinyusb/class/midi/midi_device.c" "$@" -o "$binary"
}
compile_device "$stock_router" "$output/device-stock"
compile_device "$router" "$output/device-off"
compile_device "$router" "$output/device-on" -DGRIDPICO_RP2040_READONLY_USB_DRIVE \
    "$tinyusb/class/msc/msc_device.c" "$router/usb_drive.c" "$output/image.S"
"$output/device-stock" "$output/stock-descriptors.bin"
"$output/device-off" "$output/off-descriptors.bin"
"$output/device-on" "$output/on-descriptors.bin" "$output/image-after.fat"
cmp "$output/tools.fat" "$output/image-after.fat"
cmp "$output/stock-descriptors.bin" "$output/off-descriptors.bin"
# Match the raw configuration and device descriptor against the installed ELF.
python3 - "$installed" "$output/off-descriptors.bin" "$output/on-descriptors.bin" <<'PY'
import pathlib
import struct
import sys

elf, off, on = [pathlib.Path(arg).read_bytes() for arg in sys.argv[1:]]
assert elf[:7] == b"\x7fELF\x01\x01\x01"
section_offset = struct.unpack_from("<I", elf, 32)[0]
section_size, section_count = struct.unpack_from("<HH", elf, 46)
sections = [struct.unpack_from("<10I", elf, section_offset + index * section_size)
            for index in range(section_count)]
symbols = {}
for _, kind, _, _, offset, size, link, _, _, entry_size in sections:
    if kind != 2:
        continue
    names = sections[link][4]
    for entry in range(offset, offset + size, entry_size):
        name, addr, count, _, _, section = struct.unpack_from("<IIIBBH", elf, entry)
        label = elf[names + name:elf.index(0, names + name)]
        if label in (b"device", b"configuration") and section < section_count:
            _, _, _, base, position, *_ = sections[section]
            start = position + addr - base
            symbols[label] = elf[start:start + count]
assert off[:18] == symbols[b"device"] and off[18:217] == symbols[b"configuration"]
assert off[:18] == on[:18] and on[18:27] == bytes.fromhex("0902de000501008032")
assert off[27:217] == on[27:217] and off[217:] == on[240:-18]
assert on[-18:] == bytes((18, 3)) + "GRIDPICO".encode("utf-16le")
print("Off equals installed 199 B configuration, device and stock source strings")
print("On preserves device/CDC/MIDI and strings 0–5; appends only MSC and string 6")
PY
sha256sum "$output/tools.fat" "$output/image-after.fat"

# An isolated namespace has no physical network or local service route.
mkdir "$output/browser-config" "$output/browser-cache"
XDG_CONFIG_HOME="$output/browser-config" XDG_CACHE_HOME="$output/browser-cache" \
    timeout 60s unshare -Urn node "$fixture/browser.cjs" "$output/extracted"
