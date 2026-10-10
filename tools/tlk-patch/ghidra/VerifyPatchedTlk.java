// Copyright (C) 2026 Artem Bambalov
// SPDX-License-Identifier: GPL-2.0-only
// This program is free software; you can redistribute it and/or modify it
// under the terms of version 2 of the GNU General Public License.

import java.nio.file.Files;
import java.nio.file.Path;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;

/** Apply the generated image to an in-memory, readOnly copy of tlk_p02. */
public class VerifyPatchedTlk extends GhidraScript {
    private void instructions(long start, long end) throws Exception {
        clearListing(toAddr(start), toAddr(end - 1));
        for (long value = start; value < end; value += 4) {
            Address address = toAddr(value);
            disassemble(address);
            Instruction instruction = getInstructionAt(address);
            if (instruction == null || instruction.getLength() != 4)
                throw new IllegalStateException("Not an ARM instruction at " + address);
            println("DISASM " + address + " " + instruction);
        }
    }

    @Override
    public void run() throws Exception {
        byte[] image = Files.readAllBytes(Path.of(getScriptArgs()[0]));
        boolean stage2 = getScriptArgs().length > 1 && getScriptArgs()[1].equals("2");
        boolean stage3 = getScriptArgs().length > 1 && getScriptArgs()[1].equals("3");
        java.util.List<Long> sites = new java.util.ArrayList<>();
        if (image.length != 0x197200) throw new IllegalStateException("Wrong image size");
        for (int offset = 0x200; offset < 0xf214; offset += 4) {
            Address address = toAddr(0x48000000L + offset - 0x200);
            byte[] old = getBytes(address, 4);
            boolean different = false;
            for (int i = 0; i < 4; i++) if (old[i] != image[offset + i]) different = true;
            if (!different) continue;
            if (offset < 0xef10 + 0x200) sites.add(0x48000000L + offset - 0x200);
            byte[] replacement = java.util.Arrays.copyOfRange(image, offset, offset + 4);
            println("CHANGE " + address + " " +
                java.util.HexFormat.of().formatHex(old) + " -> " +
                java.util.HexFormat.of().formatHex(replacement));
            clearListing(address, address.add(3));
            setBytes(address, replacement);
        }
        instructions(0x48002a18L, 0x48002a8cL);
        if (stage3) {
            instructions(0x480029c8L, 0x48002a10L);
            clearListing(toAddr(0x48002a10L), toAddr(0x48002a17L));
            createDWord(toAddr(0x48002a10L));
            createDWord(toAddr(0x48002a14L));
        }
        for (long site : sites) {
            if (stage3 && site >= 0x480029c8L && site < 0x48002a18L) continue;
            if (site < 0x48002a18L || site >= 0x48002a8cL) instructions(site, site + 4);
        }
        if (stage3) {
            instructions(0x4800ef10L, 0x4800ef5cL);
            clearListing(toAddr(0x4800ef5cL), toAddr(0x4800ef73L));
            for (long row = 0x4800ef5cL; row < 0x4800ef74L; row += 4) {
                createDWord(toAddr(row));
                println("DATA " + toAddr(row) + " " + String.format("%08x", getInt(toAddr(row))));
            }
            instructions(0x4800ef74L, 0x4800ef98L);
            clearListing(toAddr(0x4800ef98L), toAddr(0x4800efabL));
            createDWord(toAddr(0x4800ef98L));
            createAsciiString(toAddr(0x4800ef9cL));
            instructions(0x4800efacL, 0x4800effcL);
        } else if (stage2) {
            instructions(0x4800ef10L, 0x4800ef68L);
            clearListing(toAddr(0x4800ef68L), toAddr(0x4800ef9fL));
            for (long row = 0x4800ef68L; row < 0x4800efa0L; row += 4) {
                createDWord(toAddr(row));
                println("DATA " + toAddr(row) + " " + String.format("%08x", getInt(toAddr(row))));
            }
            instructions(0x4800efa0L, 0x4800efd0L);
            clearListing(toAddr(0x4800efd0L), toAddr(0x4800efe3L));
            createDWord(toAddr(0x4800efd0L));
            createAsciiString(toAddr(0x4800efd4L));
            instructions(0x4800efe4L, 0x4800f000L);
        } else {
        instructions(0x4800ef10L, 0x4800ef50L);
        for (long stub = 0x4800ef50L; stub < 0x4800ef98L; stub += 12) {
            instructions(stub, stub + 8);
            clearListing(toAddr(stub + 8), toAddr(stub + 11));
            createDWord(toAddr(stub + 8));
            println("DATA " + toAddr(stub + 8) + " " +
                    String.format("%08x", getInt(toAddr(stub + 8))));
        }
        instructions(0x4800ef98L, 0x4800efccL);
        clearListing(toAddr(0x4800efccL), toAddr(0x4800efdfL));
        createDWord(toAddr(0x4800efccL));
        createAsciiString(toAddr(0x4800efd0L));
        instructions(0x4800efe0L, 0x4800effcL);
        }
        if (getInt(toAddr(0x4800f010L)) != 0)
            throw new IllegalStateException("printf still routes to memory");
        println("VERIFIED all patched instructions and printf flag; readOnly project unchanged");
    }
}
