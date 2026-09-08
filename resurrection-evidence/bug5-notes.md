# Bug #5: parsa/stabilizer `-Rcode` corruption on tiny / C++-static-heavy binaries

Investigation working directory. Fresh clone of
`~/prog/stabilizer-parsa-fix/stabilizer` (base commit `b274f86`, which already
carries the `-Rheap`/`-Rstack`/`-Rcode` heap fixes and the teardown-timer fix).
All builds and all execution of instrumented binaries happen inside rootless
podman (`localhost/stabilizer-parsa-fix` image, container `szc-bug5`), never on
the host.

**Status: root cause found and confirmed (three distinct defects). The leading
hypothesis — linker GOT/PLT relaxation desyncing `--emit-relocs` records from the
final bytes — is REFUTED.** One of the three defects has a small, safe,
correct-direction fix applied (5a); the other two (5b, 5c) are structural and
need proper design, not the alignment hack I tried (it fixes 5b but activates 5c
fatally — see below).

## Reproducer (minimal, deterministic)

`repro/teardown.cpp` (the abandoned synthetic test archived by the teardown-fix
agent) built with `szc -Rcode` and run under the runtime aborts 100% of the
time, at process startup, before any program output — or, once 5b is worked
around, at exit-time during the C++ global destructor.

```
cd repro && ../stabilizer/szc -Rcode -o teardown teardown.cpp
LD_LIBRARY_PATH=../stabilizer ./teardown
```

What makes it trip the bug, characterised:
- **C++ static objects.** A global object with a constructor/destructor forces
  the compiler to emit `_GLOBAL__sub_I_<tu>` and `__cxx_global_var_init` thunks,
  which are *tiny* (16 bytes) and sit right next to each other — this triggers
  5b. A C-only program (no static ctors) does not emit these and is far less
  likely to trip it.
- **Small size.** Small binaries pack functions tightly and let the CRT startup
  stubs (`_start`, `frame_dummy`, `register_tm_clones`, …) interleave with the
  module's functions and their pass-inserted dummies, triggering 5c.
- Not size-of-relocation, not a runtime race: the corruption is baked into the
  as-built binary layout at link time and reproduces on every run.
- `libquantum` (the existing `-Rcode` vehicle) does NOT trip it: all its
  functions are large (> 32 bytes) and spaced apart, so neither 5b nor 5c fires.

The relocation records in the final binary match the final instruction bytes
exactly (checked with `objdump -dr` + `readelf --relocs`; see
`logs/objdump-teardown.log`, `logs/readelf-relocs.log`). There is no stale /
relaxed-instruction mismatch. **Linker relaxation is not the cause.**

## How `-Rcode` works (the parts that matter here)

The pass (`pass/Stabilizer.cpp::randomizeCode`) rewrites every global reference
in a function to a load from a per-function *relocation table*. On x86_64
(`isDataPCRelative` == true) the table is addressed *relative to a dummy function*
`stabilizer.dummy.<f>` that the pass inserts immediately after `f` in the IR
(`insertAfter`), and `_tableAdjacent` is set true so the runtime copies the table
immediately after the function's code. `codeLimit` (hence `code.size()`) is taken
to be the address of that dummy.

At run time the runtime installs a 32-byte `FunctionHeader` at each function's
entry, traps it, and on first call copies `code.size()` bytes (+ the adjacent
table) to a random address, restores the saved header, and patches any
pc-relative fields whose target is *outside* the copied blob
(`runtime/Function.cpp::applyTextRelocs`, using `--emit-relocs` records read from
`/proc/self/exe` in `runtime/TextRelocations.cpp`).

## Root cause: three distinct defects, all in Stabilizer's own code

All three are exposed by small / tightly-packed / C++-static-heavy binaries.
Confirmed by runtime debug instrumentation, static disassembly, and an
independent codex adversarial review (`logs/codex-verdict.txt`,
`logs/codex-run.log`) that verified each against the clang record-layout dump and
the final ELF bytes.

