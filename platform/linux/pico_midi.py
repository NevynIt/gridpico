#!/usr/bin/env python3
"""Bridge one published Pico MIDI role to an exclusive ALSA raw MIDI substream."""

from collections import Counter
from contextlib import contextmanager
import ctypes as c
import errno
import os
from pathlib import Path
import re
import select
import sys
import time

from pico_usb import DeviceNode, EndpointError, Selector, UsbTarget, ready

# ALSA directions are from Linux's point of view.
OUT, IN = 0, 1
ROLES = {"1": (OUT, 0), "2": (OUT, 1), "3": (IN, 1), "5": (IN, 0)}
CHANNEL_TYPES = (0x80, 0x90, 0xa0, 0xb0, 0xe0)
CLOSED = select.POLLERR | select.POLLHUP | select.POLLNVAL
INPUT_CLOSED = CLOSED | getattr(select, "POLLRDHUP", 0)
FIFO = 1024
HOST_LIMIT = 8192
IO_TIMEOUT = 4
MAX_RELEASES = 256


def descriptors(raw):
    if not raw or len(raw) > 65536:
        raise EndpointError("invalid USB descriptor length")
    offset = 0
    while offset < len(raw):
        length = raw[offset]
        if length < 2 or offset + length > len(raw):
            raise EndpointError("invalid USB descriptor record")
        yield raw[offset:offset + length]
        offset += length


def midi_layout(raw, configuration, midi_interface):
    """Prove the reviewed two-cable USB MIDI 1.0 mapping, in wire order."""
    configs = []
    for descriptor in descriptors(raw):
        kind = descriptor[1]
        if kind == 2:
            if len(descriptor) != 9:
                raise EndpointError("invalid USB configuration record")
            configs.append((descriptor[5], []))
        if configs:
            configs[-1][1].append(descriptor)
    active = [blocks for number, blocks in configs if number == configuration]
    if len(active) != 1:
        raise EndpointError("active USB configuration is ambiguous")
    config = active[0]
    if sum(map(len, config)) != int.from_bytes(config[0][2:4], "little"):
        raise EndpointError("incomplete active USB configuration")
    interfaces = []
    for row in config[1:]:
        if row[1] == 4:
            if len(row) != 9:
                raise EndpointError("invalid USB interface record")
            interfaces.append((row, []))
        elif interfaces:
            interfaces[-1][1].append(row)
    streaming = [(row, blocks) for row, blocks in interfaces if row[5:7] == b"\x01\x03"]
    if len(streaming) != 1:
        raise EndpointError("expected one MIDIStreaming interface")
    interface, blocks = streaming[0]
    if interface[2:8] != bytes((midi_interface, 0, 2, 1, 3, 0)):
        raise EndpointError("unexpected native MIDIStreaming interface")

    controls = []
    for header, details in interfaces:
        if header[3:8] != b"\x00\x00\x01\x01\x00":
            continue
        expected = bytes((9, 0x24, 1, 0, 1, 9, 0, 1, midi_interface))
        if expected in details:
            controls.append(header[2])
    if len(controls) != 1:
        raise EndpointError("MIDI AudioControl association is ambiguous")

    jacks = {}
    endpoints = {}
    for index, row in enumerate(blocks):
        if row[1] == 0x24 and len(row) >= 5 and row[2] in (2, 3):
            if row[4] in jacks:
                raise EndpointError("duplicate MIDI jack")
            jacks[row[4]] = row[2], row[3], row[-1]
        if row[1] == 5:
            extra = blocks[index + 1] if index + 1 < len(blocks) else b""
            if (len(row) not in (7, 9) or row[3] & 3 != 2 or row[2] in endpoints
                    or len(extra) != 6 or extra[:4] != b"\x06\x25\x01\x02"):
                raise EndpointError("unproved class-specific MIDI endpoint")
            endpoints[row[2]] = tuple(extra[4:])
    if endpoints != {0x03: (1, 5), 0x83: (3, 7)} or any(
        jacks.get(jack) != identity for jack, identity in {
            1: (2, 1, 3), 5: (2, 1, 4), 3: (3, 1, 3), 7: (3, 1, 4),
        }.items()
    ):
        raise EndpointError("unexpected native MIDI endpoint or cable order")
    return controls[0]


