#!/usr/bin/env python3
"""Widen the stack slots that NVIDIA blobs reserve for android::Parcel.

Several of the blobs build a Parcel in a stack slot whose size was fixed when
the blob was compiled.  Parcel has grown since -- 48 bytes in KitKat, 52 in
Lollipop, 60 in Q -- while libbinder's constructor still writes the whole of
the current class.  The surplus lands on whatever the blob put above the slot,
which is its saved registers and its stack guard, so the process dies on
return.  That is what killed SurfaceFlinger in eglInitialize: libnvcpl asks
libEGL_tegra's app-profile service a question, and answering it corrupts the
frame that asked.

The fix is to give each slot the room the current platform needs.  The size is
NOT written down here -- it is read from parcel_size_probe, an object built by
this release's own compiler against this release's own headers (see
parcel_size_probe.cpp).  Port to a newer Android and the number follows by
itself.  If the probe cannot be read this tool fails and takes the build with
it, because a blob left unfitted boots to a black screen and a ROM that ships
one is worse than a ROM that never built.

The rewrite is smaller than it looks.  What crowds a Parcel at offset O is not
the frame as a whole but the next slot the function uses above it, T.  So for
each cramped Parcel: add D bytes at T -- every sp-relative offset >= T moves up
by D, and the prologue's `sub sp` and the epilogue's `add sp` grow by the same
D.  Nothing else moves, and an object cannot straddle T (T is itself a slot
base, and a slot spanning it would have to overlap the Parcel), so no object is
ever torn in half.  D is rounded to a multiple of 8, which keeps sp aligned as
AAPCS requires.

Anything this tool does not fully understand is a hard error, never a skip: a
silently unpatched blob is the bug it exists to prevent.
"""

import argparse
import glob
import os
import re
import shutil
import struct
import subprocess
import sys

# Installed blobs that construct a Parcel but are never loaded, with the
# reason.  Listed rather than patched because patching them would mean
# teaching this tool the A32 encodings for code that no process maps.
#
# All four are reachable only through audio.primary.vendor.tegra.so, the stock
# NVIDIA audio HAL.  This board runs tinyhal instead: hidl/audio builds
# audio.primary.$(TARGET_BOARD_PLATFORM) = audio.primary.tegra.so, and that is
# the name hw_get_module resolves, so the stock HAL and everything below it is
# dead weight in the image.  Delete the blobs and these entries go with them.
UNREACHABLE = {
    "libbt-client-api.so": "stock audio HAL only; board uses tinyhal",
    "libaudioavp.so": "stock audio HAL only; board uses tinyhal",
    "libnvcapaudioservice.so": "stock audio HAL only; board uses tinyhal",
    "libnvaudioservice.so": "stock audio HAL only; board uses tinyhal",
}

PARCEL_CTORS = ("_ZN7android6ParcelC1Ev", "_ZN7android6ParcelC2Ev")
PROBE_SYMBOL = "__parcel_size_probe"

SHT_NOBITS = 8
STT_FUNC = 2


class Fatal(Exception):
    pass


