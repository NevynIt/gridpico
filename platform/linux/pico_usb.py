"""Identity checks shared by the two private Linux Pico endpoint helpers."""

from dataclasses import dataclass
import json
import os
from pathlib import Path
import re
import stat
import unicodedata


class EndpointError(RuntimeError):
    pass


@dataclass(frozen=True)
class Selector:
    mode: str
    usb_path: str
    vid: str
    pid: str
    bcd_device: str
    product: str
    serial: str | None
    cdc_interface: int
    midi_interface: int

    @staticmethod
    def load(filename):
        path = Path(filename)
        if not path.is_absolute():
            raise EndpointError("selector must be an absolute path")

        def object_fields(items):
            fields = dict(items)
            if len(fields) != len(items):
                raise EndpointError("duplicate selector field")
            return fields

        with os.fdopen(os.open(path, os.O_RDONLY | os.O_NONBLOCK), "rb") as source:
            if not stat.S_ISREG(os.fstat(source.fileno()).st_mode):
                raise EndpointError("selector must be a regular file")
            data = source.read(4097)
        if len(data) > 4096:
            raise EndpointError("selector is too large")
        fields = json.loads(data, object_pairs_hook=object_fields)
        if not isinstance(fields, dict) or set(fields) != {
            "mode", "usbPath", "vid", "pid", "bcdDevice", "product", "serial",
            "cdcInterface", "midiInterface",
        }:
            raise EndpointError("invalid selector fields")
        mode, path, vid, pid, bcd, product, serial, cdc, midi = (
            fields[k] for k in ("mode", "usbPath", "vid", "pid", "bcdDevice",
                                "product", "serial", "cdcInterface", "midiInterface")
        )

        def text(value):
            return (isinstance(value, str) and 1 <= len(value) <= 128
                    and value == value.strip()
                    and not any(unicodedata.category(c) in {"Cc", "Cf"} for c in value))

        if not isinstance(path, str) or len(path) > 48 or not re.fullmatch(
            r"[1-9][0-9]*-[1-9][0-9]*(?:\.[1-9][0-9]*)*", path
        ):
            raise EndpointError("invalid physical USB path")
        if not all(isinstance(s, str) and re.fullmatch(r"[0-9a-f]{4}", s)
                   for s in (vid, pid, bcd)):
            raise EndpointError("invalid USB identifiers")
        if not text(product) or cdc == midi or not all(
            type(n) is int and 0 <= n <= 255 for n in (cdc, midi)
        ):
            raise EndpointError("invalid product or interface")
        if mode == "usb-serial":
            if not text(serial):
                raise EndpointError("USB serial mode requires a real serial")
        elif mode == "ram-bench":
            if (vid, pid, bcd, product, serial, cdc, midi) != (
                "1209", "0001", "0103", "Gridpico RP2040 Router", None, 0, 3
            ):
                raise EndpointError("unrecognized no-serial Pico Router identity")
        else:
            raise EndpointError("unknown selector mode")
        return Selector(mode, path, vid, pid, bcd, product, serial, cdc, midi)


def _attribute(path):
    return path.read_text(encoding="utf-8").removesuffix("\n")


def _optional_attribute(path):
    try:
        return _attribute(path)
    except FileNotFoundError:
        return None


def _identity(info):
    return info.st_dev, info.st_ino


@dataclass(frozen=True)
class UsbInterface:
    path: Path
    identity: tuple
    driver: str

    def guard(self):
        if (_identity(self.path.stat()) != self.identity
                or (self.path / "driver").resolve(strict=True).name != self.driver):
            raise EndpointError("selected USB interface changed")


class UsbTarget:
    def __init__(self, selector, sysfs=Path("/sys")):
        self.selector = selector
        self.sysfs = sysfs
        self.devices = sysfs / "bus/usb/devices"
        self.link = self.devices / selector.usb_path
        self.path = self.link.resolve(strict=True)
        self.fd = os.open(self.path, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
        try:
            self.identity = _identity(os.fstat(self.fd))
            self.address = self.attr("busnum"), self.attr("devnum")
            self.configuration = self.attr("bConfigurationValue")
            if self.address[0] != selector.usb_path.split("-", 1)[0] or not all(
                re.fullmatch(r"[1-9][0-9]*", attr)
                for attr in (*self.address, self.configuration)
            ):
                raise EndpointError("invalid active USB address or configuration")
            self.guard()
            self.unique_serial()
        except BaseException:
            os.close(self.fd)
            raise

    def __enter__(self):
        return self

    def __exit__(self, *_):
        os.close(self.fd)

    def attr(self, name, optional=False):
        try:
            with os.fdopen(os.open(name, os.O_RDONLY, dir_fd=self.fd), "rb") as attr:
                return attr.read(4096).decode("utf-8").removesuffix("\n")
        except FileNotFoundError:
            if optional:
                return None
            raise

    def descriptors(self):
        with os.fdopen(os.open("descriptors", os.O_RDONLY, dir_fd=self.fd), "rb") as source:
            return source.read(65537)

    def guard(self):
        selector = self.selector
        if (self.link.resolve(strict=True) != self.path
                or _identity(self.path.stat()) != self.identity
                or (self.attr("busnum"), self.attr("devnum")) != self.address
                or self.attr("bConfigurationValue") != self.configuration
                or (self.attr("idVendor"), self.attr("idProduct"), self.attr("bcdDevice"),
                    self.attr("product"), self.attr("serial", optional=True))
                != (selector.vid, selector.pid, selector.bcd_device, selector.product, selector.serial)):
            raise EndpointError("selected USB device changed")

    def unique_serial(self):
        if self.selector.serial is None:
            return
        matches = []
        for entry in self.devices.iterdir():
            if (_optional_attribute(entry / "idVendor"), _optional_attribute(entry / "idProduct")) != (
                self.selector.vid, self.selector.pid
            ):
                continue
            if _optional_attribute(entry / "serial") == self.selector.serial:
                matches.append(entry.name)
        if matches != [self.selector.usb_path]:
            raise EndpointError("USB serial is missing or ambiguous")

    def interface(self, number, interface_class, subclass, driver):
        link = self.devices / f"{self.selector.usb_path}:{self.configuration}.{number}"
        resolved = link.resolve(strict=True)
        if (resolved.parent != self.path
                or tuple(int(_attribute(resolved / name), 16) for name in
                         ("bInterfaceNumber", "bInterfaceClass", "bInterfaceSubClass"))
                != (number, interface_class, subclass)):
            raise EndpointError("unexpected USB interface")
        interface = UsbInterface(resolved, _identity(resolved.stat()), driver)
        interface.guard()
        return interface


@dataclass(frozen=True)
class DeviceNode:
    path: Path
    identity: tuple
    number: int

    @staticmethod
    def resolve(path, kernel_device):
        major, minor = (int(value) for value in _attribute(kernel_device / "dev").split(":"))
        device = path.stat(follow_symlinks=False)
        number = os.makedev(major, minor)
        if not stat.S_ISCHR(device.st_mode) or device.st_rdev != number:
            raise EndpointError("native device node does not match sysfs")
        return DeviceNode(path, _identity(device), number)

    def guard(self, fd):
        opened = os.fstat(fd)
        current = self.path.stat(follow_symlinks=False)
        if (_identity(opened) != self.identity or _identity(current) != self.identity
                or opened.st_rdev != self.number or current.st_rdev != self.number):
            raise EndpointError("native device node changed")


def ready():
    try:
        if os.write(3, b"ready\n") != 6:
            raise EndpointError("partial readiness message")
    finally:
        os.close(3)
