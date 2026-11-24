"""Verify that a Pico SDK RP2040 ELF and its UF2 write only to SRAM."""

import argparse
from pathlib import Path
import struct
import sys


RAM = 0x20000000
MAIN_RAM_END = 0x20040000
PHYSICAL_RAM_END = 0x20042000
ELF_HEADER = struct.Struct("<16sHHIIIIIHHHHHH")
PROGRAM_HEADER = struct.Struct("<8I")
UF2_HEADER = struct.Struct("<8I")
UF2_BLOCK = 512
UF2_PAYLOAD = 256


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def in_ram(start, size, end):
    return RAM <= start < end and 0 < size <= end - start


def elf_loads(elf):
    (identity, file_type, machine, _, entry, table, _, _, _, header_size, count,
     _, _, _) = ELF_HEADER.unpack_from(elf)
    require(identity[:7] == b"\x7fELF\x01\x01\x01" and file_type == 2 and machine == 40,
            "Expected a little-endian executable ARM ELF32")
    require(header_size == PROGRAM_HEADER.size and table + header_size * count <= len(elf),
            "Invalid ELF program headers")
    entry_address = entry & ~1
    require(entry & 1 and in_ram(entry_address, 2, MAIN_RAM_END), "Entry is outside main SRAM")
    loads = []
    executable_entry = False
    for index in range(count):
        kind, offset, vma, lma, file_size, mem_size, flags, _ = PROGRAM_HEADER.unpack_from(
            elf, table + index * header_size)
        if kind != 1:
            continue
        require(file_size <= mem_size, "Invalid ELF load size")
        if mem_size == 0:
            continue
        require(in_ram(vma, mem_size, PHYSICAL_RAM_END) and
                in_ram(lma, mem_size, PHYSICAL_RAM_END), "ELF load span outside physical SRAM")
        print(f"PT_LOAD VMA={vma:#010x} LMA={lma:#010x} files={file_size:#x} memory={mem_size:#x}")
        if not file_size:
            continue
        require(in_ram(vma, file_size, MAIN_RAM_END) and in_ram(lma, file_size, MAIN_RAM_END)
                and offset + file_size <= len(elf), "ELF file-backed load outside main SRAM")
        executable_entry |= bool(flags & 1 and vma <= entry_address < vma + file_size)
        loads.append((lma, elf[offset:offset + file_size]))
    require(executable_entry, "Entry is not in an executable file-backed load")
    print(f"ARM Thumb entry {entry:#010x}")
    return loads


def verify_uf2(uf2, loads):
    require(uf2 and len(uf2) % UF2_BLOCK == 0, "Invalid UF2 size")
    block_count = len(uf2) // UF2_BLOCK
    seen_addresses = set()
    image = bytearray(MAIN_RAM_END - RAM)
    for offset in range(0, len(uf2), UF2_BLOCK):
        start, magic, flags, address, size, number, total, family = UF2_HEADER.unpack_from(uf2, offset)
        tail, = struct.unpack_from("<I", uf2, offset + UF2_BLOCK - 4)
        require((start, magic, tail) == (0x0A324655, 0x9E5D5157, 0x0AB16F30),
                "Invalid UF2 block magic")
        require((flags, family, size) == (0x2000, 0xE48BFF56, UF2_PAYLOAD)
                and number == offset // UF2_BLOCK and total == block_count, "Invalid RP2040 UF2 block")
        require(in_ram(address, size, MAIN_RAM_END) and address % size == 0
                and address not in seen_addresses, "UF2 write is outside main SRAM or overlaps")
        seen_addresses.add(address)
        pos = address - RAM
        image[pos:pos + size] = uf2[offset + UF2_HEADER.size:offset + UF2_HEADER.size + size]
    expected = bytearray(len(image))
    expected_addresses = set()
    for address, contents in loads:
        start = address - RAM
        end = start + len(contents)
        expected[start:end] = contents
        first_block = address - address % UF2_PAYLOAD
        expected_addresses.update(range(first_block, address + len(contents), UF2_PAYLOAD))
    require(seen_addresses == expected_addresses and image == expected, "UF2 differs from ELF loads")
    print(f"UF2 {block_count} blocks, write span {min(seen_addresses):#010x}"
          f"..{max(seen_addresses) + UF2_PAYLOAD:#010x} (exclusive), matches ELF loads")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("uf2", type=Path)
    args = parser.parse_args()
    try:
        loads = elf_loads(args.elf.read_bytes())
        verify_uf2(args.uf2.read_bytes(), loads)
    except (ValueError, OSError, struct.error) as error:
        print(f"RAM image check failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