# --------------------------------------------------------------------------
# a very small ELF32 reader -- enough to map addresses to file offsets and to
# read the dynamic symbol table, so the tool needs no readelf on the host
# --------------------------------------------------------------------------
class Elf32:
    def __init__(self, path):
        with open(path, "rb") as fh:
            self.data = bytearray(fh.read())
        self.path = path
        if self.data[:4] != b"\x7fELF":
            raise Fatal(f"{path}: not an ELF")
        self.elfclass = self.data[4]
        self.e_machine = struct.unpack_from("<H", self.data, 18)[0]
        if self.elfclass != 1:
            raise Fatal(f"{path}: not ELF32")
        e_shoff = struct.unpack_from("<I", self.data, 32)[0]
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", self.data, 46)
        self.sections = []
        for i in range(e_shnum):
            off = e_shoff + i * e_shentsize
            (name, stype, _flags, addr, offset, size, link, _info,
             _align, entsize) = struct.unpack_from("<10I", self.data, off)
            self.sections.append(dict(name=name, type=stype, addr=addr,
                                      offset=offset, size=size, link=link,
                                      entsize=entsize))
        strtab = self.sections[e_shstrndx]
        for s in self.sections:
            s["sname"] = self._cstr(strtab["offset"] + s["name"])

    def _cstr(self, off):
        end = self.data.index(b"\x00", off)
        return self.data[off:end].decode("utf-8", "replace")

    def section(self, name):
        for s in self.sections:
            if s["sname"] == name:
                return s
        return None

    def addr_to_offset(self, addr):
        for s in self.sections:
            if s["type"] == SHT_NOBITS or not s["addr"]:
                continue
            if s["addr"] <= addr < s["addr"] + s["size"]:
                return addr - s["addr"] + s["offset"]
        raise Fatal(f"{self.path}: address 0x{addr:x} is in no section")

    def exidx_starts(self):
        """Every function start, from the ARM exception index.

        The dynamic symbol table only names exported functions, and the blobs
        call the Parcel constructor from local helpers as well -- libnvcpl's
        transaction wrapper is not exported.  .ARM.exidx has one entry per
        function whatever its linkage, which is exactly the boundary list this
        needs; each entry begins with a prel31 pointer to its function.
        """
        sec = self.section(".ARM.exidx")
        if sec is None:
            raise Fatal(f"{self.path}: no .ARM.exidx; function boundaries "
                        f"cannot be established")
        starts = []
        for off in range(sec["offset"], sec["offset"] + sec["size"], 8):
            word = struct.unpack_from("<I", self.data, off)[0]
            rel = word & 0x7FFFFFFF
            if rel & 0x40000000:
                rel -= 0x80000000
            entry_addr = sec["addr"] + (off - sec["offset"])
            starts.append((entry_addr + rel) & 0xFFFFFFFF)
        return sorted(set(starts))

    def plt_stubs(self):
        """Map each PLT stub's address to the symbol it calls.

        objdump is not asked this.  The LLVM in the Q tree is 9.0 and does not
        synthesise the `<sym@plt>` labels that newer ones do, and the whole
        point of this tool is to survive a change of platform -- so the answer
        is read out of the file.  .rel.plt says which GOT slot belongs to which
        symbol, and each stub names its own GOT slot:

            add r12, pc,  #imm, #rot
            add r12, r12, #imm, #rot
            ldr pc, [r12, #imm12]!
        """
        rel = self.section(".rel.plt")
        plt = self.section(".plt")
        if rel is None or plt is None:
            raise Fatal(f"{self.path}: no .rel.plt/.plt to resolve calls with")
        syms = self.dyn_symbols()
        got = {}
        for off in range(rel["offset"], rel["offset"] + rel["size"], 8):
            r_offset, r_info = struct.unpack_from("<II", self.data, off)
            if r_info & 0xFF != 22:            # R_ARM_JUMP_SLOT
                continue
            idx = r_info >> 8
            if idx < len(syms):
                got[r_offset] = syms[idx]["name"]

        def modimm(word):
            imm8, rot = word & 0xFF, ((word >> 8) & 0xF) * 2
            return ((imm8 >> rot) | (imm8 << (32 - rot))) & 0xFFFFFFFF if rot \
                else imm8

        stubs = {}
        base, size = plt["addr"], plt["size"]
        for pos in range(0, size - 11, 4):
            w0, w1, w2 = struct.unpack_from("<3I", self.data,
                                            plt["offset"] + pos)
            if (w0 & 0xFFFFF000 != 0xE28FC000
                    or w1 & 0xFFFFF000 != 0xE28CC000
                    or w2 & 0xFFFFF000 != 0xE5BCF000):
                continue
            addr = base + pos
            slot = (addr + 8 + modimm(w0) + modimm(w1)
                    + (w2 & 0xFFF)) & 0xFFFFFFFF
            if slot in got:
                stubs[addr] = got[slot]
        return stubs

    def dyn_symbols(self):
        sym = self.section(".dynsym")
        if sym is None:
            return []
        strt = self.sections[sym["link"]]
        out = []
        for off in range(sym["offset"], sym["offset"] + sym["size"], 16):
            st_name, st_value, st_size, st_info, _o, st_shndx = \
                struct.unpack_from("<IIIBBH", self.data, off)
            out.append(dict(name=self._cstr(strt["offset"] + st_name),
                            value=st_value, size=st_size,
                            type=st_info & 0xF, shndx=st_shndx))
        return out

    def save(self):
        with open(self.path, "wb") as fh:
            fh.write(self.data)


