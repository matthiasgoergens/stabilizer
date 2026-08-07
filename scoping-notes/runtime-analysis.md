# Stabilizer runtime: mechanism and 2026 viability, from reading the source

Read 2026-08-07, at commit eadbd5d. Files: `runtime/*` (~1 100 lines total),
`pass/Stabilizer.cpp` (947 lines), `pass/LowerIntrinsics.cpp` (61 lines),
`common.mk`, `szc`. This note grounds the port-risk section of `SCOPING.md` in
the actual mechanism rather than the brief's summary of it.

## How it actually works

The pass renames the program's `main` to `stabilizer_main`; the runtime
provides its own `main` (`libstabilizer.cpp:50`). Module constructors register
every randomisable function with the runtime.

**Code randomisation** is lazy, trap-driven:

- `Function`'s constructor (`Function.h:93-109`) `mprotect`s the function's
  text pages to `PROT_READ|WRITE|EXEC` and overwrites the function's first
  bytes in place with a `FunctionHeader` — a union of a jump/trap slot plus a
  `Function*` back-pointer. The original bytes are saved and patched back into
  every relocated copy (`Function.cpp:23-50`).
- At startup and at each epoch, the entry gets an `int3` (0xCC, `Trap.h`). On
  SIGTRAP the handler (`onTrap`, `libstabilizer.cpp:125`) reads the
  `Function*` stored next to the trap, copies the function body to a fresh
  allocation from the code heap, rewrites the header as a jump to the copy,
  and resumes by rewriting the signal-context IP. So only functions that
  actually get called are (re)located — hot functions re-randomise every
  epoch, cold ones never move.
- A 500 ms `ITIMER_REAL` (`onTimer`) re-arms traps on live functions and sets
  `rerandomizing`; the next trap does a mark/sweep of old code locations by
  **walking the frame-pointer chain** from the trap context up to the saved
  `topFrame` (`libstabilizer.cpp:141-154`).
- The code heap is a 256-way `ShuffleHeap` (DieHard-style) over
  `PROT_READ|WRITE|EXEC`, `MAP_32BIT` anonymous mmap chunks of 32 MiB
  (`Heap.h:16-19`), falling back to unrestricted addresses when the low 4 GiB
  are exhausted (`MMapSource.h`).
- Forwarding jumps: within ±4 GiB a 5-byte `jmp rel32`; beyond that a
  push-target-and-`ret` sequence (`X86Jump64`, `Jump.h:21-42`).

**Stack randomisation**: before each callsite the pass loads a global 1-byte
pad, multiplies it by the stack alignment, and subtracts that from the stack
pointer (`Stabilizer.cpp:340-386`) — each callee's frame shifts by 0-255
alignment units. The pad byte is re-drawn once per relocation epoch
(`Function.cpp:63-66`). Side effect worth knowing: the dynamic stack
adjustment itself forces frame pointers, which is what makes the mark-phase
FP walk viable. (No explicit `-fno-omit-frame-pointer` anywhere in `szc` or
the makefiles — the guarantee is structural, and a port must preserve it or
switch to `.eh_frame` unwinding.)

**Heap randomisation**: the pass redirects `malloc`/`calloc`/`realloc`/`free`
to `stabilizer_*` wrappers over a 256-way `ShuffleHeap` on private mmap
(`libstabilizer.cpp:100-118`, `Heap.h:22-26`). This is the same shuffling idea
as DieHard, built from **Heap Layers**.

## Build-time trap discovered

`common.mk:112-115` clones `https://github.com/emeryberger/Heap-Layers.git`
**unpinned, at build time**. A 2026 HEAD of Heap-Layers will not compile under
GCC 4.6, and `shuffleheap.h`/`<heaplayers>` may no longer exist there.
Period-correct builds must pin a ~2013-05 Heap-Layers commit. (Container agent
notified.)

## What a 2026 kernel/CPU actually blocks — assessed against the code

