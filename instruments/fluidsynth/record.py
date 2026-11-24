#!/usr/bin/env python3
"""Run one private FluidSynth take for the local router host."""

import argparse
from collections import Counter
import os
from pathlib import Path
import re
import resource
import secrets
import select
import shutil
import signal
import stat
import struct
import subprocess
import sys
import time

from run import engine_command, finish, reserve_wav, soundfont


WAV_NAME = "recording.wav"
WAV_LIMIT = 12 * 1024 * 1024
START_SECONDS = 10
TAKE_SECONDS = 60
TAIL_SECONDS = 1
REQUEST_SECONDS = 0.5
MAX_HELD = 50  # The configured router's maximum input occurrences.
MIDI_COMMAND = re.compile(rb"midi ([0-9a-fA-F]{6})")


class TakeError(Exception):
    pass


def announce(line):
    print(line, flush=True)


def private_directory(value):
    path = Path(value)
    try:
        info = path.lstat()
    except OSError as error:
        raise argparse.ArgumentTypeError("Use an existing private take directory.") from error
    if (not path.is_absolute() or not stat.S_ISDIR(info.st_mode) or
            info.st_uid != os.geteuid() or stat.S_IMODE(info.st_mode) != 0o700):
        raise argparse.ArgumentTypeError("Use an absolute, owner-only take directory (0700).")
    return path


class Pipe:
    def __init__(self, stream, failure):
        self.fd = stream.fileno()
        self.failure = failure
        self.pending = bytearray()

    def read(self):
        chunk = os.read(self.fd, 4096)
        if not chunk:
            raise TakeError(self.failure)
        self.pending.extend(chunk)

    def line(self):
        end = self.pending.find(b"\n")
        if (end < 0 and len(self.pending) > 32) or end > 32:
            raise TakeError(self.failure)
        if end < 0:
            return None
        line = bytes(self.pending[:end])
        del self.pending[:end + 1]
        return line


def validate_wav(path):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(fd, "rb") as stream:
        info = os.fstat(fd)
        size = info.st_size
        if not stat.S_ISREG(info.st_mode) or not 44 <= size <= WAV_LIMIT:
            raise TakeError("WAV")
        header = stream.read(12)
        if (len(header) != 12 or header[:4] != b"RIFF" or header[8:] != b"WAVE" or
                int.from_bytes(header[4:8], "little") != size - 8):
            raise TakeError("WAV")
        has_format = has_samples = False
        while stream.tell() < size:
            header = stream.read(8)
            if len(header) != 8:
                raise TakeError("WAV")
            tag, length = struct.unpack("<4sI", header)
            end = stream.tell() + length + (length % 2)
            if end > size:
                raise TakeError("WAV")
            if tag == b"fmt ":
                pcm = struct.pack("<HHIIHH", 1, 2, 44100, 176400, 4, 16)
                if has_format or length < len(pcm) or stream.read(len(pcm)) != pcm:
                    raise TakeError("WAV")
                has_format = True
            elif tag == b"data":
                if not has_format or has_samples or length == 0 or length % 4:
                    raise TakeError("WAV")
                has_samples = True
            stream.seek(end)
        if not has_samples:
            raise TakeError("WAV")


