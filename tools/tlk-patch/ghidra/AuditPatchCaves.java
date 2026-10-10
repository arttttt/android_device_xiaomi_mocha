// Copyright (C) 2026 Artem Bambalov
// SPDX-License-Identifier: GPL-2.0-only
// This program is free software; you can redistribute it and/or modify it
// under the terms of version 2 of the GNU General Public License.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

public class AuditPatchCaves extends GhidraScript {
    @Override
    public void run() throws Exception {
        long[][] ranges = {{0x48001340L, 0x480013b0L},
                           {0x480018ecL, 0x48001970L},
                           {0x4800ef10L, 0x4800f000L}};
        boolean failed = false;
        for (long[] range : ranges) {
            int refs = 0, functions = 0, nonzero = 0;
            for (long value = range[0]; value < range[1]; value++) {
                Address address = toAddr(value);
                if (getByte(address) != 0) nonzero++;
                Function function = getFunctionContaining(address);
                if (function != null) {
                    functions++;
                    println("FUNCTION " + address + " " + function.getName());
                }
                for (Reference ref : getReferencesTo(address)) {
                    refs++;
                    println("REFERENCE " + ref.getFromAddress() + " -> " + address);
                }
            }
            println(String.format("CAVE %08x..%08x nonzero=%d refs=%d functionBytes=%d",
                                  range[0], range[1], nonzero, refs, functions));
            if (range[0] == 0x4800ef10L && (nonzero != 0 || refs != 0 || functions != 0))
                failed = true;
        }
        if (failed) throw new IllegalStateException("The used ef10 cave is occupied");
        println("AUDITED ef10; the two referenced early regions must not be used");
    }
}