# --------------------------------------------------------------------------
# instruction model
# --------------------------------------------------------------------------
class Insn:
    """One decoded instruction.

    `halfwords` holds what objdump printed, which is the sequence of 16-bit
    values in decoding order -- NOT the bytes in file order.  `sub sp, #0x48`
    prints as "b092" and is the halfword 0xB092, stored little-endian as 92 b0.
    Keeping the halfwords and packing them back out on write is what keeps the
    two apart.
    """

    __slots__ = ("addr", "size", "mnem", "args", "halfwords", "target")

    def __init__(self, addr, mnem, args, halfwords):
        self.addr = addr
        self.mnem, self.args = mnem, args
        self.halfwords = halfwords
        self.size = 2 * len(halfwords)
        self.target = None

    def encode(self, halfwords):
        return b"".join(struct.pack("<H", h) for h in halfwords)

    def __repr__(self):
        return f"0x{self.addr:x} {self.mnem} {self.args}"


LINE = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2,4}\s)+)\s*$")
REG = re.compile(r"^(r\d+|sl|fp|ip|sp|lr|pc)$")
IMM = re.compile(r"#(0x[0-9a-f]+|\d+)")
SP_MEM = re.compile(r"\[sp(?:,\s*#(0x[0-9a-f]+|\d+))?\]")
REG_MEM = re.compile(r"\[(r\d+),\s*#(0x[0-9a-f]+|\d+)\]")
SP_ADD = re.compile(r"^(r\d+|sp),\s*sp(?:,\s*#(0x[0-9a-f]+|\d+))?\s*$")

NO_DEST = ("push", "cmp", "cmn", "tst", "teq", "str", "strb", "strh", "strd",
           "stm", "b", "bl", "blx", "bx", "cbz", "cbnz", "nop", "it", "dmb",
           "dsb", "isb", "svc", "pld")


def imm_of(text):
    m = IMM.search(text or "")
    return int(m.group(1), 0) if m else None


def disassemble(objdump, path, thumb):
    triple = "thumbv7-linux-androideabi" if thumb else "armv7-linux-androideabi"
    proc = subprocess.run([objdump, "-d", f"--triple={triple}", path],
                          capture_output=True, text=True)
    if proc.returncode != 0:
        raise Fatal(f"{path}: objdump failed: {proc.stderr.strip()}")
    insns = []
    literals = []          # (addr, length) of constant pools, see below
    for raw in proc.stdout.splitlines():
        if "\t" not in raw:
            continue
        head, *rest = raw.split("\t")
        m = re.match(r"^\s*([0-9a-f]+):\s+([0-9a-f ]+?)\s*$", head)
        if not m or not rest:
            continue
        addr = int(m.group(1), 16)
        halfwords = normalise(m.group(2).split())
        if halfwords is None:
            continue
        mnem = rest[0].strip()
        args = (rest[1].strip() if len(rest) > 1 else "").partition("@")[0]
        ins = Insn(addr, mnem, args.strip(), halfwords)

        # Branch targets and constant-pool addresses are computed from the
        # encoding, never read from objdump's annotations: LLVM 9 (what the Q
        # tree ships) prints neither `<sym@plt>` labels nor resolved literal
        # addresses, and prints branch displacements rather than destinations.
        # Decoding them here is both version-proof and exact.
        ins.target = branch_target(ins)
        lit = literal_target(ins)
        if lit is not None:
            literals.append((lit, 8 if mnem.startswith("ldrd") else 4))
        insns.append(ins)

    # Constant pools sit inside the function bodies and disassemble as
    # plausible instructions -- which is where a [sp, #1008] appeared in a
    # frame of 124 bytes.  The loads that read them say which bytes are data.
    def is_data(ins):
        return any(a < ins.addr + ins.size and ins.addr < a + n
                   for a, n in literals)

    return [i for i in insns if not is_data(i)]