def find_rawmidi(target, interfaces, devices=Path("/dev/snd")):
    paths = {interface.path for interface in interfaces}
    matches = []
    for entry in (target.sysfs / "class/sound").glob("midiC*"):
        indices = re.fullmatch(r"midiC([0-9]+)D([0-9]+)", entry.name)
        resolved = entry.resolve(strict=True)
        if not indices or target.path not in resolved.parents:
            continue
        card, device = (int(number) for number in indices.groups())
        if (resolved.parents[2] not in paths or resolved.parent.name != f"card{card}"
                or resolved.parents[1].name != "sound"):
            raise EndpointError("unverified RawMIDI ancestry on the selected USB device")
        matches.append((card, device, entry))
    if len(matches) != 1:
        raise EndpointError("expected one RawMIDI device on the selected USB interface")
    card, device, entry = matches[0]
    return card, device, DeviceNode.resolve(devices / entry.name, entry)


class PollFD(c.Structure):
    _fields_ = (("fd", c.c_int), ("events", c.c_short), ("revents", c.c_short))


class RawMidi:
    def __init__(self, card, device, cable, direction, node):
        library = c.CDLL("libasound.so.2")
        ptr, integer, size = c.c_void_p, c.c_int, c.c_size_t

        def bind(name, result, *args):
            function = getattr(library, "snd_rawmidi_" + name)
            function.restype = result
            function.argtypes = args
            return function

        self.close = bind("close", integer, ptr)
        self.drop = bind("drop", integer, ptr)
        self.drain = bind("drain", integer, ptr)
        self._write = bind("write", c.c_ssize_t, ptr, c.c_char_p, size)
        self._read = bind("read", c.c_ssize_t, ptr, ptr, size)
        self._read_status = bind("status", integer, ptr, ptr)
        self._available = bind("status_get_avail", size, ptr)
        self._xruns = bind("status_get_xruns", size, ptr)
        self._free_status = bind("status_free", None, ptr)
        self.handle = ptr()
        self.status = ptr()
        stream = c.byref(self.handle)
        open_raw = bind("open", integer, c.POINTER(ptr), c.POINTER(ptr), c.c_char_p, integer)
        self.checked(open_raw(stream if direction == IN else None, stream if direction == OUT else None,
                              f"hw:{card},{device},{cable}".encode("ascii"), 2), "open")
        try:
            if bind("type", integer, ptr)(self.handle) != 0:
                raise EndpointError("expected a direct ALSA hardware substream")
            with self.container(bind, "info") as info:
                self.checked(bind("info", integer, ptr, ptr)(self.handle, info), "info")
                actual = tuple(bind("info_get_" + field, integer, ptr)(info) for field in (
                    "card", "device", "subdevice", "stream", "subdevices_count",
                ))
            if actual != (card, device, cable, direction, 2):
                raise EndpointError("ALSA substream does not match the selected native cable")
            count = bind("poll_descriptors_count", integer, ptr)
            poll = bind("poll_descriptors", integer, ptr, c.POINTER(PollFD), c.c_uint)
            descriptor = PollFD()
            if count(self.handle) != 1 or poll(self.handle, c.byref(descriptor), 1) != 1:
                raise EndpointError("ALSA handle has no unique hardware file descriptor")
            self.fd = descriptor.fd
            self.events = descriptor.events
            node.guard(self.fd)
            with self.container(bind, "params") as params:
                self.checked(bind("params_current", integer, ptr, ptr)(self.handle, params), "get params")
                self.checked(bind("params_set_buffer_size", integer, ptr, ptr, size)(
                    self.handle, params, FIFO), "buffer size")
                self.checked(bind("params_set_avail_min", integer, ptr, ptr, size)(
                    self.handle, params, 3 if direction == OUT else 1), "minimum availability")
                if direction == OUT:
                    self.checked(bind("params_set_no_active_sensing", integer, ptr, ptr, integer)(
                        self.handle, params, 1), "disable automatic active sensing")
                self.checked(bind("params", integer, ptr, ptr)(self.handle, params), "set params")
            self.checked(bind("status_malloc", integer, c.POINTER(ptr))(c.byref(self.status)), "allocate status")
        except BaseException:
            self.close(self.handle)
            raise

    @staticmethod
    def checked(result, operation):
        if result < 0:
            raise EndpointError(f"ALSA {operation}: {os.strerror(-result)}")
        return result

    @classmethod
    @contextmanager
    def container(cls, bind, kind):
        data = c.c_void_p()
        malloc = bind(kind + "_malloc", c.c_int, c.POINTER(c.c_void_p))
        free = bind(kind + "_free", None, c.c_void_p)
        cls.checked(malloc(c.byref(data)), "allocate " + kind)
        try:
            yield data
        finally:
            free(data)

    def __enter__(self):
        return self

    def __exit__(self, error, *_):
        if error is not None:
            self.drop(self.handle)
        self._free_status(self.status)
        result = self.close(self.handle)
        if error is None:
            self.checked(result, "close")

    def available(self):
        self.checked(self._read_status(self.handle, self.status), "status")
        if self._xruns(self.status):
            raise EndpointError("ALSA RawMIDI byte loss")
        return self._available(self.status)

    def write(self, message):
        if self.available() < len(message):
            return False
        result = self._write(self.handle, message, len(message))
        if result in (-errno.EAGAIN, -errno.EINTR):
            return False
        if self.checked(result, "write") != len(message):
            raise EndpointError("ambiguous partial ALSA write")
        return True

    def read(self):
        data = c.create_string_buffer(256)
        result = self._read(self.handle, data, len(data))
        if result in (-errno.EAGAIN, -errno.EINTR):
            return b""
        size = self.checked(result, "read")
        if size == 0:
            raise EndpointError("ALSA RawMIDI endpoint ended")
        self.available()
        return data.raw[:size]

    def flush(self):
        result = self.drain(self.handle)
        if result in (-errno.EAGAIN, -errno.EINTR):
            return False
        self.checked(result, "drain")
        return True


