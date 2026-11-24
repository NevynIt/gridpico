#!/usr/bin/env python3
"""Relay one verified Pico ACM endpoint over the private stdio protocol."""

import errno
import fcntl
import os
from pathlib import Path
import re
import select
import stat
import struct
import sys
import termios
import time
import tty

from pico_usb import DeviceNode, EndpointError, Selector, UsbTarget, ready

LIMIT = 8192
STALL = 5
CLOSED = select.POLLERR | select.POLLHUP | select.POLLNVAL
INPUT_CLOSED = CLOSED | getattr(select, "POLLRDHUP", 0)


def find_tty(target, interface, devices=Path("/dev")):
    matches = []
    for entry in (target.sysfs / "class/tty").glob("ttyACM*"):
        if (re.fullmatch(r"ttyACM[0-9]+", entry.name)
                and (entry / "device").resolve(strict=True) == interface.path):
            matches.append(entry)
    if len(matches) != 1:
        raise EndpointError("expected one ACM device on the selected interface")
    entry = matches[0]
    return DeviceNode.resolve(devices / entry.name, entry)


def require_unused(node, proc=Path("/proc")):
    for process in proc.iterdir():
        if not process.name.isdecimal() or process.name == str(os.getpid()):
            continue
        try:
            owner = process.stat().st_uid
        except FileNotFoundError:
            continue
        if owner != os.geteuid():
            continue
        # A nondumpable same-UID process can have hidden descriptors. Inspect
        # visible holders and use TIOCEXCL to protect later ordinary opens.
        try:
            handles = list((process / "fd").iterdir())
        except OSError as error:
            if error.errno not in (errno.ENOENT, errno.EACCES):
                raise
            continue
        for handle in handles:
            try:
                info = handle.stat()
            except OSError as error:
                if error.errno not in (errno.ENOENT, errno.EACCES):
                    raise
                continue
            if stat.S_ISCHR(info.st_mode) and info.st_rdev == node.number:
                raise EndpointError("selected ACM endpoint is already open")


def configure(fd, guard):
    tty.setraw(fd, termios.TCSANOW)
    attrs = termios.tcgetattr(fd)
    attrs[0] &= ~(termios.IXON | termios.IXOFF | termios.IXANY)
    attrs[2] &= ~(termios.CSIZE | termios.PARENB | termios.CSTOPB | termios.CRTSCTS)
    attrs[2] |= termios.CS8 | termios.CREAD | termios.CLOCAL | termios.HUPCL
    attrs[4:6] = [termios.B115200, termios.B115200]
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    dtr = struct.pack("i", termios.TIOCM_DTR)
    fcntl.ioctl(fd, termios.TIOCMBIC, dtr)
    time.sleep(0.2)
    guard()
    termios.tcflush(fd, termios.TCIFLUSH)
    fcntl.ioctl(fd, termios.TIOCMBIS, dtr)
    if not struct.unpack("i", fcntl.ioctl(fd, termios.TIOCMGET, bytes(4)))[0] & termios.TIOCM_DTR:
        raise EndpointError("DTR assertion failed")


def relay(fd, guard):
    os.set_blocking(0, False)
    os.set_blocking(1, False)
    to_device = bytearray()
    to_host = bytearray()
    deadlines = {}
    while True:
        guard()
        if any(deadline <= time.monotonic() for deadline in deadlines.values()):
            raise EndpointError("CDC relay I/O stalled")
        poller = select.poll()
        poller.register(0, (select.POLLIN if len(to_device) < LIMIT else 0) | INPUT_CLOSED)
        poller.register(1, select.POLLOUT if to_host else 0)
        poller.register(fd, (select.POLLIN if len(to_host) < LIMIT else 0)
                        | (select.POLLOUT if to_device else 0))
        events = dict(poller.poll(100))
        host = events.get(0, 0)
        native = events.get(fd, 0)
        output = events.get(1, 0)
        if native & CLOSED or output & CLOSED or host & (select.POLLERR | select.POLLNVAL):
            raise EndpointError("CDC relay endpoint closed")
        if host & INPUT_CLOSED:
            return
        guard()
        for source, destination, pending, flags in (
            (0, fd, to_device, host), (fd, 1, to_host, native),
        ):
            if not flags & select.POLLIN or len(pending) == LIMIT:
                continue
            try:
                data = os.read(source, min(256, LIMIT - len(pending)))
            except BlockingIOError:
                continue
            if not data:
                if source == 0:
                    return
                raise EndpointError("CDC device reached EOF")
            if not pending:
                deadlines[destination] = time.monotonic() + STALL
            pending.extend(data)
        for destination, pending, flags in ((fd, to_device, native), (1, to_host, output)):
            if pending and flags & select.POLLOUT:
                try:
                    written = os.write(destination, pending)
                except BlockingIOError:
                    continue
                if written:
                    del pending[:written]
                    if pending:
                        deadlines[destination] = time.monotonic() + STALL
                    else:
                        deadlines.pop(destination)


def run(filename):
    selector = Selector.load(filename)
    with UsbTarget(selector) as target:
        interface = target.interface(selector.cdc_interface, 2, 2, "cdc_acm")
        node = find_tty(target, interface)
        require_unused(node)
        fd = os.open(node.path, os.O_RDWR | os.O_NONBLOCK | os.O_NOCTTY | os.O_NOFOLLOW)
        try:
            fcntl.ioctl(fd, termios.TIOCEXCL)

            def guard():
                target.guard()
                interface.guard()
                node.guard(fd)

            guard()
            require_unused(node)
            configure(fd, guard)
            guard()
            target.unique_serial()
            ready()
            relay(fd, guard)
        finally:
            os.close(fd)


if __name__ == "__main__":
    try:
        if len(sys.argv) != 2:
            raise EndpointError("usage: pico_cdc.py TARGET")
        run(sys.argv[1])
    except (OSError, ValueError, EndpointError) as error:
        print(f"Pico CDC: {error}", file=sys.stderr)
        sys.exit(1)