def normalise(groups):
    """Turn objdump's byte column into halfwords, whichever way it prints.

    LLVM 9 prints bytes in file order ("ff f7 2e ee"); newer ones print the
    halfwords themselves ("f7ff ee2e").  Both mean the same instruction, and
    everything downstream wants halfwords.
    """
    if not groups:
        return None
    if all(len(g) == 4 for g in groups):
        return [int(g, 16) for g in groups]
    if all(len(g) == 2 for g in groups) and len(groups) % 2 == 0:
        b = [int(g, 16) for g in groups]
        return [b[i] | (b[i + 1] << 8) for i in range(0, len(b), 2)]
    return None


def branch_target(ins):
    """Absolute destination of a Thumb BL/BLX (immediate), or None."""
    if len(ins.halfwords) != 2:
        return None
    hw1, hw2 = ins.halfwords
    if hw1 & 0xF800 != 0xF000 or hw2 & 0xC000 != 0xC000:
        return None
    s = (hw1 >> 10) & 1
    j1, j2 = (hw2 >> 13) & 1, (hw2 >> 11) & 1
    i1, i2 = 1 - (j1 ^ s), 1 - (j2 ^ s)
    to_arm = not (hw2 >> 12) & 1                       # BLX exchanges
    if to_arm:
        imm = ((hw1 & 0x3FF) << 12) | ((hw2 & 0x7FE) << 1)
        pc = (ins.addr + 4) & ~3
    else:
        imm = ((hw1 & 0x3FF) << 12) | ((hw2 & 0x7FF) << 1)
        pc = ins.addr + 4
    off = (s << 24) | (i1 << 23) | (i2 << 22) | imm
    if off & 0x1000000:
        off -= 0x2000000
    return (pc + off) & 0xFFFFFFFF


def literal_target(ins):
    """Address of the constant a pc-relative load reads, or None."""
    pc = (ins.addr + 4) & ~3
    if len(ins.halfwords) == 1:
        hw = ins.halfwords[0]
        if hw & 0xF800 == 0x4800:                      # LDR Rt, [pc, #imm8*4]
            return pc + ((hw & 0xFF) << 2)
        return None
    hw1, hw2 = ins.halfwords
    if hw1 & 0xFF7F == 0xF85F:                         # LDR.W Rt, [pc, #imm12]
        imm = hw2 & 0xFFF
        return pc + imm if (hw1 >> 7) & 1 else pc - imm
    if hw1 & 0xFE7F == 0xE85F:                         # LDRD Rt, Rt2, [pc,#±]
        imm = (hw2 & 0xFF) << 2
        return pc + imm if (hw1 >> 7) & 1 else pc - imm
    return None


# --------------------------------------------------------------------------
# per-function analysis
# --------------------------------------------------------------------------
class Site:
    """One Parcel built on the stack."""

    def __init__(self, call_addr, offset):
        self.call_addr = call_addr
        self.offset = offset