class Held:
    def __init__(self, panel):
        self.notes = Counter()
        self.count = 0
        self.panel = panel

    def change(self, message):
        status, note, velocity = message
        kind = status & 0xf0
        if kind not in (0x80, 0x90) or self.panel and status & 15:
            return None
        key = status & 15, note
        if kind == 0x90 and velocity:
            return key, 0 if self.panel and self.notes[key] else 1
        return key, -1 if self.notes[key] else 0

    def accepted(self, change):
        if change:
            key, delta = change
            self.notes[key] += delta
            self.count += delta
            if not self.notes[key]:
                del self.notes[key]

    def releases(self):
        for (channel, note), count in self.notes.items():
            for _ in range(count):
                yield bytes((0x80 | channel, note, 0))


class MidiDecoder:
    """Reconstruct complete channel messages from the kernel byte stream."""

    def __init__(self):
        self.status = 0
        self.data = bytearray()

    def feed(self, chunk):
        for value in chunk:
            if value >= 0xf8:
                continue
            if value >= 0x80:
                self.status = value if value < 0xf0 else 0
                self.data.clear()
            elif self.status:
                self.data.append(value)
                kind = self.status & 0xf0
                if len(self.data) == (1 if kind in (0xc0, 0xd0) else 2):
                    if kind in CHANNEL_TYPES:
                        yield bytes((self.status, *self.data))
                    self.data.clear()


def live_poll(native, native_events, host_events, stdout_events=0):
    poller = select.poll()
    poller.register(native.fd, native_events)
    poller.register(0, host_events | INPUT_CLOSED)
    if stdout_events:
        poller.register(1, stdout_events)
    events = dict(poller.poll(100))
    device, host, stdout = (events.get(fd, 0) for fd in (native.fd, 0, 1))
    if device & CLOSED or stdout & CLOSED or host & (select.POLLERR | select.POLLNVAL):
        raise EndpointError("MIDI relay endpoint closed")
    return device, host, stdout


