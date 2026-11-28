"""Pack the exact reviewed offline pages into a deterministic FAT12 image."""
import argparse
import hashlib
import struct
from pathlib import Path

SECTOR = 512
SECTORS = 768
FAT_SECTORS = 3
ROOT_ENTRIES = 32
ROOT = 1 + 2 * FAT_SECTORS
DATA = ROOT + ROOT_ENTRIES * 32 // SECTOR
LABEL = b"GRIDPICO   "
VOLUME_ID = 0x4E742426
DOS_DATE = (20 << 9) | (1 << 5) | 1  # 2000-01-01
HTML_NAMES = ("HOME.HTM", "INDEX.HTM", "DIAG.HTM", "FM.HTM")
VERSION_SHA = "a6c5b1c68f1cd16735b2752cf3f17ac86d88a6ce4122961979f1b30005144ab5"


def load_assets(stage):
    version = (stage / "VERSION.TXT").read_bytes()
    if hashlib.sha256(version).hexdigest() != VERSION_SHA:
        raise ValueError("VERSION.TXT does not match published 4e74242 assets")
    records = version.decode("ascii").splitlines()[2:]
    if len(records) != len(HTML_NAMES):
        raise ValueError("VERSION.TXT does not list the four pinned pages")
    assets = {}
    for name, line in zip(HTML_NAMES, records):
        algorithm, filename, expected = line.split()
        if (algorithm, filename) != ("sha256", name):
            raise ValueError(f"Missing pinned SHA-256 record for {name}")
        content = (stage / name).read_bytes()
        if hashlib.sha256(content).hexdigest() != expected:
            raise ValueError(f"{name} differs from pinned VERSION.TXT")
        assets[name] = content
    assets["VERSION.TXT"] = version
    return assets


def set_fat(fat, cluster, value):
    offset = cluster + cluster // 2
    if cluster % 2 == 0:
        fat[offset] = value & 0xff
        fat[offset + 1] = (fat[offset + 1] & 0xf0) | (value >> 8)
    else:
        fat[offset] = (fat[offset] & 0x0f) | ((value & 0xf) << 4)
        fat[offset + 1] = value >> 4


def entry(image, index, name, attr, first=0, size=0):
    at = ROOT * SECTOR + index * 32
    image[at:at + 11] = name
    image[at + 11] = attr
    for date_offset in (16, 18, 24):
        struct.pack_into("<H", image, at + date_offset, DOS_DATE)
    struct.pack_into("<HI", image, at + 26, first, size)


def pack(assets):
    image = bytearray(SECTOR * SECTORS)
    image[0:11] = b"\xeb\x3c\x90GRIDPICO"
    struct.pack_into("<HBHBHHBHHHII", image, 11, SECTOR, 1, 1, 2, ROOT_ENTRIES,
                     SECTORS, 0xf8, FAT_SECTORS, 32, 64, 0, 0)
    struct.pack_into("<BBBI", image, 36, 0, 0, 0x29, VOLUME_ID)
    image[43:62] = LABEL + b"FAT12   "
    image[62:66] = b"\xfa\xf4\xeb\xfd"  # Nonbootable: halt.
    image[510:512] = b"\x55\xaa"

    fat = bytearray(FAT_SECTORS * SECTOR)
    set_fat(fat, 0, 0xff8)
    set_fat(fat, 1, 0xfff)
    entry(image, 0, LABEL, 0x08)
    cluster = 2
    for index, (name, payload) in enumerate(assets.items(), start=1):
        stem, suffix = name.split(".")
        first = cluster
        required = (len(payload) + SECTOR - 1) // SECTOR
        if required == 0 or (cluster - 2) + required > SECTORS - DATA:
            raise ValueError(f"{name} does not fit the FAT12 volume")
        for _ in range(required):
            set_fat(fat, cluster, cluster + 1 if cluster + 1 < first + required else 0xfff)
            cluster += 1
        at = (DATA + first - 2) * SECTOR
        image[at:at + len(payload)] = payload
        entry(image, index, f"{stem:<8}{suffix:<3}".encode("ascii"), 0x21, first, len(payload))

    image[SECTOR:ROOT * SECTOR] = fat + fat
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("assets", type=Path)
    parser.add_argument("image", type=Path)
    args = parser.parse_args()
    try:
        image = pack(load_assets(args.assets))
        args.image.write_bytes(image)
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