class Function:
    def __init__(self, name, start, end, insns):
        self.name, self.start, self.end = name, start, end
        self.insns = insns
        self.frame = None
        self.frame_insns = []      # prologue/epilogue sp adjustments
        self.base_insns = []       # (insn, offset) for `add rX, sp, #off`
        self.mem_insns = []        # (insn, offset) for `[sp, #off]`
        self.offsets = set()
        self.sites = []

    def analyse(self, ctor_addrs):
        regmap = {}
        for ins in self.insns:
            mn, args = ins.mnem, ins.args

            # sp adjustments
            if mn.startswith(("sub", "add")) and re.match(r"^sp\s*,", args):
                val = imm_of(args)
                if val is None:
                    raise Fatal(f"{self.name}: sp adjusted by a register "
                                f"at 0x{ins.addr:x} ({mn} {args})")
                if mn.startswith("sub"):
                    if self.frame is None:
                        self.frame = val
                    elif self.frame != val:
                        raise Fatal(f"{self.name}: two different frame sizes")
                elif self.frame is not None and val != self.frame:
                    raise Fatal(f"{self.name}: `add sp, #{val}` does not match "
                                f"frame {self.frame} at 0x{ins.addr:x}")
                self.frame_insns.append(ins)
                continue

            if mn in ("mov", "mov.w") and re.match(r"^sp\s*,", args):
                raise Fatal(f"{self.name}: sp restored from a register "
                            f"at 0x{ins.addr:x}")

            # rX <- sp + imm
            m = SP_ADD.match(args)
            if mn.startswith("add") and m and m.group(1) != "sp":
                off = int(m.group(2), 0) if m.group(2) else 0
                regmap[m.group(1)] = off
                self.offsets.add(off)
                self.base_insns.append((ins, off))
                continue

            if mn.startswith("mov"):
                parts = [p.strip() for p in args.split(",")]
                if len(parts) == 2 and REG.match(parts[0]):
                    if parts[1] == "sp":
                        regmap[parts[0]] = 0
                        self.offsets.add(0)
                    elif parts[1] in regmap:
                        regmap[parts[0]] = regmap[parts[1]]
                    else:
                        regmap.pop(parts[0], None)
                    continue

            # [sp, #imm]
            m = SP_MEM.search(args)
            if m:
                off = int(m.group(1), 0) if m.group(1) else 0
                self.offsets.add(off)
                self.mem_insns.append((ins, off))

            # [rX, #imm] where rX is a known sp alias: records the offset so it
            # cannot be mistaken for free space.  Needs no patch -- base and
            # displacement shift together.
            m = REG_MEM.search(args)
            if m and m.group(1) in regmap:
                self.offsets.add(regmap[m.group(1)] + int(m.group(2), 0))

            # a Parcel is born
            if ins.target in ctor_addrs:
                    if "r0" not in regmap:
                        raise Fatal(f"{self.name}: cannot tell where the "
                                    f"Parcel at 0x{ins.addr:x} lives")
                    self.sites.append(Site(ins.addr, regmap["r0"]))

            # clobbers
            if mn.startswith(("bl", "blx")):
                for r in ("r0", "r1", "r2", "r3", "r12", "lr"):
                    regmap.pop(r, None)
            elif mn.startswith(("pop", "ldm", "ldrd")):
                for r in re.findall(r"\br\d+\b", args):
                    regmap.pop(r, None)
            elif mn.split(".")[0] not in NO_DEST:
                parts = [p.strip() for p in args.split(",")]
                if parts and REG.match(parts[0]):
                    regmap.pop(parts[0], None)

        if self.sites and self.frame is None:
            raise Fatal(f"{self.name}: builds a Parcel but allocates no frame")

    def plan(self, size):
        """Return {threshold: delta} needed to give every Parcel `size` bytes."""
        deltas = {}
        ordered = sorted(self.offsets)
        for site in self.sites:
            above = [o for o in ordered if o > site.offset]
            if above:
                thresh = above[0]
                room = thresh - site.offset
            else:
                thresh = self.frame
                room = self.frame - site.offset
            if room >= size:
                continue
            need = size - room
            need = (need + 7) & ~7
            deltas[thresh] = max(deltas.get(thresh, 0), need)
        return deltas