def send(native, guard, panel):
    held = Held(panel)
    pending = bytearray()
    deadline = None
    rejection = None
    while True:
        guard()
        if deadline is not None and time.monotonic() >= deadline:
            raise EndpointError("MIDI send stalled")
        device, host, _ = live_poll(native, native.events if len(pending) == 3 else 0,
                                    select.POLLIN if len(pending) < 3 else 0)
        guard()
        if host & INPUT_CLOSED:
            break
        if host & select.POLLIN and len(pending) < 3:
            try:
                data = os.read(0, 3 - len(pending))
            except BlockingIOError:
                continue
            if not data:
                break
            if not pending:
                deadline = time.monotonic() + IO_TIMEOUT
            pending.extend(data)
        if len(pending) < 3 or not device & select.POLLOUT:
            continue
        status, note, velocity = pending
        if ((status & 0xf0) not in (CHANNEL_TYPES if panel else (0x80, 0x90))
                or max(note, velocity) >= 0x80):
            rejection = "invalid MIDI message for the role"
            break
        message = bytes(pending)
        change = held.change(message)
        if change and change[1] > 0 and held.count == MAX_RELEASES:
            rejection = "MIDI release capacity exhausted"
            break
        if native.write(message):
            held.accepted(change)
            pending.clear()
            deadline = None

    # After retirement, ignore unread input and release only admitted holds on
    # the original handle. A kernel drain is not a receiver acknowledgment.
    deadline = time.monotonic() + IO_TIMEOUT
    for message in held.releases():
        wait_write(native, guard, deadline, lambda: native.write(message))
    wait_write(native, guard, deadline, native.flush)
    if rejection:
        raise EndpointError(rejection)


def wait_write(native, guard, deadline, action):
    poller = select.poll()
    poller.register(native.fd, native.events)
    while time.monotonic() < deadline:
        guard()
        if action():
            guard()
            return
        if any(events & CLOSED for _, events in poller.poll(100)):
            break
    raise EndpointError("MIDI retirement could not be confirmed")


def receive(native, guard, notes_only):
    decoder = MidiDecoder()
    pending = bytearray()
    deadline = None
    while True:
        guard()
        if deadline is not None and time.monotonic() >= deadline:
            raise EndpointError("MIDI receive stalled")
        device, host, stdout = live_poll(native, native.events, select.POLLIN,
                                         select.POLLOUT if pending else 0)
        guard()
        if host & INPUT_CLOSED:
            return
        if host & select.POLLIN:
            try:
                data = os.read(0, 1)
            except BlockingIOError:
                data = None
            if data == b"":
                return
            if data:
                raise EndpointError("unexpected input for receive role")
        if device & select.POLLIN:
            frames = b"".join(message for message in decoder.feed(native.read())
                              if not notes_only or message[0] & 0xf0 in (0x80, 0x90))
            if len(pending) + len(frames) > HOST_LIMIT:
                raise EndpointError("MIDI receive queue full")
            if frames and not pending:
                deadline = time.monotonic() + IO_TIMEOUT
            pending.extend(frames)
        if pending and stdout & select.POLLOUT:
            try:
                written = os.write(1, pending)
            except BlockingIOError:
                continue
            if written:
                del pending[:written]
                deadline = time.monotonic() + IO_TIMEOUT if pending else None


def run(filename, slot):
    selector = Selector.load(filename)
    direction, cable = ROLES[slot]
    with UsbTarget(selector) as target:
        control_number = midi_layout(target.descriptors(), int(target.configuration), selector.midi_interface)
        control = target.interface(control_number, 1, 1, "snd-usb-audio")
        streaming = target.interface(selector.midi_interface, 1, 3, "snd-usb-audio")
        card, device, node = find_rawmidi(target, (control, streaming))
        with RawMidi(card, device, cable, direction, node) as native:
            def guard():
                target.guard()
                control.guard()
                streaming.guard()
                node.guard(native.fd)

            guard()
            target.unique_serial()
            os.set_blocking(0, False)
            os.set_blocking(1, False)
            ready()
            if direction == OUT:
                send(native, guard, slot == "2")
            else:
                receive(native, guard, slot == "5")


if __name__ == "__main__":
    try:
        if len(sys.argv) != 3 or sys.argv[2] not in ROLES:
            raise EndpointError("usage: pico_midi.py TARGET SLOT (1, 2, 3, or 5)")
        run(sys.argv[1], sys.argv[2])
    except (OSError, ValueError, EndpointError) as error:
        print(f"Pico MIDI: {error}", file=sys.stderr)
        sys.exit(1)
