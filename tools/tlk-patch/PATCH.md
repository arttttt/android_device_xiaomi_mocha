<!-- Copyright (C) 2026 Artem Bambalov; SPDX-License-Identifier: GPL-2.0-only -->

# TLK 0.2 UART and timeout image

`tos-psci-0.2-uart-timeouts.img` is a diagnostic binary patch of the original
TLK 0.2 image. It routes `printf` to UART, adds initialization markers, and
bounds FUSE, TIMERUS and UART TX waits. The owner's first-image device test,
reported by the team lead, reached `W Welcome A F 1 2 ... J init complete`
on cold boot. A menu reboot stopped after `WWelcome to TLK` / `AF`, without
`1`, `J` or `FUSE timeout`. The second image below instruments that interval.
The second-image menu reboot, also reported by the lead, stopped at
`AIBCDEFGHJKL`, locating the stall inside `0x480029c8` before `M`.
PSCI/LP0 validation and a third-image device test remain outstanding.

| Property | Value |
| --- | --- |
| Input SHA256 | `a05195438bc84f0194def15df30b9f7a357aecbb2bd2d91c19430b2033d2fcf2` |
| Output SHA256 | `544cc16b1b8c08bf1e9ac5d6f2fde1a58294465428de267a2bcb8c371fc0f6ba` |
| Image size | `0x197200` (1,667,584 bytes), unchanged |
| Header | All 512 bytes unchanged, including `NVTOSP\0` and `1667072` |
| Address mapping | VA = file offset − `0x200` + `0x48000000` |
| Added storage | 236 of 240 bytes at VA `0x4800ef10..0x4800efff` |
| `printf` flag | VA `0x4800f010`, file `0xf210`: `1` → `0` |

The complete per-word before/after table and compiled symbol addresses are in
`tos-psci-0.2-uart-timeouts.patch.json`. `ghidra-verified.log` records the
cave audit and the disassembly of every modified instruction and trampoline.

## UART markers

Markers are single characters without added whitespace. They go directly
through the bounded UARTD transmitter, preserving arguments, scratch
registers, LR, flags and the original caller's stack layout.

| Marker | Hook VA | Meaning |
| --- | --- | --- |
| `W` | `0x48004e50` | Immediately before printing `Welcome to TLK` |
| `A` | `0x48004e54` | First call after Welcome, destination `0x48009064` |
| `F` | `0x48004e94` | Before creating the `bootstrap2` thread |
| `J` | `0x480084c0` | Before entering `0x48002568`, which prints init complete |
| `1` | `0x48002b54` | Enter FUSE wait; once per invocation (`N=1`) |
| `2` | `0x48002b6c` | FUSE reached IDLE; before reading SECURITY_MODE_0 |

The order of `1`/`2` relative to the main-thread markers depends on scheduling.
`FUSE timeout\r\n` means the FUSE wait exhausted its budget; `2` is absent
on that path. The message uses existing `dputs`, whose `putchar` is bounded.
`printf` errors and allocation/halt messages now also go to UART.

The first image uses a reduced ladder: the full set of intermediate hooks,
the early kmain hook and the delay M3 marker are omitted. Ghidra found 51
references into the proposed `0x48001340` zero region and three into
`0x480018ec`; both contain live data and remain unchanged. The actual
`0x4800ef10` cave has no references or containing functions in the analyzed
project. Subsequent instrumentation can focus on the interval identified
by the first reproduction, without consuming unverified storage.

## Second image: resume and platform initialization

`tos-psci-0.2-uart-stage2.img` starts from the same original image, retains
the `printf` flag and all three wait limits, and replaces the first marker
ladder with 14 uppercase markers. Its SHA256 is
`966545babc1bc9c6d8d8efbc66368833bc9b7e1d86e4d3624ab2012eeea94dad`.
The 512-byte header and `0x197200` file size remain unchanged. Added code
and lookup data occupy exactly 240 bytes at `0x4800ef10..0x4800efff`.