# --------------------------------------------------------------------------
# Thumb re-encoding: only the immediate changes, never the length
# --------------------------------------------------------------------------
def reencode(ins, new):
    """Return the new little-endian bytes for `ins` carrying immediate `new`."""
    if len(ins.halfwords) == 1:
        hw = ins.halfwords[0]
        if hw & 0xFF80 in (0xB000, 0xB080):          # add/sub sp, #imm7*4
            if new % 4 or new > 508:
                raise Fatal(f"0x{ins.addr:x}: {new} not encodable in sub sp")
            return ins.encode([(hw & 0xFF80) | (new >> 2)])
        if hw & 0xF800 == 0xA800:                    # add rD, sp, #imm8*4
            if new % 4 or new > 1020:
                raise Fatal(f"0x{ins.addr:x}: {new} not encodable in add rX,sp")
            return ins.encode([(hw & 0xFF00) | (new >> 2)])
        if hw & 0xF000 == 0x9000:                    # ldr/str rT, [sp, #imm8*4]
            if new % 4 or new > 1020:
                raise Fatal(f"0x{ins.addr:x}: {new} not encodable in ldr/str sp")
            return ins.encode([(hw & 0xFF00) | (new >> 2)])
        raise Fatal(f"0x{ins.addr:x}: unhandled 16-bit form {hw:04x} "
                    f"({ins.mnem} {ins.args})")

    if len(ins.halfwords) != 2:
        raise Fatal(f"0x{ins.addr:x}: unexpected instruction length")
    hw1, hw2 = ins.halfwords
    # ADD/SUB (immediate), T3 modified-immediate or T4 plain imm12, Rn = SP
    if hw1 & 0xFBEF == 0xF10D or hw1 & 0xFBFF == 0xF20D:
        return _encode_addsub(ins, hw1, hw2, new, sub=False)
    if hw1 & 0xFBEF == 0xF1AD or hw1 & 0xFBFF == 0xF2AD:
        return _encode_addsub(ins, hw1, hw2, new, sub=True)
    # STR.W/LDR.W Rt, [SP, #imm12]
    if hw1 in (0xF8CD, 0xF8DD):
        if new > 4095:
            raise Fatal(f"0x{ins.addr:x}: {new} exceeds imm12")
        return ins.encode([hw1, (hw2 & 0xF000) | new])
    # STR/LDR Rt, [SP, #imm8] with P=1 U=1 W=0
    if hw1 in (0xF84D, 0xF85D) and hw2 & 0x0F00 == 0x0C00:
        if new <= 255:
            return ins.encode([hw1, (hw2 & 0xFF00) | new])
        if new <= 4095:                      # widen to the imm12 form
            return ins.encode([0xF8CD if hw1 == 0xF84D else 0xF8DD,
                               (hw2 & 0xF000) | new])
        raise Fatal(f"0x{ins.addr:x}: {new} exceeds imm12")
    raise Fatal(f"0x{ins.addr:x}: unhandled 32-bit form {hw1:04x} {hw2:04x} "
                f"({ins.mnem} {ins.args})")


def _encode_addsub(ins, hw1, hw2, new, sub):
    if hw1 & 0x0010:
        raise Fatal(f"0x{ins.addr:x}: flag-setting add/sub cannot be widened")
    if new > 4095:
        raise Fatal(f"0x{ins.addr:x}: {new} exceeds imm12")
    # the plain imm12 form (ADDW/SUBW) encodes 0..4095 with no rotation, is the
    # same length as T3, and differs only in not setting flags -- which T3 with
    # S=0 does not do either.
    base = 0xF2AD if sub else 0xF20D
    i = (new >> 11) & 1
    imm3 = (new >> 8) & 7
    imm8 = new & 0xFF
    rd = (hw2 >> 8) & 0xF
    return ins.encode([base | (i << 10), (imm3 << 12) | (rd << 8) | imm8])


