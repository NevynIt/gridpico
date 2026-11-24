#!/usr/bin/env bash
# Exercise production matching with a synthetic sysfs hierarchy and ALSA calls.
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'python3 -c "from pathlib import Path; import shutil, sys; shutil.rmtree(Path(sys.argv[1]))" "$work"' EXIT
usb="$work/sys/devices/1-2"
midi="$usb/1-2:1.1"
control="$usb/1-2:1.0"
cards="$work/sys/class/sound"
bus="$work/sys/bus/usb/devices"
mkdir -p "$midi" "$control" "$cards/card3" "$cards/card4" "$bus" "$work/sys/devices/pci"
ln -s "$usb" "$bus/1-2"
ln -s "$midi" "$cards/card3/device"
ln -s "$work/sys/devices/pci" "$cards/card4/device"
printf '1234\n' > "$usb/idVendor"
printf 'abcd\n' > "$usb/idProduct"
printf 'GP42\n' > "$usb/serial"
printf '01\n' > "$midi/bInterfaceClass"
printf '03\n' > "$midi/bInterfaceSubClass"
printf '01\n' > "$midi/bInterfaceNumber"
printf '01\n' > "$control/bInterfaceClass"
printf '01\n' > "$control/bInterfaceSubClass"
printf '00\n' > "$control/bInterfaceNumber"

mapfile -t symbols < <(sed -nE 's/^.* __wrap_([a-z0-9_]+)\(.*/-Wl,--wrap=\1/p' platform/linux/test_usb_identity.c)
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wpedantic -I. platform/linux/test_usb_identity.c \
    platform/linux/linux_midi.c platform/linux/usb_identity.c "${symbols[@]}" -lasound -o "$work/check"
"$work/check" "$work/sys" verified

printf '\n' > "$usb/serial"
"$work/check" "$work/sys" unavailable
printf 'GP42\n' > "$usb/serial"
mkdir "$work/sys/devices/1-3"
cp "$usb/idVendor" "$usb/idProduct" "$usb/serial" "$work/sys/devices/1-3"
ln -s "$work/sys/devices/1-3" "$bus/1-3"
"$work/check" "$work/sys" unavailable
unlink "$bus/1-3"

printf '03\n' > "$control/bInterfaceSubClass"
"$work/check" "$work/sys" unavailable
printf '01\n' > "$control/bInterfaceSubClass"
printf '02\n' > "$midi/bInterfaceNumber"
"$work/check" "$work/sys" unavailable
printf '01\n' > "$midi/bInterfaceNumber"
unlink "$cards/card3/device"
ln -s "$control" "$cards/card3/device"
"$work/check" "$work/sys" unavailable
unlink "$cards/card3/device"
"$work/check" "$work/sys" unavailable

printf '%s\n' 'USB matcher: direction, ambiguity, sysfs correlation, output gate and input generation rollback passed'
