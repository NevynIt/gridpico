#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
compose=(docker compose -f "$root/.devcontainer/compose.yaml")

case ${1:-} in
--alsa|--alsa-probe|--alsa-config)
  mode=$1
  shift
  alsa_nodes=(/dev/snd/seq)
  while [[ ${1:-} == --alsa-node ]]; do
    node=${2:-}
    if [[ ! $node =~ ^/dev/snd/(controlC[0-9]+|pcmC[0-9]+D[0-9]+[pc])$ ]]; then
      printf '%s\n' '--alsa-node expects an explicit /dev/snd/controlC# or /dev/snd/pcmC#D#[pc].' >&2
      exit 2
    fi
    for previous in "${alsa_nodes[@]}"; do
      if [[ $node == "$previous" ]]; then
        printf 'Duplicate ALSA device %s.\n' "$node" >&2
        exit 2
      fi
    done
    alsa_nodes+=("$node")
    shift 2
  done
  if [[ $mode != --alsa && $# != 0 ]]; then
    printf 'Usage: ./scripts/start.sh %s [--alsa-node DEVICE ...]\n' "$mode" >&2
    exit 2
  fi

  alsa_groups=()
  docker_devices=()
  probe_nodes=()
  for node in "${alsa_nodes[@]}"; do
    if [[ ! -c "$node" ]]; then
      printf 'ALSA needs the character device %s on the Linux Docker host.\n' "$node" >&2
      exit 1
    fi
    if ! device_gid="$(stat -Lc '%g' -- "$node")" || [[ ! $device_gid =~ ^[0-9]+$ ]]; then
      printf 'Could not determine the numeric group of %s.\n' "$node" >&2
      exit 1
    fi
    group=$device_gid
    # Use pico's established primary group for a device owned by group zero.
    if [[ $group == 0 ]]; then
      group=1000
    fi
    alsa_groups+=("$group")
    docker_devices+=(--device "$node:$node:rw" --group-add "$group")
    probe_nodes+=("$node" "$device_gid" "$group")
  done
  export GRIDPICO_ALSA_SEQ_GID="${alsa_groups[0]}"

  if [[ $mode != --alsa-config ]] &&
    ! docker run --rm --pull=never --network=none --read-only --user pico --cap-drop=ALL \
      --security-opt=no-new-privileges:true "${docker_devices[@]}" \
      --entrypoint python3 gridpico-dev:latest -c '
import ctypes
import os
import sys

uid = os.geteuid()
groups = sorted(set(os.getgroups()) | {os.getegid()})
print(f"pico UID {uid}, groups {groups}", flush=True)
if uid == 0:
    raise SystemExit("Expected a non-root pico process")
for node, expected_gid, group in zip(sys.argv[1::3], sys.argv[2::3], sys.argv[3::3]):
    gid = os.stat(node).st_gid
    print(f"{node} GID {gid}", flush=True)
    if gid != int(expected_gid) or int(group) not in groups:
        raise SystemExit(f"Numeric group mismatch for {node}")
    if node != sys.argv[1]:
        try:
            fd = os.open(node, os.O_RDWR | os.O_NONBLOCK)
        except OSError as error:
            raise SystemExit(f"Could not open {node} for read/write: {error.strerror}")
        os.close(fd)

alsa = ctypes.CDLL("libasound.so.2")
alsa.snd_seq_open.argtypes = (ctypes.POINTER(ctypes.c_void_p), ctypes.c_char_p,
                             ctypes.c_int, ctypes.c_int)
alsa.snd_seq_close.argtypes = (ctypes.c_void_p,)
alsa.snd_strerror.argtypes = (ctypes.c_int,)
alsa.snd_strerror.restype = ctypes.c_char_p
sequencer = ctypes.c_void_p()
# SND_SEQ_OPEN_DUPLEX and SND_SEQ_NONBLOCK.
result = alsa.snd_seq_open(ctypes.byref(sequencer), b"hw", 3, 1)
if result < 0:
    raise SystemExit(f"Could not open ALSA sequencer in duplex mode: {alsa.snd_strerror(result).decode()}")
alsa.snd_seq_close(sequencer)
print("Opened ALSA sequencer in duplex mode as pico")
' "${probe_nodes[@]}"; then
    printf 'ALSA probe failed. Verify the local image and effective device group or ACL on the Docker host.\n' >&2
    exit 1
  fi
  if [[ $mode == --alsa-probe ]]; then
    exit 0
  fi
  compose+=(-f "$root/.devcontainer/compose.alsa.yaml")
  if (( ${#alsa_nodes[@]} > 1 )); then
    alsa_override="$(mktemp "${TMPDIR:-/tmp}/gridpico-alsa.XXXXXXXX")"
    trap 'rm -f -- "$alsa_override"' EXIT
    {
      printf 'services:\n  dev:\n    devices:\n'
      for node in "${alsa_nodes[@]:1}"; do
        printf '      - "%s:%s:rw"\n' "$node" "$node"
      done
      printf '    group_add:\n'
      for group in "${alsa_groups[@]:1}"; do
        printf '      - "%s"\n' "$group"
      done
    } >"$alsa_override"
    compose+=(-f "$alsa_override")
  fi
  if [[ $mode == --alsa-config ]]; then
    "${compose[@]}" config --quiet
  else
    "${compose[@]}" up -d --no-build dev "$@"
  fi
  exit
  ;;
esac

# Starting never silently rebuilds the image.
exec "${compose[@]}" up -d --no-build dev "$@"