class Take:
    def __init__(self, settings):
        self.settings = settings
        self.wav = settings.work_dir / WAV_NAME
        self.reserved = False
        self.cancelled = False
        self.engine = None
        self.sender = None
        self.control = Pipe(sys.stdin, "HOST_LOST")
        self.held = Counter()
        self.hold_count = 0

    def cancel(self, _signal, _frame):
        self.cancelled = True

    def read(self, pipe, deadline):
        sources = [pipe] if pipe is self.alsa else [self.alsa, pipe]
        by_fd = {source.fd: source for source in sources}
        while True:
            if self.cancelled:
                raise TakeError("CANCELLED")
            if self.engine.poll() is not None:
                raise TakeError("ENGINE_LOST")
            if self.sender.poll() is not None:
                raise TakeError("ALSA_LOST")
            if pipe is not self.alsa and self.alsa.line() is not None:
                raise TakeError("ALSA_LOST")
            delay = deadline - time.monotonic()
            if delay <= 0:
                return None
            # Check for an asynchronous ALSA fault before a buffered host command.
            ready, _, _ = select.select(by_fd, [], [], 0)
            if pipe is not self.alsa and self.alsa.fd in ready:
                self.alsa.read()
                continue
            line = pipe.line()
            if line is not None:
                if line == b"fault" and pipe is self.control:
                    raise TakeError("SOCKET_LOST")
                return line
            if not ready:
                ready, _, _ = select.select(by_fd, [], [], min(delay, 0.1))
            for fd in ready:
                by_fd[fd].read()

    def send(self, note, deadline):
        if time.monotonic() >= deadline:
            raise TakeError("ALSA_TIMEOUT")
        deadline = min(deadline, time.monotonic() + REQUEST_SECONDS)
        self.sender.stdin.write(b"midi " + note.hex().encode("ascii") + b"\n")
        if self.read(self.alsa, deadline) != b"ok":
            raise TakeError("ALSA_LOST")

    def midi(self, command, deadline):
        match = MIDI_COMMAND.fullmatch(command)
        if match is None:
            raise TakeError("INVALID")
        note = bytes.fromhex(match[1].decode("ascii"))
        status, key, velocity = note
        if status >> 4 not in (8, 9) or key > 127 or velocity > 127:
            raise TakeError("INVALID")
        identity = (status & 0xF, key)
        if status >> 4 == 9 and velocity:
            if self.hold_count == MAX_HELD:
                raise TakeError("CAPACITY")
            self.send(note, deadline)
            self.held[identity] += 1
            self.hold_count += 1
        elif self.held[identity]:
            self.send(note, deadline)
            self.held[identity] -= 1
            self.hold_count -= 1
            if not self.held[identity]:
                del self.held[identity]

    def start(self, deadline):
        binary = shutil.which("fluidsynth")
        if binary is None:
            raise TakeError("ENGINE_UNAVAILABLE")
        reserve_wav(self.wav)
        self.reserved = True
        name = "GPRec" + secrets.token_hex(5)
        command = engine_command(binary, self.settings.soundfont, name, wav=self.wav,
                                 midi_channels=16)
        self.engine = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=sys.stderr, bufsize=0)
        self.sender = subprocess.Popen(
            [self.settings.alsa_sender, str(self.engine.pid), name],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0,
        )
        self.alsa = Pipe(self.sender.stdout, "ALSA_LOST")
        if self.read(self.alsa, deadline) != b"ready":
            raise TakeError("ALSA_UNAVAILABLE")
        announce("ready")
        attached = self.read(self.control, deadline)
        if attached != b"attached":
            raise TakeError("ATTACH_TIMEOUT" if attached is None else "INVALID")
        announce("recording")

    def finish_sender(self):
        sender, self.sender = self.sender, None
        sender.stdin.close()
        try:
            return sender.wait(timeout=REQUEST_SECONDS)
        except subprocess.TimeoutExpired:
            sender.kill()
            sender.wait()
            return 1
        finally:
            sender.stdout.close()

    def finish_engine(self):
        engine, self.engine = self.engine, None
        return finish(engine)

    def read_after_stop(self, deadline):
        # Host and helper time limits can request stop together. Ignore in-flight MIDI.
        while True:
            command = self.read(self.control, deadline)
            if command is None or (command != b"stop" and MIDI_COMMAND.fullmatch(command) is None):
                return command

    def capture(self):
        self.start(time.monotonic() + START_SECONDS)
        deadline = time.monotonic() + TAKE_SECONDS
        while (command := self.read(self.control, deadline)) not in (b"stop", None):
            self.midi(command, deadline)

        drain_deadline = time.monotonic() + REQUEST_SECONDS
        for (channel, key), count in self.held.items():
            for _ in range(count):
                self.send(bytes((0x80 | channel, key, 0)), drain_deadline)
        announce("closing")
        if self.read_after_stop(time.monotonic() + REQUEST_SECONDS) != b"closed":
            raise TakeError("CLOSE_FAILED")
        if self.read_after_stop(time.monotonic() + TAIL_SECONDS) is not None:
            raise TakeError("INVALID")
        if self.finish_sender() != 0:
            raise TakeError("ALSA_LOST")
        if self.engine.poll() is not None:
            raise TakeError("ENGINE_LOST")
        if self.finish_engine() != 0:
            raise TakeError("ENGINE_EXIT")
        if self.cancelled:
            raise TakeError("CANCELLED")
        validate_wav(self.wav)

    def cleanup(self, complete):
        if self.sender is not None:
            self.finish_sender()
        if self.engine is not None:
            self.finish_engine()
        if self.reserved and not complete:
            self.wav.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--soundfont", required=True, type=soundfont)
    parser.add_argument("--alsa-sender", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=private_directory)
    settings = parser.parse_args()
    if not settings.alsa_sender.is_absolute():
        parser.error("--alsa-sender requires an absolute path")

    take = Take(settings)
    for signum in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(signum, take.cancel)
    soft, hard = resource.getrlimit(resource.RLIMIT_FSIZE)
    resource.setrlimit(resource.RLIMIT_FSIZE,
                       (min(WAV_LIMIT, soft) if soft >= 0 else WAV_LIMIT, hard))
    complete = False
    error = None
    try:
        take.capture()
        announce("complete")
        complete = True
    except TakeError as failure:
        error = str(failure)
    except OSError as failure:
        print(f"FluidSynth take: {failure}", file=sys.stderr)
        error = "IO"
    finally:
        take.cleanup(complete)
    if complete:
        return 0
    announce(f"error {error}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
