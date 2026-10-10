#!/usr/bin/env python3
# Copyright (C) 2026 Artem Bambalov
# SPDX-License-Identifier: GPL-2.0-only
# This program is free software; you can redistribute it and/or modify it
# under the terms of version 2 of the GNU General Public License.
"""Reproduce the TLK 0.2 UART/timeout image. Run with python3 -I."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

BASE = 0x48000000
HEADER_SIZE = 0x200
IMAGE_SIZE = 0x197200
SOURCE_SHA256 = "a05195438bc84f0194def15df30b9f7a357aecbb2bd2d91c19430b2033d2fcf2"
HERE = Path(__file__).resolve().parent
EXPECTED = {
    0x2AE8: "73402de9", 0x2B54: "0100a0e3", 0x2B58: "f41e00eb",
    0x2B68: "f9ffff1a", 0x2B6C: "38309fe5", 0x2B94: "7080bde8",
    0xA738: "0050a0e3", 0xA754: "050051e1", 0xA75C: "f9ffff3a",
    0x4E50: "4e1600eb", 0x4E54: "821000eb", 0x4E94: "510c00eb",
    0x84C0: "28e8ffeb", 0xF010: "01000000",
}


def branch(source, destination, *, link=False, condition=14):
    delta = destination - source - 8
    if source % 4 or destination % 4 or not -(1 << 25) <= delta < (1 << 25):
        raise ValueError("Unaligned or out-of-range ARM branch")
    if not 0 <= condition <= 14:
        raise ValueError("Invalid ARM condition")
    opcode = condition << 28 | 0x0A000000 | (int(link) << 24)
    return struct.pack("<I", opcode | ((delta >> 2) & 0xFFFFFF))


def assemble(source, *, round_number=1):
    """Read ELF32 sections/symbols and resolve local ARM branch relocations.

    Clang emits R_ARM_CALL/JUMP24 even for some local labels. Resolving them
    here avoids any host linker or an accidental unrelocated .text copy.
    """
    compiler = shutil.which("clang")
    if compiler is None:
        raise ValueError("clang with the ARM assembler is required")
    with tempfile.TemporaryDirectory(prefix="tlk393-asm-") as temporary:
        obj = Path(temporary) / "patch.o"
        defines = ["-DTLK_ROUND2"] if round_number >= 2 else []
        if round_number == 3:
            defines.append("-DTLK_ROUND3")
        subprocess.run([compiler, "--target=armv7-none-eabi", *defines, "-c",
                        str(source), "-o", str(obj)], check=True)
        elf = obj.read_bytes()
    if elf[:7] != b"\x7fELF\x01\x01\x01" or struct.unpack_from("<H", elf, 18)[0] != 40:
        raise ValueError("Expected a little-endian ELF32 ARM object")
    table_offset = struct.unpack_from("<I", elf, 32)[0]
    entry_size, count, names_index = struct.unpack_from("<HHH", elf, 46)
    if entry_size != 40:
        raise ValueError("Unexpected ELF section format")
    sections = [struct.unpack_from("<10I", elf, table_offset + 40 * i)
                for i in range(count)]

    def section_bytes(index):
        section = sections[index]
        return elf[section[4]:section[4] + section[5]]

    def string_at(strings, offset):
        return strings[offset:strings.index(b"\0", offset)].decode("ascii")

    names = section_bytes(names_index)
    text_index = next(i for i, section in enumerate(sections)
                      if string_at(names, section[0]) == ".text")
    text = bytearray(section_bytes(text_index))
    symbols = {}
    symbol_tables = {}
    for index, section in enumerate(sections):
        if section[1] != 2:  # SHT_SYMTAB
            continue
        strings = section_bytes(section[6])
        entries = [struct.unpack_from("<IIIBBH", section_bytes(index), offset)
                   for offset in range(0, section[5], 16)]
        symbol_tables[index] = entries
        for name, value, _size, _info, _other, defined_section in entries:
            if name and defined_section == text_index:
                symbols[string_at(strings, name)] = value
    for index, section in enumerate(sections):
        if section[1] != 9 or section[7] != text_index:  # SHT_REL
            continue
        entries = symbol_tables[section[6]]
        for offset in range(0, section[5], 8):
            location, info = struct.unpack_from("<II", section_bytes(index), offset)
            kind = info & 0xFF
            symbol = entries[info >> 8]
            if kind not in (28, 29) or symbol[5] != text_index:
                raise ValueError("Unsupported or external ARM relocation")
            instruction = struct.unpack_from("<I", text, location)[0]
            if instruction & 0x0E000000 != 0x0A000000:
                raise ValueError("Relocation is not on an ARM branch")
            # A local BL's implicit ELF REL addend is -8 (the PC bias).
            if instruction & 0xFFFFFF != 0xFFFFFE:
                raise ValueError("Unexpected ARM relocation addend")
            text[location:location + 4] = branch(
                location, symbol[1], link=bool(instruction & 0x01000000),
                condition=instruction >> 28)
    return bytes(text), symbols


def prepare(original, *, round_number=1):
    if round_number not in (1, 2, 3):
        raise ValueError("Unknown diagnostic round")
    if hashlib.sha256(original).hexdigest() != SOURCE_SHA256:
        raise ValueError("Original image SHA256 does not match TLK 0.2")
    if len(original) != IMAGE_SIZE or not original.startswith(b"NVTOSP\x001667072\x00"):
        raise ValueError("Unexpected image header/size")
    for address, expected in EXPECTED.items():
        offset = address + HEADER_SIZE
        if original[offset:offset + 4].hex() != expected:
            raise ValueError(f"Unexpected instruction at {BASE + address:#x}")
    if any(original[0xEF10 + HEADER_SIZE:0xF000 + HEADER_SIZE]):
        raise ValueError("The audited ef10 cave is not empty")
    assembled, symbols = assemble(HERE / "tlk_uart_timeout.S", round_number=round_number)
    expected_end = 0x2A84
    if symbols["bounded_putchar_end"] != expected_end or symbols["fuse_delay_shim_end"] != expected_end + 8:
        raise ValueError("putchar layout overflow")
    if symbols["marker_dispatch_end"] > 0xF000:
        raise ValueError("Audited cave overflow")
    if round_number == 3:
        if symbols["bounded_setup_keys_end"] != 0x2A18:
            raise ValueError("In-place platform_setup_keys overflow")
        if original[0xE928 + HEADER_SIZE:0xE92F + HEADER_SIZE] != b"%08x |\0":
            raise ValueError("Unexpected existing single-argument hex format")
        if struct.unpack_from("<I", original, 0xECAC + HEADER_SIZE)[0] != 0x70006300:
            raise ValueError("UARTD table entry changed")
    patched = bytearray(original)
    reasons = {}

    def edit(address, data, reason):
        if len(data) % 4 or address % 4:
            raise ValueError("Edits must consist of aligned ARM words")
        offset = address + HEADER_SIZE
        patched[offset:offset + len(data)] = data
        for relative in range(0, len(data), 4):
            reasons[address + relative] = reason

    def instruction(address, word, reason):
        edit(address, struct.pack("<I", word), reason)

    edit(0x2A18, assembled[0x2A18:0x2A8C], "Bounded putchar and 1us FUSE delay shim")
    if round_number == 3:
        edit(0x29C8, assembled[0x29C8:0x2A18],
             "TZRAM keys: retain header arithmetic, save arguments, marker P before CAR")
    edit(0xEF10, assembled[0xEF10:symbols["marker_dispatch_end"]],
         "Audited cave: markers and timeout guards")
    hooks = [(0x4E50, "mark_welcome", "W", True),
             (0x4E54, "mark_post_welcome", "A", True),
             (0x4E94, "mark_bootstrap", "F", True),
             (0x84C0, "mark_init_complete", "J", True),
             (0x2B54, "mark_fuse_begin", "1", False),
             (0x2B6C, "mark_fuse_ready", "2", False)] if round_number == 1 else []
    if round_number == 1:
        for address, symbol, marker, link in hooks:
            edit(address, branch(address, symbols[symbol], link=link), f"UART marker {marker}")
    else:
        table_start, table_end = symbols["marker_table"], symbols["marker_table_end"]
        for index, table_offset in enumerate(range(table_start, table_end, 4)):
            word = struct.unpack_from("<I", assembled, table_offset)[0]
            if round_number == 3:
                address = ((word >> 14) & 0x3FFF) * 4 - 4
                target = (word & 0x3FFF) * 4
                marker = chr(ord("A") + (word >> 28))
            else:
                address, target = (word >> 16) - 4, word & 0xFFFF
                marker = chr(ord("A") + index)
            old = struct.unpack_from("<I", original, address + HEADER_SIZE)[0]
            if address == 0xBBC8:
                if old != 0xE8BD40F0 or target != symbols["tail_replay"]:
                    raise ValueError("Unexpected bb14 tail-return frame")
            else:
                delta = old & 0xFFFFFF
                if delta & 0x800000:
                    delta -= 0x1000000
                if old >> 24 != 0xEB or address + 8 + delta * 4 != target:
                    raise ValueError(f"Lookup does not replay the original BL at {BASE + address:#x}")
            hooks.append((address, "marker_dispatch", marker, True))
            edit(address, branch(address, symbols["marker_dispatch"], link=True),
                 f"UART round-{round_number} marker {marker}")
        edit(0x2B54, branch(0x2B54, symbols["fuse_begin"]),
             "Initialize the unchanged FUSE budget without the old marker")
    # Two extra saved registers retain AAPCS stack alignment and preserve r7.
    instruction(0x2AE8, 0xE92D41F3, "FUSE: save counter registers with aligned frame")
    instruction(0x2B94, 0xE8BD81F0, "FUSE: restore enlarged frame after original add sp,#8")
    edit(0x2B58, branch(0x2B58, symbols["fuse_delay_shim"], link=True),
         "FUSE: always request exactly 1us")
    edit(0x2B68, branch(0x2B68, symbols["fuse_guard"], condition=1),
         "FUSE: guard busy-state back edge")
    instruction(0xA738, 0xE3A05801, "Delay: 65536 consecutive stalled samples")
    instruction(0xA754, 0xE3510000, "Delay: retain original high-word comparison against zero")
    edit(0xA75C, branch(0xA75C, symbols["delay_guard"], condition=3),
         "Delay: guard wait back edge; reset budget on progress")
    instruction(0xF010, 0, "Route printf to UART")
    if len(patched) != len(original) or patched[:HEADER_SIZE] != original[:HEADER_SIZE]:
        raise ValueError("Image size/header changed")
    changes = []
    for address, reason in sorted(reasons.items()):
        offset = address + HEADER_SIZE
        before, after = original[offset:offset + 4], patched[offset:offset + 4]
        if before != after:
            changes.append({"address": f"0x{BASE + address:08x}",
                            "file_offset": f"0x{offset:x}",
                            "before": before.hex(), "after": after.hex(), "reason": reason})
    manifest = {
        "input_sha256": SOURCE_SHA256,
        "output_sha256": hashlib.sha256(patched).hexdigest(),
        "image_size": len(patched), "cave_bytes": symbols["marker_dispatch_end"] - 0xEF10,
        "limits": {"fuse_iterations": 8192, "delay_stalled_samples": 65536,
                   "uart_tx_polls_per_character": 65536},
        "symbols": {name: f"0x{BASE + value:08x}" for name, value in symbols.items()
                    if not name.startswith("$")},
        "markers": [{"address": f"0x{BASE + address:08x}", "character": marker}
                    for address, _symbol, marker, _link in hooks],
        "changes": changes,
    }
    if round_number >= 2:
        manifest["round"] = round_number
    if round_number == 3:
        manifest["limits"]["tzram_copy_bytes"] = 0x10000
        manifest["copy_diagnostics"] = {
            "before_car": "P", "after_car": "Q", "after_copy": "R",
            "oversize_skip": "X", "format": "0x4800e928", "destination": "0x7c010000",
        }
    return bytes(patched), manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--round", type=int, choices=(1, 2, 3), default=1)
    args = parser.parse_args()
    if args.original.resolve() == args.output.resolve():
        parser.error("Output must differ from the original")
    manifest_path = args.output.with_suffix(".patch.json")
    if args.output.exists() or manifest_path.exists():
        parser.error("Output image or manifest already exists; choose a new path")
    try:
        patched, manifest = prepare(args.original.read_bytes(), round_number=args.round)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.error(str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("xb") as image:
        image.write(patched)
    with manifest_path.open("x", encoding="utf-8") as report:
        json.dump(manifest, report, indent=2)
        report.write("\n")
    print(f"Image: {args.output}\nSHA256: {manifest['output_sha256']}")
    print(f"Manifest: {manifest_path}; cave: {manifest['cave_bytes']}/240 bytes")


if __name__ == "__main__":
    main()
