#!/usr/bin/env python3
# Copyright (C) 2026 Artem Bambalov
# SPDX-License-Identifier: GPL-2.0-only
# This program is free software; you can redistribute it and/or modify it
# under the terms of version 2 of the GNU General Public License.
"""Run real ARM key-copy guards, formatted output and compact anchors."""

import importlib.util
import json
from pathlib import Path
import struct
import unittest

from unicorn import UC_HOOK_MEM_WRITE

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("tlk_stage2", HERE / "test_patch_tlk_stage2.py")
s = importlib.util.module_from_spec(spec)
spec.loader.exec_module(s)
h = s.h
IMAGE_PATH = HERE / "tos-psci-0.2-uart-stage3.img"
IMAGE = IMAGE_PATH.read_bytes()
REPORT = json.loads(IMAGE_PATH.with_suffix(".patch.json").read_text())
PARAMS = 0x4A000000
TZRAM = 0x7C010000
CAR = 0x60006360


class Machine(s.BaseMachine):
    def __init__(self, *, image=IMAGE, **arguments):
        super().__init__(image=image, **arguments)
        self.cpu.mem_map(PARAMS, 0x30000)
        self.cpu.mem_map(TZRAM - 0x1000, 0x12000)
        self.cpu.mem_write(TZRAM - 0x1000, b"\xcc" * 0x12000)
        self.word(CAR, 0xABC51234)
        self.word(0x480003A8, PARAMS)
        self.car_events = []
        self.copy_writes = 0
        self.cpu.hook_add(UC_HOOK_MEM_WRITE, self.monitor_copy)

    def monitor_copy(self, _cpu, _access, address, _size, value, _user):
        if address == CAR:
            self.car_events.append((value, bytes(self.writes)))
        if TZRAM <= address < TZRAM + 0x10000:
            self.copy_writes += 1

    def keys(self, length, *, first=0):
        self.word(PARAMS, first)
        offset = 0x68 if first == 0 else (4 + ((first + 3) & ~3)) & 0xFFFFFFFF
        header = PARAMS + offset
        self.word(header, length)
        data = bytes((i * 17 + 3) & 255 for i in range(min(length, 0x10000)))
        self.cpu.mem_write(header + 4, data)
        return data

    def assert_canaries(self, case):
        case.assertEqual(self.cpu.mem_read(TZRAM - 0x1000, 0x1000), b"\xcc" * 0x1000)
        case.assertEqual(self.cpu.mem_read(TZRAM + 0x10000, 0x1000), b"\xcc" * 0x1000)


h.Machine = Machine
s.Machine = Machine