| Marker | Hook VA | Meaning and interval if it is the last marker |
| --- | --- | --- |
| `A` | `0x48004e98` | `thread_create` returned; before `thread_resume` at `0x48008200`. No `B` means bootstrap2 entry has not been observed. |
| `B` | `0x48004dfc` | bootstrap2 entry, before the empty `0x48004558` call and platform_init. Before `C`, includes platform_init entry and `0x48002bb0`. |
| `C` | `0x4800c124` | Before PMC initialization at `0x4800bffc`; no `D` means that call has not returned to its next hook. |
| `D` | `0x4800c12c` | `0x4800bffc` returned; before `0x4800bb14`. Before `E`, includes bb14's allocations. |
| `E` | `0x4800bb9c` | Before `0x48000c84`; no `F` localizes to this call or its return boundary. |
| `F` | `0x4800bba0` | Before `0x4800c3e0`; before `G`, also includes the unmarked `0x48000c98` call. |
| `G` | `0x4800bba8` | Before `0x48000c5c`; no `H` localizes to this call or its return boundary. |
| `H` | `0x4800bbac` | Before `0x48000da4`; before `J`, includes the following argument setup. |
| `I` | `0x480084bc` | kmain has become the idle thread with priority 0 and reached its first `thread_yield` at `0x48008198`. This is an entry marker, not a WFI/sleep marker. |
| `J` | `0x4800bbc0` | Before `0x4800b930`; no `K` means its return to the tail path has not been observed. |
| `K` | `0x4800bbc8` | Before the tail call to `0x4800b990`; replays the original pop and unchanged branch at `0x4800bbcc`. |
| `L` | `0x4800263c` | `0x48002bb0` and its bb14 tail returned; before `0x480029c8`. No `M` localizes to this interval. |
| `M` | `0x48002640` | Before `0x48001a98`, which includes the FUSE path and later initialization. The FUSE timeout remains active. |
| `N` | `0x480084c0` | The idle thread's first yield returned; before `0x48002568`, which prints initialization complete and enters the non-secure world. This may be the normal final TLK marker. |

Characters can interleave with normal UART text. Table order is not execution
order: `I` belongs to kmain while `B..H`, `J..M` belong to bootstrap2.
`I` without `B` means kmain reached its first idle yield before any observed
bootstrap2 entry. `B` without `I` means bootstrap2 entered, but kmain has not
yet reached that yield; the last platform marker narrows the interval.
These observations identify control-flow boundaries, not the cause of a
stall. A dead UART drops markers after the bounded TX budget.

The boot idle path at `0x4800847c` renames the current thread to `idle`
(literal `0x4800e4cc`, call `0x48008438`), sets priority 0 via `0x48008454`,
and calls `thread_yield` once at `0x480084bc`. The yield enqueues the thread
via `0x48007f4c` and reschedules via `0x48007e90`. After it returns, the
original loop `0x480084c4 -> 0x480084c0` calls the platform jumpback path.
Consequently `I` is outside this back edge and occurs once per boot idle
entry without a flag or extra storage.

The WFI loop at `0x48000ce4..0x48000d04` is separate: Ghidra finds callers
`0x4800bce0` in `0x4800bc94` and `0x4800be18` in `0x4800bcf0`, after
monitor power-management preparations. It is not the boot scheduler's
idle-yield path, and the second image leaves the entire loop unchanged.

The `c98` marker, old `W/A/F/J/1/2` ladder, PMC latch marker and TIMERUS
scale marker are omitted to fit the audited cave. `A` supplies the anchor
after thread creation. The scale calculation begins at `0x48002868`;
`0x48002864` is the `0x60005000` literal, not an instruction hook.

All displaced BLs retain their original return address and destination.
The shared lookup dispatcher uses a 32-byte frame, preserves the original
stack alignment, and restores
r0-r4, ip, flags and SP. The `K` hook restores bb14's original r4-r7/LR
frame before its original tail branch. Full per-word changes are in
`tos-psci-0.2-uart-stage2.patch.json`; the final Ghidra audit and disassembly
are in `ghidra-stage2-final-verified.log`.

The second image is prepared locally; this agent has not flashed it or
committed changes. The delivery path is
`tos-psci-0.2-uart-stage2.img`.

## Third image: bounded key copy to TZRAM

`tos-psci-0.2-uart-stage3.img` has SHA256
`00161e2102504bdac77c8bf364fc1b1de32afe3d367b0e93495cb20de2593300`.
It retains the UART routing flag and all FUSE, TIMERUS and TX limits.
The header and file size are unchanged. It uses 236 of the same 240-byte
audited cave and replaces the 80-byte function at `0x480029c8` in place.
The first and second images, including their manifests, still reproduce
byte for byte.

This function corresponds to `platform_setup_keys` in the local TLK source
at `tlk-src (NVIDIA tlk, nv-tegra) platform/tegra/common/memory.c`.
Its destination is TZRAM at `0x7c010000`, size `0x10000` bytes, matching
`platform/tegra/include/platform/memmap.h` and the Tegra K1 TRM address map.
The copied data are encrypted keys. Earlier discussion identifying this
copy as WB0 was an inference and is superseded by the source comparison.

