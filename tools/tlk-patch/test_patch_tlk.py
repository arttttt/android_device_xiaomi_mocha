#!/usr/bin/env python3
# Copyright (C) 2026 Artem Bambalov
# SPDX-License-Identifier: GPL-2.0-only
# This program is free software; you can redistribute it and/or modify it
# under the terms of version 2 of the GNU General Public License.
"""Unit and ARM execution tests; run with an isolated Unicorn/Capstone Python."""

import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
    UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
    UC_ARM_REG_R12, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_CPSR)

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("patch_tlk", HERE / "patch_tlk.py")
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)
ORIGINAL = (HERE.parent / "psci02/tos-psci-0.2.img").read_bytes()
IMAGE_PATH = HERE / "tos-psci-0.2-uart-timeouts.img"
IMAGE = IMAGE_PATH.read_bytes()
REPORT = json.loads(IMAGE_PATH.with_suffix(".patch.json").read_text())
REGISTERS = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
             UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
             UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
             UC_ARM_REG_R12]
STOP = 0x48200000
STACK = 0x49008000
OUTPUT = 0x49009000
UART = 0x70006300


class Machine:
    def __init__(self, *, image=IMAGE, uart_ready=True, timer_step=1, timer_start=100,
                 timer_period=1, fuse_busy_reads=0, visible=False, uart_index=4):
        self.cpu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        for address, size in [(0x48000000, 0x210000), (0x49000000, 0x10000),
                              (0x60005000, 0x2000), (0x70006000, 0x1000),
                              (0x7000F000, 0x1000)]:
            self.cpu.mem_map(address, size)
        self.cpu.mem_write(0x48000000, image[0x200:])
        self.cpu.reg_write(UC_ARM_REG_CPSR, 0xA8000013)
        self.initial = {register: 0x13570000 + i * 0x101
                        for i, register in enumerate(REGISTERS)}
        for register, value in self.initial.items():
            self.cpu.reg_write(register, value)
        self.cpu.reg_write(UC_ARM_REG_SP, STACK)
        self.cpu.reg_write(UC_ARM_REG_LR, STOP)
        self.word(0x481A04C0, 0)
        self.word(0x481A04BC, uart_index)
        self.word(0x60006008, 0)
        self.word(0x60006014, 0x80)
        self.word(0x60006048, 0x10000000 if visible else 0)
        self.word(0x7000F9A0, 1)
        self.word(OUTPUT, 0xDEADBEEF)
        self.uart_ready = uart_ready
        self.timer_step = timer_step
        self.timer_start = timer_start
        self.timer_period = timer_period
        self.fuse_busy_reads = fuse_busy_reads
        self.timer_reads = 0
        self.fuse_reads = 0
        self.security_reads = 0
        self.uart_reads = 0
        self.writes = bytearray()
        self.cpu.hook_add(UC_HOOK_MEM_READ, self.read)
        self.cpu.hook_add(UC_HOOK_MEM_WRITE, self.write)

    def word(self, address, value):
        self.cpu.mem_write(address, struct.pack("<I", value))

    def read(self, cpu, _access, address, _size, _value, _user):
        if address in (UART + 0x14, 0x70006414):
            self.uart_reads += 1
            cpu.mem_write(address, bytes([0x40 if self.uart_ready else 0]))
        elif address == 0x60005010:
            value = (self.timer_start +
                     (self.timer_reads // self.timer_period) * self.timer_step) & 0xFFFFFFFF
            self.timer_reads += 1
            self.word(address, value)
        elif address == 0x7000F800:
            self.fuse_reads += 1
            self.word(address, 0 if self.fuse_reads <= self.fuse_busy_reads else 0x40000)
        elif address == 0x7000F9A0:
            self.security_reads += 1

    def write(self, _cpu, _access, address, size, value, _user):
        if address in (UART, 0x70006400) and size == 1:
            self.writes.append(value)

    def run(self, start, stop=STOP, count=10000000):
        self.cpu.emu_start(start, stop, count=count)
        # Exhausting the instruction budget must not count as returning.
        from unicorn.arm_const import UC_ARM_REG_PC
        if self.cpu.reg_read(UC_ARM_REG_PC) != stop:
            raise AssertionError("ARM code failed to return within the test instruction budget")

    def assert_frame(self, case):
        case.assertEqual(self.cpu.reg_read(UC_ARM_REG_SP), STACK)
        for register in REGISTERS[4:12]:
            case.assertEqual(self.cpu.reg_read(register), self.initial[register])


class PatcherTests(unittest.TestCase):
    def test_branch_encoding_boundaries_and_conditions(self):
        decoder = Cs(CS_ARCH_ARM, CS_MODE_ARM)
        for delta in (-33554432, -4, 0, 4, 33554428):
            for condition in (0, 1, 3, 14):
                for link in (False, True):
                    data = patch.branch(0x48010000, 0x48010008 + delta,
                                        condition=condition, link=link)
                    ins = next(decoder.disasm(data, 0x48010000))
                    self.assertIn(hex(0x48010008 + delta), ins.op_str)
        for source, target in [(1, 4), (0, 3), (0, 33554440), (33554440, 0)]:
            with self.assertRaises(ValueError):
                patch.branch(source, target)

    def test_reproduction_and_exact_manifest_coverage(self):
        image, manifest = patch.prepare(ORIGINAL)
        self.assertEqual(image, IMAGE)
        self.assertEqual(manifest, REPORT)
        self.assertEqual(len(image), 0x197200)
        self.assertEqual(image[:0x200], ORIGINAL[:0x200])
        self.assertEqual(image[0xF214:], ORIGINAL[0xF214:])
        self.assertEqual(image[0xF210:0xF214], bytes(4))
        self.assertEqual(image[0x1540:0x15B0], ORIGINAL[0x1540:0x15B0])
        self.assertEqual(image[0x1AEC:0x1B70], ORIGINAL[0x1AEC:0x1B70])
        changed = {i for i, (a, b) in enumerate(zip(ORIGINAL, image)) if a != b}
        listed = set()
        for item in manifest["changes"]:
            offset = int(item["file_offset"], 16)
            self.assertEqual(ORIGINAL[offset:offset + 4].hex(), item["before"])
            self.assertEqual(image[offset:offset + 4].hex(), item["after"])
            listed.update(offset + i for i, (a, b) in enumerate(zip(
                bytes.fromhex(item["before"]), bytes.fromhex(item["after"]))) if a != b)
        self.assertEqual(changed, listed)

    def test_reject_wrong_truncated_and_already_patched_images(self):
        for data in (b"", ORIGINAL[:-1], IMAGE, ORIGINAL[:100] + b"\xff" + ORIGINAL[101:]):
            with self.assertRaises(ValueError):
                patch.prepare(data)

    def test_cli_refuses_overwrite_and_bad_input_without_outputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = directory / "bad.img"
            output = directory / "output.img"
            source.write_bytes(b"wrong")
            cmd = [sys.executable, "-I", str(HERE / "patch_tlk.py"), str(source)]
            self.assertNotEqual(subprocess.run(cmd + [str(output)], capture_output=True).returncode, 0)
            self.assertFalse(output.exists())
            self.assertNotEqual(subprocess.run(cmd + [str(source)], capture_output=True).returncode, 0)
            output.write_bytes(b"keep")
            self.assertNotEqual(subprocess.run(cmd + [str(output)], capture_output=True).returncode, 0)
            self.assertEqual(output.read_bytes(), b"keep")


class ArmExecutionTests(unittest.TestCase):
    def test_putchar_gates_nul_crlf_and_uart_selection(self):
        for character, inhibit, index, expected in [(ord("x"), 0, 4, b"x"),
                (10, 0, 4, b"\r\n"), (0, 0, 4, b""), (ord("x"), 1, 4, b""),
                (ord("x"), 0, 0, b""), (ord("e"), 0, 5, b"e")]:
            machine = Machine(uart_index=index)
            machine.word(0x481A04C0, inhibit)
            machine.cpu.reg_write(UC_ARM_REG_R0, character)
            machine.run(0x48002A18)
            self.assertEqual(machine.writes, expected)
            machine.assert_frame(self)

    def test_putchar_dead_uart_is_bounded_and_does_not_write(self):
        for character, polls in [(ord("x"), 65536), (10, 131072)]:
            machine = Machine(uart_ready=False)
            machine.cpu.reg_write(UC_ARM_REG_R0, character)
            machine.run(0x48002A18)
            self.assertEqual(machine.uart_reads, polls)
            self.assertEqual(machine.writes, b"")
            machine.assert_frame(self)

    def test_bl_markers_preserve_arguments_stack_flags_and_correct_return_pc(self):
        cases = [(0x48004E50, 0x4800A790, b"W"), (0x48004E54, 0x48009064, b"A"),
                 (0x48004E94, 0x48007FE0, b"F"), (0x480084C0, 0x48002568, b"J")]
        for ready in (True, False):
            for start, target, expected in cases:
                machine = Machine(uart_ready=ready)
                flags = machine.cpu.reg_read(UC_ARM_REG_CPSR)
                machine.word(STACK, 0x1000)  # fifth thread_create argument
                machine.run(start, target)
                for register, value in machine.initial.items():
                    self.assertEqual(machine.cpu.reg_read(register), value)
                self.assertEqual(machine.cpu.reg_read(UC_ARM_REG_CPSR), flags)
                self.assertEqual(machine.cpu.reg_read(UC_ARM_REG_LR), start + 4)
                self.assertEqual(machine.cpu.reg_read(UC_ARM_REG_SP), STACK)
                self.assertEqual(machine.cpu.mem_read(STACK, 4), struct.pack("<I", 0x1000))
                self.assertEqual(machine.writes, expected if ready else b"")

    def test_fuse_non_bl_hooks_keep_lr_and_relocate_ldr(self):
        for start, stop in [(0x48002B54, 0x48002B58), (0x48002B6C, 0x48002B70)]:
            machine = Machine()
            flags = machine.cpu.reg_read(UC_ARM_REG_CPSR)
            machine.run(start, stop)
            self.assertEqual(machine.cpu.reg_read(UC_ARM_REG_LR), STOP)
            self.assertEqual(machine.cpu.reg_read(UC_ARM_REG_CPSR), flags)
            self.assertEqual(machine.cpu.reg_read(UC_ARM_REG_SP), STACK)
            if start == 0x48002B54:
                self.assertEqual(machine.cpu.reg_read(UC_ARM_REG_R7), 8192)
                self.assertEqual(machine.cpu.reg_read(UC_ARM_REG_R0), 1)
                self.assertEqual(machine.writes, b"1")
            else:
                self.assertEqual(machine.cpu.reg_read(UC_ARM_REG_R3), 0x7000F000)
                self.assertEqual(machine.writes, b"2")

    def test_delay_zero_short_wrap_and_long_with_repeated_microseconds(self):
        for requested, start, period in [(0, 100, 1), (1, 100, 1),
                (10, 0xFFFFFFFC, 1), (96, 100, 1024)]:
            machine = Machine(timer_start=start, timer_period=period)
            machine.cpu.reg_write(UC_ARM_REG_R0, requested)
            machine.run(0x4800A730)
            elapsed = (machine.timer_reads - 1) // period
            self.assertGreaterEqual(elapsed, requested)
            self.assertLessEqual(elapsed, requested + 1)
            if period > 1:
                self.assertGreater(machine.timer_reads, 65536)
            machine.assert_frame(self)

    def test_delay_frozen_timer_returns_after_bounded_stalled_reads(self):
        machine = Machine(timer_step=0)
        machine.cpu.reg_write(UC_ARM_REG_R0, 1)
        machine.run(0x4800A730)
        self.assertGreaterEqual(machine.timer_reads, 65536)
        self.assertLessEqual(machine.timer_reads, 65539)
        machine.assert_frame(self)

    def test_fuse_idle_busy_then_ready_and_last_allowed_poll_restore_visibility(self):
        for busy in (0, 5, 8191):
            for visible in (False, True):
                machine = Machine(fuse_busy_reads=busy, visible=visible)
                machine.cpu.reg_write(UC_ARM_REG_R1, OUTPUT)
                machine.run(0x48002AE4)
                self.assertEqual(machine.fuse_reads, busy + 1)
                self.assertEqual(machine.security_reads, 1)
                self.assertEqual(machine.cpu.mem_read(OUTPUT, 4), struct.pack("<I", 1))
                self.assertEqual(machine.writes, b"12")
                self.assertEqual(machine.cpu.mem_read(0x60006048, 4),
                                 struct.pack("<I", 0x10000000 if visible else 0))
                machine.assert_frame(self)

    def test_fuse_timeout_returns_zero_without_security_read_and_restores_frame(self):
        for visible in (False, True):
            machine = Machine(fuse_busy_reads=100000, visible=visible)
            machine.cpu.reg_write(UC_ARM_REG_R1, OUTPUT)
            machine.run(0x48002AE4)
            self.assertEqual(machine.fuse_reads, 8192)
            self.assertEqual(machine.security_reads, 0)
            self.assertEqual(machine.cpu.mem_read(OUTPUT, 4), bytes(4))
            self.assertEqual(machine.writes, b"1FUSE timeout\r\n")
            self.assertEqual(machine.cpu.mem_read(0x60006048, 4),
                             struct.pack("<I", 0x10000000 if visible else 0))
            machine.assert_frame(self)

    def test_fuse_reset_and_clock_assert_arguments_match_original(self):
        for reset, clock in [(0x80, 0x80), (0, 0)]:
            results = []
            for image in (ORIGINAL, IMAGE):
                machine = Machine(image=image)
                machine.word(0x60006008, reset)
                machine.word(0x60006014, clock)
                machine.cpu.reg_write(UC_ARM_REG_R1, OUTPUT)
                machine.run(0x48002AE4, 0x4800A82C)
                args = [machine.cpu.reg_read(reg) for reg in REGISTERS[:4]]
                expression = machine.cpu.mem_read(machine.cpu.reg_read(UC_ARM_REG_SP), 4)
                results.append((args, expression))
            self.assertEqual(results[0], results[1])


if __name__ == "__main__":
    unittest.main(verbosity=2)