class Stage3Tests(unittest.TestCase):
    test_putchar_gates = h.ArmExecutionTests.test_putchar_gates_nul_crlf_and_uart_selection
    test_putchar_dead_uart = h.ArmExecutionTests.test_putchar_dead_uart_is_bounded_and_does_not_write
    test_delay_healthy = h.ArmExecutionTests.test_delay_zero_short_wrap_and_long_with_repeated_microseconds
    test_delay_frozen = h.ArmExecutionTests.test_delay_frozen_timer_returns_after_bounded_stalled_reads
    test_fuse_assert_arguments = h.ArmExecutionTests.test_fuse_reset_and_clock_assert_arguments_match_original
    test_fuse_success_timeout = s.Stage2ExecutionTests.test_fuse_success_and_timeout_keep_the_stage1_budgets_and_cleanup
    test_all_flag_combinations = s.Stage2ExecutionTests.test_lookup_restores_all_nzcv_and_q_flag_combinations

    def test_anchor_rows_preserve_all_registers_flags_stack_and_lr(self):
        self.assertEqual([r["character"] for r in REPORT["markers"]], list("ABILMN"))
        for ready in (True, False):
            for row in REPORT["markers"]:
                start = int(row["address"], 16)
                old = struct.unpack_from("<I", h.ORIGINAL, start - h.patch.BASE + 0x200)[0]
                delta = old & 0xFFFFFF
                if delta & 0x800000:
                    delta -= 0x1000000
                destination = start + 8 + delta * 4
                m = Machine(uart_ready=ready)
                flags = m.cpu.reg_read(h.UC_ARM_REG_CPSR)
                m.run(start, destination)
                self.assertEqual(m.writes, row["character"].encode() if ready else b"")
                self.assertEqual(m.cpu.reg_read(h.UC_ARM_REG_CPSR), flags)
                self.assertEqual(m.cpu.reg_read(h.UC_ARM_REG_LR), start + 4)
                self.assertEqual(m.cpu.reg_read(h.UC_ARM_REG_SP), h.STACK)
                for register, value in m.initial.items():
                    self.assertEqual(m.cpu.reg_read(register), value)

    def test_exact_normal_copy_zero_and_limit_with_both_header_layouts(self):
        for first in (0, 1, 3, 4, 7, 0x65):
            for length in (0, 1, 3, 44, 0x600, 0x10000):
                with self.subTest(first=first, length=length):
                    m = Machine()
                    data = m.keys(length, first=first)
                    m.run(0x480029C8)
                    self.assertEqual(m.writes, f"PQ{length:08x} |R".encode())
                    self.assertEqual(m.car_events, [(0xEBC51234, b"P")])
                    self.assertEqual(m.cpu.mem_read(TZRAM, len(data)), data)
                    self.assertEqual(m.cpu.mem_read(TZRAM + length, 0x10000 - length),
                                     b"\xcc" * (0x10000 - length))
                    m.assert_canaries(self)
                    m.assert_frame(self)
                    self.assertEqual(m.cpu.reg_read(h.UC_ARM_REG_R0), TZRAM)

    def test_unsigned_oversize_skips_all_destination_writes_and_returns(self):
        for first in (0, 3):
            for length in (0x10001, 0x20000, 0x80000000, 0xDEADBEEF, 0xFFFFFFFF):
                m = Machine()
                m.keys(length, first=first)
                m.run(0x480029C8)
                self.assertEqual(m.writes, f"PQ{length:08x} |X".encode())
                self.assertEqual(m.copy_writes, 0)
                self.assertEqual(m.cpu.mem_read(TZRAM, 0x10000), b"\xcc" * 0x10000)
                m.assert_canaries(self)
                m.assert_frame(self)

    def test_dead_uart_keeps_bounded_copy_and_skip_paths(self):
        for length in (44, 0x10001):
            m = Machine(uart_ready=False)
            data = m.keys(length)
            m.run(0x480029C8)
            self.assertEqual(m.writes, b"")
            self.assertEqual(m.uart_reads, 13 * 65536)
            self.assertEqual(m.copy_writes > 0, length <= 0x10000)
            if length <= 0x10000:
                self.assertEqual(m.cpu.mem_read(TZRAM, len(data)), data)
            m.assert_frame(self)

    def test_l_returns_to_m_after_oversize_skip(self):
        m = Machine()
        m.keys(0xDEADBEEF)
        m.run(0x4800263C, 0x48001A98)
        self.assertEqual(m.writes, b"LPQdeadbeef |XM")
        self.assertEqual(m.copy_writes, 0)
        self.assertEqual(m.cpu.reg_read(h.UC_ARM_REG_SP), h.STACK)

    def test_phase_markers_and_length_precede_their_mmio_and_copy_boundaries(self):
        m = Machine()
        data = m.keys(44)
        m.run(0x480029C8, 0x48002A08)  # Immediately before the CAR store.
        self.assertEqual(m.writes, b"P")
        self.assertEqual(m.car_events, [])
        m.run(0x48002A08, 0x4800EFEC)  # Immediately before memcpy.
        self.assertEqual(m.writes, b"PQ0000002c |")
        self.assertEqual(m.copy_writes, 0)
        m.run(0x4800EFEC)
        self.assertEqual(m.writes, b"PQ0000002c |R")
        self.assertEqual(m.cpu.mem_read(TZRAM, 44), data)

    def test_all_rounds_reproduce_and_only_listed_words_change(self):
        for number, image, report in ((1, h.IMAGE, h.REPORT), (2, s.IMAGE, s.REPORT),
                                     (3, IMAGE, REPORT)):
            reproduced, manifest = h.patch.prepare(h.ORIGINAL, round_number=number)
            self.assertEqual(reproduced, image)
            self.assertEqual(manifest, report)
        self.assertEqual(IMAGE[:0x200], h.ORIGINAL[:0x200])
        self.assertEqual(IMAGE[0xF214:], h.ORIGINAL[0xF214:])
        self.assertEqual(len(IMAGE), len(h.ORIGINAL))
        self.assertEqual(REPORT["cave_bytes"], 236)
        for address in (0xC124, 0xC12C, 0xBB9C, 0xBBA0, 0xBBA8, 0xBBAC, 0xBBC0, 0xBBC8):
            offset = address + 0x200
            self.assertEqual(IMAGE[offset:offset + 4], h.ORIGINAL[offset:offset + 4])
        listed = set()
        for row in REPORT["changes"]:
            offset = int(row["file_offset"], 16)
            self.assertEqual(h.ORIGINAL[offset:offset + 4].hex(), row["before"])
            self.assertEqual(IMAGE[offset:offset + 4].hex(), row["after"])
            listed.update(offset + i for i, (a, b) in enumerate(zip(
                bytes.fromhex(row["before"]), bytes.fromhex(row["after"]))) if a != b)
        self.assertEqual({i for i, (a, b) in enumerate(zip(h.ORIGINAL, IMAGE)) if a != b}, listed)


if __name__ == "__main__":
    unittest.main(verbosity=2)