# --------------------------------------------------------------------------
# driver
# --------------------------------------------------------------------------
def find_tool(name, override=None):
    """Locate a host llvm tool inside the tree.

    The build runs from the tree top, and the clang prebuilt is versioned in
    its path (clang-r353983d today, something else next release), so the
    version is globbed rather than named.  Q defines no make variable for this
    -- the path moved into Soong -- which is why the tool finds its own tools.
    """
    if override:
        if not os.path.exists(override):
            raise Fatal(f"{name}: {override} does not exist")
        return override
    top = os.environ.get("ANDROID_BUILD_TOP") or os.getcwd()
    hits = sorted(glob.glob(os.path.join(
        top, "prebuilts/clang/host/*-x86/clang-*/bin", name)))
    if hits:
        return hits[-1]
    found = shutil.which(name)
    if found:
        return found
    raise Fatal(f"{name} not found under {top}/prebuilts/clang or on PATH")


def probe_size(nm, probe):
    if not os.path.exists(probe):
        raise Fatal(f"parcel size probe not built: {probe}")
    proc = subprocess.run([nm, "--print-size", "--defined-only", probe],
                          capture_output=True, text=True)
    if proc.returncode != 0:
        raise Fatal(f"{nm} failed on {probe}: {proc.stderr.strip()}")
    for line in proc.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 4 and parts[-1] == PROBE_SYMBOL:
            size = int(parts[1], 16)
            if size < 8 or size > 4096:
                raise Fatal(f"{PROBE_SYMBOL} has implausible size {size}")
            return size
    raise Fatal(f"{PROBE_SYMBOL} not found in {probe}; cannot learn "
                f"sizeof(android::Parcel)")


def functions_of(elf, insns, ctor_addrs, arm_ranges=()):
    names = {}
    for s in elf.dyn_symbols():
        if s["type"] == STT_FUNC and s["shndx"] and s["value"]:
            names[s["value"] & ~1] = s["name"]

    # The exception index is not quite a list of every function: the linker
    # merges adjacent entries that share an unwind descriptor, so a run of
    # identically-shaped functions arrives as one (BpPHSClient::GetTid is
    # folded into GetPid).  The symbol table names the exported ones it misses,
    # and the union of the two is what bounds a function here.  Anything still
    # merged shows up as two frame sizes in one body, which Function.analyse
    # refuses outright rather than patching blind.
    starts = sorted(set(elf.exidx_starts()) | set(names))
    text = elf.section(".text")
    limit = text["addr"] + text["size"]

    def calls_ctor(ins):
        return ins.target in ctor_addrs

    def is_arm(addr):
        return any(lo <= addr < hi for lo, hi in arm_ranges)

    out = []
    for i, start in enumerate(starts):
        end = starts[i + 1] if i + 1 < len(starts) else limit
        body = [ins for ins in insns if start <= ins.addr < end]
        if not body or not any(calls_ctor(ins) for ins in body):
            continue
        if is_arm(start):
            # Decoded as Thumb it is noise, so the "call" is not real; but a
            # genuine A32 caller would need encodings this tool does not have.
            raise Fatal(f"0x{start:x}: a Parcel appears to be built in A32 "
                        f"code, which this tool cannot re-encode")
        out.append(Function(names.get(start, f"sub_{start:x}"),
                            start, end, body))

    covered = {ins.addr for fn in out for ins in fn.insns}
    for ins in insns:
        if calls_ctor(ins) and ins.addr not in covered and not is_arm(ins.addr):
            raise Fatal(f"Parcel built at 0x{ins.addr:x}, outside every "
                        f"function named by .ARM.exidx")
    return out