The patch preserves the binary's two key-header layouts: a zero first word
selects `boot_params + 0x68`; a nonzero first word selects
`boot_params + 4 + align4(first_word)`. The length is the selected header's
first word, and the source starts four bytes later. The newer source tree's
fixed `sizeof(boot_params_t)` layout does not replace this binary contract.
The source pointer and leading length are not additionally validated in
this diagnostic image; an invalid leading word can still select an invalid
header before the copy-size check.

The original CAR read/modify/write is preserved:
`[0x60006360] |= 0x40000000`, enabling the TZRAM clock. The function prints
`P` before that operation and `Q` after the store. It then prints the raw
32-bit length through the existing `printf`, reusing `%08x |` at
`0x4800e928`. At lengths `0..0x10000`, it calls the original `memcpy` with
the exact original destination/source/length and prints `R` after return.
Zero length follows the original no-op copy path. An unsigned length above
`0x10000` prints `X`, skips all destination writes and returns to the caller.
The keys are not truncated.

| Marker | Hook or print VA | Meaning if it is the last observed marker |
| --- | --- | --- |
| `A` | `0x48004e98` | Thread creation returned; before thread_resume. |
| `B` | `0x48004dfc` | Entered bootstrap2; before platform initialization. |
| `I` | `0x480084bc` | kmain became idle with priority 0; before its first yield. |
| `L` | `0x4800263c` | Before platform_setup_keys. Without `P`, header selection/length loading has not completed or its UART marker was dropped. |
| `P` | `0x480029f8` | Before CAR read/modify/write. Without `Q`, the subsequent CAR operation or marker output is the unresolved interval. |
| `Q` | `0x4800efcc` | CAR store instruction completed; before formatted length output. It does not prove hardware clock readiness. |
| eight hex digits followed by ` |` | `0x4800efd8` | Raw encrypted-key length, before the unsigned size check and copy. Without `R`/`X`, the copy or its return/output is the unresolved interval. |
| `R` | `0x4800eff4` | The bounded-size copy returned. |
| `X` | `0x4800eff4` | Length exceeded 64 KiB; the copy was skipped. |
| `M` | `0x48002640` | platform_setup_keys returned; before the FUSE path. |
| `N` | `0x480084c0` | Before initialization complete and the non-secure jumpback. |

For example, `LPQ0000002c |RM` means 44 bytes copied and initialization
continued. `LPQdeadbeef |XM` means the raw size was invalid, no TZRAM data
were written and initialization continued. Bootstrap2 and kmain markers
can interleave. These are software control-flow observations; they do not
establish the underlying warm-reset cause.

Stage2 hooks `C/D/E/F/G/H/J/K` are removed, with their sites restored to
original instructions. The six anchors keep their original characters
through a packed table containing a four-bit marker rank and fourteen-bit
word offsets for each return PC and displaced target. The dispatcher saves
r0-r5, ip and LR in a 32-byte frame, restores flags and the original BL LR,
then transfers to the original target. Existing putchar is byte-identical
to stage2. The copy wrapper saves destination, source, length and original
LR in an aligned 16-byte frame across diagnostics; return restores SP and
callee-saved registers. The FUSE entry omits a redundant initial `r0=1`:
the unchanged delay shim still sets it before every actual delay call.

`tos-psci-0.2-uart-stage3.patch.json` records every changed word and compiled
symbol; `ghidra-stage3-final-verified.log` records the final disassembly.
The 14 stage3 ARM tests execute real printf and memcpy instructions, check
both parameter layouts, zero/one/unaligned/exact-64-KiB copies, unsigned
oversize values, destination canaries, CAR/marker ordering, dead UART,
continuation from `L` through `X` to `M`, anchor registers/flags/LR/SP and
all-round reproduction. No flashing or commits are performed. Delivery:
`tos-psci-0.2-uart-stage3.img`.

## Wait behavior

- FUSE: at most 8192 status polls, each preceded by the original 1 µs delay.
  On timeout, print `FUSE timeout`, write result zero without reading
  SECURITY_MODE_0, and execute the original visibility restoration path.
  The extra saved registers keep the frame aligned and preserve caller r7/r8.
- Delay: retain the original 64-bit clock calculation and wrap handling.
  Exit after 65536 consecutive stalled elapsed samples. Any progress resets
  the budget, so healthy long delays are preserved. Repeated readings of
  the same microsecond are expected and allowed. A stalled-clock exit is
  silent in this image.