The brief lists W^X, CET/IBT, `mmap_min_addr`, ASLR and seccomp as risks.
Reading the code narrows that considerably:

1. **RWX pages: not blocked by default.** Both the `mprotect(RWX)` of the
   binary's own text and the RWX anonymous mmap code heap are legal on a
   default Linux kernel in 2026. They fail only under opt-in hardening:
   `PR_SET_MDWE` / systemd `MemoryDenyWriteExecute`, SELinux `execmem`, or
   PaX-style kernels. A plain podman container on this machine imposes none of
   those. *Verify empirically, but the expectation is: runs.*
2. **CET shadow stack: dormant for these binaries.** The `X86Jump64`
   push-and-`ret` forwarding would violate SHSTK — but the kernel/glibc only
   enable SHSTK for binaries whose GNU property notes opt in, and nothing
   built by GCC 4.6/LLVM 3.1 (or by a port that omits `-fcf-protection`)
   carries those notes. A hard incompatibility only if someone insists on
   CET-enabled builds; otherwise a non-issue.
3. **IBT/`endbr64`: non-issue.** Relocated entries are reached by *direct*
   jumps (no `endbr64` needed), the `ret`-trick is a return (SHSTK's problem,
   not IBT's), and Linux does not enforce user-space IBT by default anyway.
4. **`vm.mmap_min_addr`: non-issue.** `MAP_32BIT` without `MAP_FIXED` never
   allocates below the floor.
5. **ASLR/PIE: a performance and correctness wrinkle, not a blocker.** With a
   period non-PIE binary (text at 0x400000) plus `MAP_32BIT`, forwarding is
   the cheap `jmp rel32`. A modern PIE binary sits far above 4 GiB, so every
   forwarded call would take the push+`ret` path — which defeats the
   return-stack-buffer predictor on every call, i.e. the *instrument* would
   perturb the thing measured. A port wants near-text trampoline allocation
   instead. Latent bug while here: the `X86Jump32` range check
   (`Jump.h:51`) accepts distances up to 4 GiB, but `jmp rel32` is signed
   ±2 GiB — offsets between 2 and 4 GiB mistarget.

So the "can it run at all on 2026 hardware" question is likely **yes, by
default** — the genuine kernel/CPU blockers are all opt-in hardening. The
container experiment should confirm this cheaply.

## The real port risks, from the code (not kernel policy)

- **Threads.** The runtime is designed single-threaded: unlocked global
  `set`s, `ITIMER_REAL` delivered to an arbitrary thread, one global
  `topFrame`, mark/sweep of one stack. Multi-threaded programs are outside
  the design, and most 2026 benchmark targets are threaded. This is new
  engineering, not porting.
- **Unwinding/debug info.** Relocated copies carry no `.eh_frame`
  registration: C++ exceptions through relocated frames, profilers, and
  debuggers all see unknown code. Fine for the paper's C benchmarks; a real
  limitation for modern use.
- **Signal-handler discipline.** Handlers allocate (their own heap) and
  mutate global state; `ITIMER_REAL` + lazy traps is inherently racy under
  any concurrency (see above).
- **The pass is a rewrite, not a port.** 947 lines against LLVM 3.1 (typed
  pointers, legacy PM, `LowerIntrinsics` for era intrinsics). Against LLVM
  22 this is a fresh pass implementing the same transformation — tractable
  (the transformation is well-specified by this code) but not mechanical.
- **Code maturity signals.** `libstabilizer.cpp:138` calls
  `live_functions.empty()` where `.clear()` was clearly intended (benign
  no-op — the set is cleared in `onTimer` — but telling); the `Jump.h` range
  bug above; PPC support already half-commented-out.

## Sizing

The whole system is ~2 100 lines (runtime ~1 100, pass ~1 000). The mechanism
is compact and fully comprehensible in an afternoon. The port cost is not in
the line count — it is in re-validating the trap/relocate machinery against
modern glibc/PIE and deciding what to do about threads.
