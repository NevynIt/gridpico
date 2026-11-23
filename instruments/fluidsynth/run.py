#!/usr/bin/env python3
"""Run an independent FluidSynth MIDI destination in the foreground."""

import argparse
import os
from pathlib import Path
import re
import select
import shutil
import signal
import subprocess
import sys


def soundfont(path):
    path = Path(path).absolute()
    try:
        if not path.is_file():
            raise OSError
        with path.open("rb") as stream:
            header = stream.read(12)
    except OSError:
        raise argparse.ArgumentTypeError(f"SoundFont must be a readable file: {path}") from None
    if header[:4] != b"RIFF" or header[8:] != b"sfbk":
        raise argparse.ArgumentTypeError(f"File does not have a SoundFont header: {path}")
    return path


def midi_name(name):
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,15}", name) or name == "pid":
        raise argparse.ArgumentTypeError("Use 1–15 ASCII letters, digits, _ or -, excluding 'pid'.")
    return name


def alsa_device(name):
    if not name or name != name.strip() or not name.isprintable():
        raise argparse.ArgumentTypeError("Provide a non-empty ALSA PCM device name.")
    return name


def wav_path(path):
    path = Path(path).absolute()
    if path.suffix.lower() != ".wav":
        raise argparse.ArgumentTypeError("Use a fresh output path ending in .wav.")
    return path


def wait_for_stop(engine, stop_requested):
    line = bytearray()
    while not stop_requested() and engine.poll() is None:
        ready, _, _ = select.select([sys.stdin], [], [], 0.2)
        if not ready:
            continue
        chunk = os.read(sys.stdin.fileno(), 4096)
        if not chunk:
            return
        for byte in chunk:
            if byte == ord("\n"):
                if line in (b"quit", b"quit\r"):
                    return
                line.clear()
            elif len(line) <= len(b"quit\r"):
                line.append(byte)


def finish(engine):
    if engine.poll() is None:
        try:
            engine.stdin.write(b"quit\n")
        except BrokenPipeError:
            pass
    engine.stdin.close()
    try:
        status = engine.wait(timeout=5)
    except subprocess.TimeoutExpired:
        print("FluidSynth did not exit after quit. Stopping the child process.", file=sys.stderr)
        engine.kill()
        engine.wait()
        return 1
    return status if status >= 0 else 128 - status


def run(command):
    stopping = False

    def request_stop(_signal, _frame):
        nonlocal stopping
        stopping = True

    for signum in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(signum, request_stop)

    # Keep terminal signals on the wrapper so it can close the WAV cleanly.
    engine = subprocess.Popen(command, stdin=subprocess.PIPE, bufsize=0, start_new_session=True)
    print("FluidSynth started. Enter quit or press Ctrl+C to stop.", flush=True)
    try:
        wait_for_stop(engine, lambda: stopping)
    finally:
        status = finish(engine)
    return status


def main():
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--soundfont", required=True, type=soundfont, metavar="FILE")
    parser.add_argument("--midi-name", type=midi_name, default="GridpicoFluid", metavar="NAME")
    sink = parser.add_mutually_exclusive_group(required=True)
    sink.add_argument("--alsa-device", type=alsa_device, metavar="PCM")
    sink.add_argument("--wav", type=wav_path, metavar="FILE")
    settings = parser.parse_args()
    engine = shutil.which("fluidsynth")
    if engine is None:
        parser.exit(1, "FluidSynth was not found on PATH. Use an installed fluidsynth executable.\n")

    command = [
        engine, "-f", "/dev/null", "-r", "44100", "-g", "0.65", "-m", "alsa_seq",
        "-o", f"midi.alsa_seq.id={settings.midi_name}", "-o", f"midi.portname={settings.midi_name}",
        "-o", "midi.autoconnect=0",
        "-o", "synth.reverb.active=0", "-o", "synth.chorus.active=0",
    ]
    if settings.wav is not None:
        try:
            fd = os.open(settings.wav, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            os.close(fd)
        except OSError as error:
            parser.exit(1, f"Cannot create WAV {settings.wav}: {error.strerror}\n")
        command.extend(("-a", "file", "-o", "audio.file.type=wav", "-o", "audio.file.format=s16",
                        "-o", f"audio.file.name={settings.wav}"))
    else:
        command.extend(("-a", "alsa", "-o", f"audio.alsa.device={settings.alsa_device}"))
    command.append(str(settings.soundfont))
    try:
        return run(command)
    except OSError as error:
        parser.exit(1, f"FluidSynth wrapper: {error}\n")


if __name__ == "__main__":
    sys.exit(main())
