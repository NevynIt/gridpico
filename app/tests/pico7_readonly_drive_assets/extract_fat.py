"""Independently read the FAT12 image and compare the extracted root files."""
import pathlib
import sys

image, staged, output = map(pathlib.Path, sys.argv[1:])
disk = image.read_bytes()


def le(data, offset, count):
    return int.from_bytes(data[offset:offset + count], "little")


sector = le(disk, 11, 2)
per_cluster = disk[13]
reserved = le(disk, 14, 2)
fats = disk[16]
root_entries = le(disk, 17, 2)
total = le(disk, 19, 2)
fat_sectors = le(disk, 22, 2)
assert (sector, total, fats) == (512, 768, 2)
assert len(disk) == total * sector and disk[510:512] == b"\x55\xaa"
assert disk[43:54] == b"GRIDPICO   " and disk[54:62] == b"FAT12   "
fat_begin = reserved * sector
fat_bytes = fat_sectors * sector
table = disk[fat_begin:fat_begin + fat_bytes]
assert table == disk[fat_begin + fat_bytes:fat_begin + 2 * fat_bytes]
assert table[:3] == bytes((disk[21], 0xff, 0xff))
root_begin = fat_begin + fats * fat_bytes
root_end = root_begin + root_entries * 32
data_begin = (root_end + sector - 1) // sector * sector
cluster_bytes = per_cluster * sector
clusters = (len(disk) - data_begin) // cluster_bytes
assert 0 < clusters < 4085


def read_file(cluster, length, used):
    content = bytearray()
    while len(content) < length:
        assert 2 <= cluster < clusters + 2 and cluster not in used
        used.add(cluster)
        address = data_begin + (cluster - 2) * cluster_bytes
        content.extend(disk[address:address + cluster_bytes])
        entry = le(table, (cluster * 3) // 2, 2)
        cluster = entry >> 4 if cluster & 1 else entry & 0xfff
    assert cluster >= 0xff8 and not any(content[length:])
    return content[:length]


output.mkdir()
seen, used = set(), set()
label = False
for address in range(root_begin, root_end, 32):
    entry = disk[address:address + 32]
    if entry[0] == 0:
        assert not any(disk[address:root_end])
        break
    if entry[11] == 8:
        assert not label and entry[:11] == b"GRIDPICO   "
        label = True
        continue
    stem = entry[:8].decode("ascii").rstrip()
    extension = entry[8:11].decode("ascii").rstrip()
    name = f"{stem}.{extension}"
    assert name not in seen and entry[11] == 0x21
    assert name in {"HOME.HTM", "INDEX.HTM", "DIAG.HTM", "FM.HTM", "VERSION.TXT"}
    data = read_file(le(entry, 26, 2), le(entry, 28, 4), used)
    assert data == (staged / name).read_bytes()
    (output / name).write_bytes(data)
    seen.add(name)
    print(f"exact extracted {name} {len(data)} B")
assert len(seen) == 5 and label
print(f"FAT12 1 volume, {total} sectors, {len(used)} allocated clusters, two matching FATs")