- UART TX: at most 65536 LSR reads per character (twice for CRLF). If TX
  never becomes ready, drop the character and return. Preserve console
  gates, selected UART, NUL suppression and CRLF conversion for `putchar`.

## Reproduction

The patcher requires Python 3 and Clang's ARM assembler, with no Python
packages. It rejects a different input SHA256, changes only listed words,
and refuses to overwrite the original or an existing output.

```sh
python3 -I tools/tlk-patch/patch_tlk.py \
  tos-psci-0.2-original.img \
  /tmp/tos-psci-0.2-uart-timeouts.img

python3 -I tools/tlk-patch/patch_tlk.py \
  tos-psci-0.2-original.img \
  /tmp/tos-psci-0.2-uart-stage2.img --round 2

python3 -I tools/tlk-patch/patch_tlk.py \
  tos-psci-0.2-original.img \
  /tmp/tos-psci-0.2-uart-stage3.img --round 3
```

`tlk_uart_timeout.S` contains the injected ARM assembly. The patcher resolves
Clang's local ARM branch relocations before copying code and enforces cave
and function boundaries. The original console literal pool is retained.

## Verification

The 13 unit/integration tests use Unicorn 2.1.4 and Capstone 5.0.9. They run
actual patched instructions with modeled MMIO: healthy/frozen TIMERUS,
timer rollover, 1024 identical samples per microsecond and more than 65536
total reads during a healthy delay, FUSE success on the last permitted
poll, FUSE timeout, visibility cleanup, assertion argument compatibility,
console gates, UART selection, CRLF, dead UART, original LR at non-BL hooks,
register/flag/stack preservation, exact manifest coverage and reproduction.

```sh
python3 -m venv /tmp/tlk393-test-venv
/tmp/tlk393-test-venv/bin/python -I -m pip install unicorn==2.1.4 capstone==5.0.9
/tmp/tlk393-test-venv/bin/python -I tools/tlk-patch/test_patch_tlk.py
/tmp/tlk393-test-venv/bin/python -I tools/tlk-patch/test_patch_tlk_stage2.py
/tmp/tlk393-test-venv/bin/python -I tools/tlk-patch/test_patch_tlk_stage3.py
```

Ghidra 12.1.4 verification uses the existing original `tlk_p02` project in
read-only mode. The verification script patches its in-memory copy and
disassembles it; the original project remains unchanged.
The second suite adds 11 tests covering all lookup rows with healthy/dead
UART, all 32 NZCV/Q combinations, tail-frame replay, the single idle-yield
marker outside the original jumpback loop, both image reproductions and
the unchanged UART/FUSE/delay behavior.

```sh
env JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home \
  /opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless \
  <ghidra-project-dir> tlk_p02 \
  -process tos-psci-0.2.img -readOnly -noanalysis \
  -scriptPath tools/tlk-patch/ghidra \
  -postScript AuditPatchCaves.java \
  -postScript VerifyPatchedTlk.java \
  tools/tlk-patch/tos-psci-0.2-uart-timeouts.img \
  -scriptlog /tmp/tlk393-ghidra.log

env JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home \
  /opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless \
  <ghidra-project-dir> tlk_p02 \
  -process tos-psci-0.2.img -readOnly -noanalysis \
  -scriptPath tools/tlk-patch/ghidra \
  -postScript AuditPatchCaves.java \
  -postScript AuditIdleEntry.java \
  -postScript VerifyPatchedTlk.java \
  tools/tlk-patch/tos-psci-0.2-uart-stage2.img 2 \
  -scriptlog /tmp/tlk393-stage2-ghidra.log

env JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home \
  /opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless \
  <ghidra-project-dir> tlk_p02 \
  -process tos-psci-0.2.img -readOnly -noanalysis \
  -scriptPath tools/tlk-patch/ghidra \
  -postScript AuditPatchCaves.java \
  -postScript VerifyPatchedTlk.java \
  tools/tlk-patch/tos-psci-0.2-uart-stage3.img 3 \
  -scriptlog /tmp/tlk393-stage3-ghidra.log
```

## The original image

`prebuilt/firmware/tos-psci-0.2.img` in this tree is the stage 3 output.
The patcher takes the unpatched original (sha256 `a0519543...d2fcf2`), which
is in this tree's history:

```sh
git show 948991b:prebuilt/firmware/tos-psci-0.2.img > tos-psci-0.2-original.img
```
