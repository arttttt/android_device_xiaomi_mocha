#!/usr/bin/env python3
# Copyright (C) 2026 Artem Bambalov
# SPDX-License-Identifier: GPL-2.0-only
# This program is free software; you can redistribute it and/or modify it
# under the terms of version 2 of the GNU General Public License.
"""Execute stage-2 lookup hooks, tail replay and the unchanged wait guards."""

import importlib.util
import json
from pathlib import Path
import struct
import unittest

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("tlk_harness", HERE / "test_patch_tlk.py")
h = importlib.util.module_from_spec(spec)
spec.loader.exec_module(h)
IMAGE_PATH = HERE / "tos-psci-0.2-uart-stage2.img"
IMAGE = IMAGE_PATH.read_bytes()
REPORT = json.loads(IMAGE_PATH.with_suffix(".patch.json").read_text())
BaseMachine = h.Machine


class Machine(BaseMachine):
    def __init__(self, *, image=IMAGE, **arguments):
        super().__init__(image=image, **arguments)


h.Machine = Machine


class Stage2ExecutionTests(unittest.TestCase):
    test_putchar_gates = h.ArmExecutionTests.test_putchar_gates_nul_crlf_and_uart_selection
    test_putchar_dead_uart = h.ArmExecutionTests.test_putchar_dead_uart_is_bounded_and_does_not_write
    test_delay_healthy_and_wrap = h.ArmExecutionTests.test_delay_zero_short_wrap_and_long_with_repeated_microseconds
    test_delay_frozen = h.ArmExecutionTests.test_delay_frozen_timer_returns_after_bounded_stalled_reads
    test_fuse_assert_arguments = h.ArmExecutionTests.test_fuse_reset_and_clock_assert_arguments_match_original

    def test_all_lookup_rows_preserve_arguments_flags_stack_lr_even_without_uart(self):
        for ready in (True, False):
            for row in REPORT["markers"]:
                start = int(row["address"], 16)
                if start == 0x4800BBC8:
                    continue
                old = struct.unpack_from("<I", h.ORIGINAL, start - h.patch.BASE + 0x200)[0]
                displacement = old & 0xFFFFFF
                if displacement & 0x800000:
                    displacement -= 0x1000000
                target = start + 8 + 4 * displacement
                machine = Machine(uart_ready=ready)
                flags = machine.cpu.reg_read(h.UC_ARM_REG_CPSR)
                machine.word(h.STACK, 0xC0FFEE)
                machine.run(start, target)
                self.assertEqual(machine.writes, row["character"].encode() if ready else b"")
                for register, value in machine.initial.items():
                    self.assertEqual(machine.cpu.reg_read(register), value)
                self.assertEqual(machine.cpu.reg_read(h.UC_ARM_REG_CPSR), flags)
                self.assertEqual(machine.cpu.reg_read(h.UC_ARM_REG_LR), start + 4)
                self.assertEqual(machine.cpu.reg_read(h.UC_ARM_REG_SP), h.STACK)
                self.assertEqual(machine.cpu.mem_read(h.STACK, 4), struct.pack("<I", 0xC0FFEE))

    def test_lookup_restores_all_nzcv_and_q_flag_combinations(self):
        for nzcvq in range(32):
            machine = Machine()
            flags = 0x13 | (nzcvq << 27)
            machine.cpu.reg_write(h.UC_ARM_REG_CPSR, flags)
            machine.run(0x480084C0, 0x48002568)
            self.assertEqual(machine.cpu.reg_read(h.UC_ARM_REG_CPSR), flags)

    def test_b990_tail_replay_pops_original_lr_before_the_unchanged_branch(self):
        for ready in (True, False):
            machine = Machine(uart_ready=ready)
            flags = machine.cpu.reg_read(h.UC_ARM_REG_CPSR)
            restored = [0x44550000 + i for i in range(4)] + [h.STOP]
            machine.cpu.mem_write(h.STACK, struct.pack("<5I", *restored))
            machine.run(0x4800BBC8, 0x4800B990)
            self.assertEqual(machine.writes, b"K" if ready else b"")
            self.assertEqual(machine.cpu.reg_read(h.UC_ARM_REG_LR), h.STOP)
            self.assertEqual(machine.cpu.reg_read(h.UC_ARM_REG_SP), h.STACK + 20)
            self.assertEqual(machine.cpu.reg_read(h.UC_ARM_REG_CPSR), flags)
            for index, register in enumerate(h.REGISTERS[4:8]):
                self.assertEqual(machine.cpu.reg_read(register), restored[index])
            for register in h.REGISTERS[:4] + h.REGISTERS[8:]:
                self.assertEqual(machine.cpu.reg_read(register), machine.initial[register])

    def test_idle_i_occurs_once_before_first_yield_and_is_outside_the_jumpback_loop(self):
        for ready in (True, False):
            machine = Machine(uart_ready=ready)
            flags = machine.cpu.reg_read(h.UC_ARM_REG_CPSR)
            machine.word(h.STACK, 0xC0FFEE)
            # First yield after kmain renames itself idle and sets priority 0.
            machine.run(0x480084BC, 0x48008198)
            self.assertEqual(machine.writes, b"I" if ready else b"")
            self.assertEqual(machine.uart_reads, 1 if ready else 65536)
            for register, value in machine.initial.items():
                self.assertEqual(machine.cpu.reg_read(register), value)
            self.assertEqual(machine.cpu.reg_read(h.UC_ARM_REG_CPSR), flags)
            self.assertEqual(machine.cpu.reg_read(h.UC_ARM_REG_LR), 0x480084C0)
            self.assertEqual(machine.cpu.reg_read(h.UC_ARM_REG_SP), h.STACK)
            self.assertEqual(machine.cpu.mem_read(h.STACK, 4), struct.pack("<I", 0xC0FFEE))
            # The original loop after yield returns only to the N hook,
            # so I cannot repeat even if platform jumpback returns again.
            machine.run(0x480084C0, 0x48002568)
            for _ in range(5):
                machine.run(0x480084C4, 0x48002568)
            self.assertEqual(machine.writes, b"I" + b"N" * 6 if ready else b"")
            for address in range(0xCE4, 0xD08, 4):
                offset = address + h.patch.HEADER_SIZE
                self.assertEqual(IMAGE[offset:offset + 4], h.ORIGINAL[offset:offset + 4])

    def test_fuse_success_and_timeout_keep_the_stage1_budgets_and_cleanup(self):
        for busy in (0, 8191, 100000):
            for visible in (False, True):
                machine = Machine(fuse_busy_reads=busy, visible=visible)
                machine.cpu.reg_write(h.UC_ARM_REG_R1, h.OUTPUT)
                machine.run(0x48002AE4)
                timeout = busy >= 8192
                self.assertEqual(machine.fuse_reads, min(busy + 1, 8192))
                self.assertEqual(machine.security_reads, 0 if timeout else 1)
                self.assertEqual(machine.cpu.mem_read(h.OUTPUT, 4), struct.pack("<I", 0 if timeout else 1))
                self.assertEqual(machine.writes, b"FUSE timeout\r\n" if timeout else b"")
                self.assertEqual(machine.cpu.mem_read(0x60006048, 4),
                                 struct.pack("<I", 0x10000000 if visible else 0))
                machine.assert_frame(self)

    def test_original_inputs_reproduce_both_images_and_only_listed_words_change(self):
        first, first_report = h.patch.prepare(h.ORIGINAL)
        self.assertEqual(first, h.IMAGE)
        self.assertEqual(first_report, h.REPORT)
        second, second_report = h.patch.prepare(h.ORIGINAL, round_number=2)
        self.assertEqual(second, IMAGE)
        self.assertEqual(second_report, REPORT)
        self.assertEqual(second[:0x200], h.ORIGINAL[:0x200])
        self.assertEqual(second[0xF214:], h.ORIGINAL[0xF214:])
        self.assertEqual(second[0xF210:0xF214], bytes(4))
        self.assertEqual(len(second), len(h.ORIGINAL))
        self.assertEqual(REPORT["cave_bytes"], 240)
        listed = set()
        for row in REPORT["changes"]:
            offset = int(row["file_offset"], 16)
            self.assertEqual(h.ORIGINAL[offset:offset + 4].hex(), row["before"])
            self.assertEqual(second[offset:offset + 4].hex(), row["after"])
            listed.update(offset + i for i, (a, b) in enumerate(zip(
                bytes.fromhex(row["before"]), bytes.fromhex(row["after"]))) if a != b)
        changed = {i for i, (a, b) in enumerate(zip(h.ORIGINAL, second)) if a != b}
        self.assertEqual(changed, listed)


if __name__ == "__main__":
    unittest.main(verbosity=2)
