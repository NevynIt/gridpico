#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
build=$(mktemp -d)
trap 'rm -rf -- "$build"' EXIT

# Compile production discovery and transport functions with the fixture's USB
# boundary. The MCU startup and PIO drivers remain outside it.
python3 - "$build/firmware.inc" <<'PY'
from pathlib import Path
import sys
from textwrap import dedent, indent

source = Path("platform/rp2040/router/firmware.c").read_text()

def span(first, last, after=0):
    start = source.index(first, after)
    return source[start:source.index(last, start)]

service = source.index("static void service_host(void)")
demux = span("            const uint8_t cable = packet.bytes[0] >> 4;",
             "\n        }\n    }\n    service_discovery();", service)
Path(sys.argv[1]).write_text("\n".join([
    span("typedef struct {\n    bool present;", "static gridpico_pico_router router;"),
    "static host_interface interfaces[CFG_TUH_MIDI];",
    span("typedef struct {\n    uint16_t vid, pid, revision;", "static gridpico_pico_auto_routes automatic_routes;"),
    span("static bool observed_path(", "static bool probe_still_current("),
    span("static void report_discovery_capacity(", "static bool panel_input_fenced("),
    span("static bool panel_input_fenced(", "enum { KEYS_LED_QUEUE"),
    span("enum { KEYS_LED_QUEUE", "static uint8_t keys_voice_length("),
    span("static bool keys_led_current(", "static bool keys_native_current("),
    span("static bool keys_pad_restore(", "static bool dispatch_keys_host("),
    span("static gridpico_result sync_apc_roles(", "static void copy_apc_status("),
    span("static gridpico_result set_led_eligibility(", "static gridpico_result apply_apc_role("),
    span("static bool same_serial(", "static void admit_without_strings("),
    span("static bool recognizable_controller(", "static bool pat2_shape_known("),
    "#ifdef GRIDPICO_RP2040_PICO7_NORMAL",
    "static gridpico_pico_auto_routes automatic_routes;",
    span("static void admit_without_strings(", "static void service_discovery("),
    span("static bool pat2_shape_known(", "static void retire_host_interface("),
    "#endif",
    span("static void retire_host_interface(", "#endif\n\n#ifdef GRIDPICO_RP2040_PIO_DIAGNOSTICS"),
    span("bool tuh_enum_descriptor_configuration_cb(", "\n#endif\n\n#ifdef GRIDPICO_PICO_GENERIC_MIDI\nvoid tuh_umount_cb("),
    span("void tuh_umount_cb(", "\n#endif\n\n#ifdef GRIDPICO_PICO_GENERIC_MIDI\nstatic uint8_t endpoint_cables("),
    span("static uint8_t endpoint_cables(", "\n#endif"),
    span("void tuh_midi_descriptor_cb(", "#ifdef GRIDPICO_RP2040_HCD_TRACE\nbool __real_midih_xfer_cb("),
    span("static bool current_musical_flight(", "\n#else\nbool __wrap_midih_xfer_cb("),
    span("static bool submit_host_packet(", "\n#else\nvoid tuh_midi_tx_cb("),
    span("static void receive(gridpico_pico_peer", "#ifdef GRIDPICO_PICO_GENERIC_MIDI\nstatic void service_host"),
    # Only the loop/USB FIFO seam is supplied by the host. The demux body
    # comes verbatim from the production host foreground loop.
    "static void receive_discovered_packet(const discovered_device *binding, gridpico_pico_packet packet)\n{\n"
        + indent(dedent(demux), "    ") + "\n}\n",
]))
PY

case "${1:-}" in
    '') modes=(stock pico7) ;;
    --normal-boot) modes=(normal) ;;
    --normal-pages) modes=(normal-pages) ;;
    --normal-pages-bitmap) modes=(normal-pages-bitmap); [[ $# -eq 1 || $# -eq 3 ]] ;;
    --pages-13) modes=(pages-13) ;;
    *) printf 'usage: %s [--normal-boot|--normal-pages|--normal-pages-bitmap [MACHINE_PAGES BOOT_PAGES]|--pages-13]\n' "$0" >&2; exit 2 ;;
esac
for mode in "${modes[@]}"; do
    opt_in=()
    boot_sources=()
    case "$mode" in
        pico7) opt_in=(-DGRIDPICO_PICO7_HOST_FIXTURE) ;;
        normal|normal-pages|normal-pages-bitmap)
            opt_in=(-DGRIDPICO_RP2040_PICO7_NORMAL -DGRIDPICO_RP2040_PAT2_BOOT_READ)
            if [[ $mode != normal ]]; then opt_in+=(-DGRIDPICO_RP2040_ROUTING_PAGES); fi
            if [[ $mode == normal-pages-bitmap ]]; then opt_in+=(-DGRIDPICO_PICO25_HOST_FIXTURE); fi
            boot_sources=(app/rp2040_pat2_apply.c app/rp2040_auto_routes.c app/rp2040_snapshot_pat2.c
                          app/rp2040_snapshot.c)
            ;;
        pages-13) opt_in=(-DGRIDPICO_RP2040_ROUTING_PAGES) ;;
    esac
    "${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror -g -O0 \
        -fsanitize=address,undefined -fno-sanitize-recover=all -fno-pie -no-pie \
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -DPICO_ON_DEVICE=1 -DPICO_RP2040=1 -DGRIDPICO_RP2040_MATRIX_CLI \
        -DGRIDPICO_RP2040_PATCHBAY_BENCH "${opt_in[@]}" -I. -I"$build" \
        app/tests/pico7_two_hub_admission.c app/rp2040_router.c \
        core/midi/patch_graph.c core/session/patch_session.c frontends/cli/cli_frontend.c \
        frontends/apc/apc_frontend.c frontends/apc/apc_protocol.c frontends/apc/apc_physical.c \
        "${boot_sources[@]}" -o "$build/$mode"
    if [[ $mode == normal-pages-bitmap ]]; then
        "$build/$mode" machine "${2:-}"
        "$build/$mode" boot "${3:-}"
    else
        "$build/$mode"
    fi
done
