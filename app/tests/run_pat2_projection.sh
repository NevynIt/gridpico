#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
build=$(mktemp -d)
trap 'rm -rf -- "$build"' EXIT
python3 - "$build/firmware.inc" <<'PY'
from pathlib import Path
import sys

source = Path("platform/rp2040/router/firmware.c").read_text()
def span(first, last):
    start = source.index(first)
    return source[start:source.index(last, start)]

Path(sys.argv[1]).write_text("\n".join([
    span("typedef struct {\n    bool present;", "static gridpico_pico_router router;"),
    "static host_interface interfaces[CFG_TUH_MIDI];",
    span("typedef struct {\n    uint16_t vid, pid, revision;", "static gridpico_pico_auto_routes automatic_routes;"),
    span("static bool observed_interface(", "static bool probe_still_current("),
    span("static unsigned cable_span(", "static gridpico_result lookup_lane_kind("),
    span("static bool compatible_panel_input(", "static bool panel_input_fenced("),
    span("static bool recognizable_controller(", "static bool placed_port("),
    span("static bool same_serial(", "static bool identity_conflict("),
    span("static void automatic_policy_blocks(", "static void service_automatic_policy("),
]))
PY
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -g -O1 -ffunction-sections -fdata-sections \
    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-pie -no-pie \
    -DPICO_ON_DEVICE=1 -DPICO_RP2040=1 -DGRIDPICO_RP2040_PATCHBAY_BENCH \
    -DGRIDPICO_RP2040_MATRIX_CLI -I. -I"$build" -Wl,--gc-sections \
    app/tests/pat2_projection_host.c app/rp2040_router.c app/rp2040_snapshot_pat2.c \
    core/midi/patch_graph.c core/session/patch_session.c \
    frontends/apc/apc_frontend.c frontends/apc/apc_protocol.c \
    frontends/apc/apc_physical.c frontends/apc/apc_chooser.c -o "$build/projection"
"$build/projection"
