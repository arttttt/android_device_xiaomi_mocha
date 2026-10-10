// Copyright (C) 2026 Artem Bambalov
// SPDX-License-Identifier: GPL-2.0-only
// This program is free software; you can redistribute it and/or modify it
// under the terms of version 2 of the GNU General Public License.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;

/** Distinguish monitor power-off WFI from the boot thread's idle yield. */
public class AuditIdleEntry extends GhidraScript {
    @Override
    public void run() throws Exception {
        for (long value = 0x48000ce4L; value <= 0x48000d04L; value += 4) {
            disassemble(toAddr(value));
            Instruction instruction = getInstructionAt(toAddr(value));
            if (instruction == null) throw new IllegalStateException("Missing idle instruction");
            println("IDLE " + instruction.getAddress() + " " + instruction);
            for (Reference reference : getReferencesTo(toAddr(value))) {
                println("IDLE_REF " + reference.getFromAddress() + " -> " +
                        reference.getToAddress() + " " + reference.getReferenceType());
            }
        }
        for (long value = 0x4800847cL; value <= 0x480084c4L; value += 4) {
            disassemble(toAddr(value));
            Instruction instruction = getInstructionAt(toAddr(value));
            if (instruction == null) throw new IllegalStateException("Missing idle boot instruction");
            println("BOOT_IDLE " + instruction.getAddress() + " " + instruction);
            for (Reference reference : getReferencesTo(toAddr(value)))
                println("BOOT_IDLE_REF " + reference.getFromAddress() + " -> " +
                        reference.getToAddress() + " " + reference.getReferenceType());
        }
        if (getInt(toAddr(0x480084c4L)) != 0xeafffffd)
            throw new IllegalStateException("Idle back edge does not skip first yield");
        if (!getInstructionAt(toAddr(0x480084bcL)).toString().equals("bl 0x48008198"))
            throw new IllegalStateException("Unexpected boot idle yield call");
        println("AUDITED boot idle first yield outside jumpback loop; monitor WFI is separate");
    }
}
