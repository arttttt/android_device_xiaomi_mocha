import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

// Pre-script: disassemble in ARM mode from the reset vector and from every
// word in the kernel image that looks like a code pointer into it.
public class SeedArm extends GhidraScript {
	@Override
	public void run() throws Exception {
		long lo = 0x48000000L, hi = 0x48010000L;
		disassemble(toAddr(lo));
		int n = 0;
		for (long a = lo; a < hi; a += 4) {
			long v = getInt(toAddr(a)) & 0xffffffffL;
			if (v >= lo && v < hi && (v & 3) == 0) {
				if (getInstructionAt(toAddr(v)) == null && disassemble(toAddr(v))) n++;
			}
		}
		println("seeded " + n);
	}
}