### 5a. `applyTextRelocs` internal/external test keys on the symbol value, not the target

For `R_X86_64_PC32`/`PLT32` the code computed `S = oldVal + oldP - addend` (the
ELF *symbol value* `S`) and left the field alone iff `S` was inside the copied
range. But references to the function's own adjacent relocation table are emitted
against the `.text` **section symbol** plus a large addend. So `S` is the section
base (0x1080, *outside* the function at 0x1090) while the actual target `S + A =
oldVal + oldP` (0x1200, the adjacent table) is *inside*. The check misclassified
these internal table references as external and rewrote their displacement
(`oldVal - delta`), making the copied code's table loads point back near the
original binary → wild read → SIGSEGV.

Runtime evidence (`logs/run-02-stderr.log`, my `BUG5` debug print), for
`__cxx_global_var_init`:
```
off=21 addend=396 oldVal=359 S=0x…35080 target=0x…35210  Sinternal=0 targetinternal=1
off=28 addend=404 oldVal=360 S=0x…35080 target=0x…35218  Sinternal=0 targetinternal=1
```
`S` tests external; the real target (adjacent table slot) is internal.

**Fix applied** (`runtime/Function.cpp`, ~1 line): test `target = oldVal + oldP`
(= `S + A`) instead of `S`. Verified: those relocs flip to `internal=1`
(`logs/run-04-stderr.log`). Correct direction, in Stabilizer's own source.
**Caveat found by codex and left documented in the code:** `S + A` is ~4 bytes
short of the true x86 RIP effective address (`P + 4 + disp`), so a reference to
the very first/last few bytes of the copied region could still be misclassified.
Not exercised by the known reproducers, but the containment test is not
bullet-proof.

5a alone does **not** fix the reproducer — it then aborts on 5b instead
(`logs/run-06-stderr.log`: `Text relocation overflow (PC32/PLT32) off=7`, the
exact abort the teardown-fix agent recorded).

### 5b. `FunctionHeader` (32 bytes) overwrites the next function when functions are < 32 bytes apart

`sizeof(FunctionHeader) == 32`: a `union{ uint8_t jmp[21]; uint8_t trap[1]; }`
(x86-64 `Jump` = `X86Jump64` = 21 bytes) followed by `Function* _f` at offset 24.
The runtime `placement new`s this at every function's entry. But tiny CRT/C++
thunks are only 16 bytes apart — e.g. `_GLOBAL__sub_I` at `.text` 0x1080,
`__cxx_global_var_init` at 0x1090. So `_GLOBAL__sub_I`'s `_f` back-pointer, at
offset 24 → **0x1098**, is written straight into `__cxx_global_var_init`'s body,
clobbering the displacement field of its first table-load instruction (`off=7`,
bytes 0x1097–0x109a). Byte 0x1097 (0x65) survives; 0x1098–0x109a become the low
bytes of a heap `Function*` (~0xC0…). The field now reads a ~−1.6 GB garbage
displacement; on relocation the runtime tries to rebase it and int32-overflows →
`ABORT: Text relocation overflow`. (Worse: it also corrupts the *saved* header
snapshot, so even a correct restore replays the clobber.)

Evidence: `off=7` reads `oldVal=-1073590171` on the *first* relocation, a heap
pointer, not the original `0x165` (`logs/run-04-stderr.log`). Controlled
confirmation: forcing every function to `Align(64)` gives `_GLOBAL__sub_I` its
own 64-byte slot, and `off=7` then reads the correct value and the overflow abort
disappears (`logs/run-05-stderr.log` shows `off=7 oldVal=373 internal=1`).

This is a violation of an unwritten design assumption: **every function has ≥
`sizeof(FunctionHeader)` = 32 bytes of exclusive space at its entry.** No small,
obviously-safe fix — the `Align(64)` experiment perturbs layout and activates 5c
(below). Directions worth designing/reviewing: skip randomising functions smaller
than the header; shrink the header (the 21-byte `X86Jump64` slot dominates — a
near jump is 5 bytes when the copy is within ±2 GiB, which `MAP_32BIT` already
arranges); or force per-function alignment **and** fix 5c so the size computation
survives the reordering.