def refit(path, size, objdump, nm, dry_run=False):
    elf = Elf32(path)
    syms = elf.dyn_symbols()
    if not any(s["name"] in PARCEL_CTORS and not s["shndx"] for s in syms):
        return None

    name = os.path.basename(path)
    if name in UNREACHABLE:
        print(f"  {name}: skipped -- {UNREACHABLE[name]}")
        return None

    # A32 and T32 can coexist: these blobs are Thumb but carry statically
    # linked compiler helpers (__aeabi_*, __udivdi3) built as ARM.  Those never
    # touch a Parcel, so the mode that matters is per function, not per file --
    # see functions_of, which refuses to guess about the ones that do.
    arm_ranges = [(s["value"], s["value"] + max(s["size"], 4))
                  for s in syms
                  if s["type"] == STT_FUNC and s["shndx"] and s["value"]
                  and not s["value"] & 1]

    insns = disassemble(objdump, path, thumb=True)
    ctor_addrs = {a for a, n in elf.plt_stubs().items() if n in PARCEL_CTORS}
    if not ctor_addrs:
        raise Fatal(f"{path}: imports a Parcel constructor but no PLT stub "
                    f"was found")

    funcs = functions_of(elf, insns, ctor_addrs, arm_ranges)
    if not funcs:
        raise Fatal(f"{path}: Parcel constructor is called from outside any "
                    f"known function")

    total_sites = changed = 0
    for fn in funcs:
        fn.analyse(ctor_addrs)
        total_sites += len(fn.sites)
        deltas = fn.plan(size)
        if not deltas:
            continue

        def shift(off):
            return sum(d for t, d in deltas.items() if t <= off)

        grow = sum(deltas.values())
        edits = []
        for ins, off in fn.base_insns:
            if shift(off):
                edits.append((ins, off + shift(off)))
        for ins, off in fn.mem_insns:
            if shift(off):
                edits.append((ins, off + shift(off)))
        for ins in fn.frame_insns:
            edits.append((ins, fn.frame + grow))

        for ins, new in edits:
            data = reencode(ins, new)
            if len(data) != ins.size:
                raise Fatal(f"{fn.name}: re-encoding changed instruction size")
            if not dry_run:
                off = elf.addr_to_offset(ins.addr)
                elf.data[off:off + ins.size] = data
        changed += 1
        print(f"  {name}: {fn.name} frame {fn.frame} -> {fn.frame + grow}, "
              f"{len(fn.sites)} Parcel(s), {len(edits)} instruction(s)")

    if changed and not dry_run:
        elf.save()
        verify(path, size, objdump)
    return (total_sites, changed)


def verify(path, size, objdump):
    """Re-read the patched blob and insist every Parcel now fits."""
    elf = Elf32(path)
    insns = disassemble(objdump, path, thumb=True)
    ctor_addrs = {a for a, n in elf.plt_stubs().items() if n in PARCEL_CTORS}
    for fn in functions_of(elf, insns, ctor_addrs):
        fn.analyse(ctor_addrs)
        left = fn.plan(size)
        if left:
            raise Fatal(f"{path}: {fn.name} still cramped after patching "
                        f"({left}) -- the blob has been left modified and "
                        f"must be restored from git")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--probe", required=True,
                    help="parcel_size_probe archive built for the target")
    ap.add_argument("--objdump", help="defaults to the tree's clang prebuilt")
    ap.add_argument("--nm", help="defaults to the tree's clang prebuilt")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("roots", nargs="+")
    args = ap.parse_args()

    try:
        objdump = find_tool("llvm-objdump", args.objdump)
        nm = find_tool("llvm-nm", args.nm)
        size = probe_size(nm, args.probe)
        print(f"parcel-refit: sizeof(android::Parcel) = {size} "
              f"(measured, not assumed)")

        targets = []
        for root in args.roots:
            if os.path.isfile(root):
                targets.append(root)
                continue
            for dirpath, _dirs, files in os.walk(root):
                for f in files:
                    p = os.path.join(dirpath, f)
                    if os.path.islink(p) or not f.endswith(".so"):
                        continue
                    with open(p, "rb") as fh:
                        if fh.read(4) != b"\x7fELF":
                            continue
                    targets.append(p)

        touched = 0
        for p in sorted(targets):
            r = refit(p, size, objdump, nm, args.dry_run)
            if r and r[1]:
                touched += 1
        print(f"parcel-refit: {touched} blob(s) refitted")
    except Fatal as exc:
        sys.stderr.write(f"\nparcel-refit: FATAL: {exc}\n")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
