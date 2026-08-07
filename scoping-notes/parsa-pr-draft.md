# Draft PR to parsa/stabilizer — NOT POSTED, awaiting Matthias's approval

Mechanics: `gh pr create --repo parsa/stabilizer --head matthiasgoergens:llvm21-fixes`
(same fork network; fallback if cross-fork base refused: issue with branch link).
Hold posting until the stress agent's RNG harness has MEASURED the
steady-state claim in paragraph 3 (currently: derived + DeepSeek-stepped,
not yet measured). If the first-four-zeros micro-fix commit lands first,
push it to llvm21-fixes so it rides along.

Title: Fix three runtime crashes under sustained re-randomisation (LLVM 21)

Hi @parsa — thanks for the LLVM 21 port; it builds cleanly and the
CodeWindow/near-mapping work for PIE holds up well. Building on it, I
found that sustained runs (long enough for the 500 ms re-randomisation
timer to fire repeatedly) crash in all three randomisation modes, and
this branch fixes the three underlying bugs. With them, tests/libquantum
(input 851 2, ~29 s uninstrumented, ~170 re-randomisation epochs) runs to
completion under -Rcode, -Rstack and -Rheap individually and combined,
with output byte-identical to an uninstrumented build — matching what the
original 2013 code does with period toolchains in an Ubuntu 12.04
container, which I used as the reference oracle throughout.

The first two crashes are one bug in modern DieHard worn twice.
ShuffleHeap::malloc() bypasses its shuffle buffer for requests larger
than MaxSize and calls the super heap directly, but ShuffleHeap::free()
has no matching bypass: it unconditionally swaps the freed pointer into
the bin's shuffle buffer, which malloc never filled for those sizes, so
the swap hands a null to the super heap, which faults reading the size
header at ptr - 8. The data heap hits this on libquantum's first realloc
over 256 bytes (gdb at the fault: reqSz=512, binIndex=6, ptr swapped to
0x0); the code heap hits it on essentially every FunctionLocation free,
since a relocated function nearly always exceeds 256 bytes (gdb:
reqSz=4096, binIndex=9, same swap-to-null). The fix is a small
ShuffleFreeGuard layer that restores the malloc-side bypass for free,
applied to both heap types. Pinning the 2013-era Heap-Layers/DieHard
instead is not an option — your Heap.h adaptation to the new template API
is necessary (the old revisions no longer compile against the port at
all); the adaptation just needed this one symmetry restored.

The third crash is not a port bug: it is a latent bug in the 2013
upstream Util.h, inherited verbatim. getRandomByte()'s refill branch
resets _randCount to sizeof(int) instead of 0, so the read cursor walks
out of the 4-byte buffer and, being a uint8_t, wraps at 256 — in steady
state roughly 4 of every 256 "random" bytes come from the RNG and the
rest are whatever follows the buffer in .bss. It never crashed in 2013
because there was always mapped .bss after the buffer; in this port the
buffer happens to land at the exact end of a much larger .bss, so the
first epoch's stack-pad refresh faults. The one-line fix resets the
cursor to 0. Beyond the crash, this has a consequence worth knowing
about: the stack pads are getRandomByte()'s only consumer, so
stack-randomisation quality in the original tool was substantially
degraded by this bug — relevant to anyone comparing against the paper's
stack-ablation numbers.

Each commit message carries the gdb evidence. Known residuals I'd be
happy to follow up on: the first four getRandomByte() calls still return
zero before the first refill (also true upstream), and gdb
breakpoints/strace interact badly with the int3 trap protocol (both
write 0xCC) — worth a README note. I can also share the verification
setup (the period-container oracle and run logs) if useful.