### 5c. `codeLimit`/`code.size()` assume the pass-inserted dummy stays physically adjacent — it does not

The pass takes `codeLimit` = the dummy `stabilizer.dummy.<f>` inserted right after
`f` in the IR. LLVM codegen + the linker do **not** preserve IR order in the final
layout: the dummies (and CRT stubs) get reordered/clustered away from their
functions. For `_ZN6GlobalD2Ev` (the `Global` destructor, run at exit) in the
unaligned original reproducer:

```
_ZN6GlobalD2Ev                   0x1560
stabilizer.dummy._ZN6GlobalD2Ev  0x1240      # BEFORE the function
```
so `code.size() = 0x1240 - 0x1560` underflows to `0xffffffffffffface0`; the copy
allocation requests a wrapped size → `ABORT: Couldn't allocate memory for function
relocation` (`FunctionLocation.h`). Confirmed by `nm` on the unaligned binary
(`logs/nm-current-unaligned.log`) — this is real in the original, not an artefact
of the alignment experiment; 5b just aborts first (at startup) so 5c is only
reached once 5b is worked around (`logs/run-05-stderr.log`).

codex noted a second consequence: the wrapped `end = base + size` also breaks
relocation *attribution* in `TextRelocations` (`P < end` fails), so relocations in
the mis-sized function's real body are never recorded.

No small safe fix: the size must not depend on final-layout adjacency of a
separate symbol. Directions: compute size from the ELF symbol size / the symbol
table rather than a sentinel function; or place `f` and its table in a dedicated
section (`-ffunction-sections` + a section-per-function contract) so the runtime
can read the real bounds.

## Verdict on the leading hypothesis

REFUTED. `objdump -dr` and `readelf --relocs` on the built binary agree byte for
byte: e.g. final bytes at 0x1087 hold displacement 0x135, `0x1087 + 0x135 =
0x11bc`, matching the retained record `.text + 0x13c` exactly (hardware reaches
0x11c0 after the +4 RIP step). No GOTPCRELX→PC32 / PLT32→PC32 stale-record
mismatch is present. The corruption is entirely from Stabilizer's own header
install (5b), size computation (5c) and reloc-classification (5a). Linker layout
*ordering* is an essential trigger for 5c, but linker *relaxation* is not the
mechanism.

## Reproduction / evidence index (`logs/`)

- `build-01-make.log` — pass+runtime build (base b274f86, clean).
- `run-01-stderr.log` — first repro, raw SIGSEGV in copied `__cxx_global_var_init`.
- `objdump-teardown.log`, `readelf-relocs.log`, `nm-sorted.log` — static evidence,
  same binary. Shows the adjacent-table `.text`+addend relocs and the tight
  0x1080/0x1090 packing.
- `run-02-stderr.log` — `BUG5` debug: `S` external vs target internal (proves 5a).
- `run-04-stderr.log` — after 5a fix: off=14/21/28 now internal; off=7 still
  corrupted by 5b (heap pointer in the field).
- `run-05-stderr.log` — 5a + Align(64): 5b gone, now 5c allocation abort at exit.
- `run-06-stderr.log` — 5a only (alignment reverted): `Text relocation overflow
  off=7` — the exact teardown-README abort.
- `nm-current-unaligned.log` — 5c: dummy (0x1240) before function (0x1560).
- `claim-for-review.md`, `codex-verdict.txt`, `codex-run.log` — adversarial review
  (all three CONFIRMED, relaxation refutation CONFIRMED, 5a 4-byte caveat raised).

## What is committed

- `runtime/Function.cpp`: the 5a fix (target vs symbol containment test), with the
  boundary caveat documented in-code. Correct and safe; does not by itself fix the
  reproducer.
- The `Align(64)` experiment on the pass was tried and **reverted** — it is not a
  safe fix (activates 5c). Recorded here so it is not re-tried blindly.
</content>
