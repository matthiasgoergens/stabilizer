# Roadmap: finish the port, north star = Stabilizer works for Rust

Decision (Matthias, 2026-08-08): don't stop at "preserved, working,
documented". **Actively complete the port so Stabilizer works on modern
real-world code, with Rust support as an explicit goal.** This overrides
the scoping document's deflationary lean (SCOPING.md §0) — that lean was
the honest read *if the aim were only "is the cheap route good enough for
benchmarking"*; the aim is now the ambitious one, eyes open to the cost.

Why Rust is the right forcing function: it exercises every open axis of
the port debt at once (threads, unwinding, TLS, custom allocator, entry
point — SCOPING.md "Language scope"). A port that runs real Rust is, by
construction, a port that handles modern C and C++ too. So "make it work
for Rust" is not a niche add-on; it is the most demanding single target
that drags all the debt along with it.

## Sequencing (easiest-informative first, hardest last)

Ordered so each phase de-risks the next and produces a working artefact,
per "smoke-test end to end before scaling up" and "try the naive thing
first".

### Phase 1 — minimal single-threaded Rust, end to end (SPIKE — DONE 2026-08-08)
**Result: succeeded.** A trivial single-threaded Rust program
(`panic=abort`, `#[inline(never)]` on the hot fn) runs correctly under
`-Rcode`, `-Rstack`, `-Rheap` and all combined — verified against an
arithmetic invariant and the uninstrumented oracle across 7 runs, with
real re-randomisation events firing. Repo:
`~/prog/stabilizer-rust-spike/` (commit `61a6d66`, NOTES.md has the full
ladder). Confirms the mechanism is frontend-agnostic in practice, not
just in principle. Estimate for a real Rust frontend: **days, not weeks**
— the durable cost is toolchain bookkeeping and formalising the
linker-substitution trick, engineering not research. Concrete obstacles
found (all worked around within budget):
- **Bitcode version skew.** rustc stable 1.97.1 ships LLVM 22.1.6 vs the
  container's `opt` 21.1.8; raw bitcode → `Unknown attribute kind (105)`,
  textual-IR fallback → `unterminated attribute group`
  (`nocreateundeforpoison`, an LLVM-22 keyword). Bisected to rustc 1.93.0
  = exact LLVM 21.1.8 match, but that pin is a ~18-week cadence
  coincidence, not a guarantee. **A real Rust frontend must match
  rustc's LLVM to the pass's LLVM** (or build the pass against rustc's
  LLVM).
- **Linking gap.** `--emit=llvm-bc` exposes only the user crate's CGU;
  rustc's allocator-shim CGU and all of `std`/`core`/`alloc`'s
  precompiled rlib internals are invisible to the bitcode pipeline. Spike
  worked around it with a `-Clinker=` shim that captures rustc's real
  link line and swaps in the Stabilizer-transformed object, leaving std
  unrandomised — deliberate scope narrowing; whole-program Rust needs std
  built as bitcode (Phase 4).
- Open, not root-caused (no misbehaviour observed): `TextRelocations`
  logs "0 supported relocations" even when one was engineered; `readelf`
  corroborates zero in range, so it may be correct (the pass's IR-level
  table already covers this all-integer test). Next test: add an FP
  literal / jump table.

### Phase 2 — threads (the load-bearing hard part) — DESIGN DONE 2026-08-08
The runtime is structurally single-threaded: unlocked global registries,
`ITIMER_REAL` delivered to an arbitrary thread, one `topFrame`, one stack
walked at mark/sweep. Real Rust (and `std` itself) is threaded, so this
is the wall. Prior-art design delivered:
`~/prog/stabilizer-threads-design/DESIGN.md` (627 lines, Shuffler OSDI'16
+ TASR CCS'15 PDFs saved). Recommended protocol: **per-thread `topFrame`**
(fixes the single global anchor at `libstabilizer.cpp:38`); route the
timer through **one dedicated maintenance thread** that signals every
live thread to unwind and mark its own stack; **free a FunctionLocation
only after a barrier** confirms all threads reported (Shuffler §3.2/§4).
The in-place jump/trap entry rewrite needs **an atomic single-word
indirection, not a lock** — the hazard is a concurrent instruction
*fetch*, which no mutex touches (Shuffler + TASR converge on this).
**Biggest risk (from the design): a thread blocked in a long syscall or
spinning in uninstrumented code during an epoch** — Shuffler's paper
never addresses it; naive stop-the-world hangs the barrier, and exempting
unresponsive threads reintroduces the use-after-free. Fallback:
livepatch's per-task lazy graduation. Build that falsifying experiment
first. (Citation fix: Morpheus is ASPLOS 2019, not ISCA; Torrellas is not
an author.)

### Phase 3 — unwinding, TLS, hardened targets
- `.eh_frame` for relocated code (so Rust panics / C++ exceptions unwind
  through moved frames), or a documented `panic=abort` restriction as an
  interim.
- Port the TLS-via-relocation-table fix (magras `4e154b8f`, absent from
  parsa's lineage) and verify with a `__thread` / Rust thread-local case.
  (Note: parsa's `applyTextRelocs` already handles TLS *relocation types*
  in the code path — GOTTPOFF/TLSGD/TLSLD etc. — so the code-relocation
  side may be partly covered; the magras fix is about the stack-pad
  relocation table, a different path. Confirm which gaps remain.)
- CET/`-fcf-protection` and MDWE-hardened targets: measure what breaks,
  decide adapt-vs-document.
- Also fold in the two hardening items surfaced by cross-model review of
  the fixes: `setHandler`'s uninitialised `sigaction.sa_mask`
  (pre-existing), and `getRandomByte`'s **per-translation-unit static
  state** — each TU gets its own `_rng`, so the runtime runs several
  independent RNG sequences (a real randomisation-quality issue, not just
  a threading one; DeepSeek code review, 2026-08-08). Consider a single
  shared, locked RNG when threads land.

### Phase 4 — real Rust + productionising
Full `std` Rust (threads, panics, TLS), a `#[global_allocator]` crate for
heap randomisation, and a Cargo integration story. Then Rust workloads
join the diverse-workload baseline (SCOPING.md §3.1a) as first-class arms.

## Standing constraints
- All build/run of instrumented binaries in rootless podman; never on the
  host kernel.
- Each phase: a differential oracle (uninstrumented build, and where
  possible the period original) + adversarial/cross-model review before it
  is called done. A positive control I designed myself is close to no
  evidence.
- Prior art searched in parallel with building, not after — especially
  Phase 2.
- Commit per working increment; push to `matthiasgoergens/stabilizer`;
  nothing to `parsa/` upstream without Matthias approving text.
