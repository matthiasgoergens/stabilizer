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

### Phase 1 — minimal single-threaded Rust, end to end (SPIKE, start now)
Goal: one trivial single-threaded Rust program, `panic = "abort"`,
re-randomising correctly under all three modes with output matching an
uninstrumented build. Deliberately dodges the two hardest axes (threads,
unwinding) to surface the *integration* obstacles concretely:
- rustc `--emit=llvm-bc` (or `-Cembed-bitcode`) into the `szc`
  bitcode→`opt`→link path the driver already uses for Flang.
- `#[global_allocator]` shim forwarding to the Stabilizer heap (or accept
  no heap-randomisation for the spike and confirm code/stack work).
- Entry point: reconcile the runtime's `main` hijack with Rust's
  `lang_start`. Likely `#![no_main]` + a C `main` shim, or a
  `#[no_mangle] extern "C" fn main` experiment.
This is a spike: its output is a list of the real obstacles + a working
minimal case, not production support. Expect surprises; record them.

### Phase 2 — threads (the load-bearing hard part)
The runtime is structurally single-threaded: unlocked global registries,
`ITIMER_REAL` delivered to an arbitrary thread, one `topFrame`, one stack
walked at mark/sweep. Real Rust (and `std` itself) is threaded, so this
is the wall. **This is where the prior-art rule fires**: concurrent
in-process code re-randomisation is exactly what Shuffler (OSDI'16),
TASR, and Morpheus solved for the security threat model. Design the
concurrency model *drawing on them, in parallel with building* — do not
reinvent async-safe code migration from scratch. Sub-problems:
- Locking / lock-free access to the function & location registries.
- Relocating code a *sibling thread* may be executing (the deep one —
  Shuffler's core contribution).
- Per-thread stack walking for the mark phase; per-thread trap state.
- Signal/timer strategy across threads.

### Phase 3 — unwinding, TLS, hardened targets
- `.eh_frame` for relocated code (so Rust panics / C++ exceptions unwind
  through moved frames), or a documented `panic=abort` restriction as an
  interim.
- Port the TLS-via-relocation-table fix (magras `4e154b8f`, absent from
  parsa's lineage) and verify with a `__thread` / Rust thread-local case.
- CET/`-fcf-protection` and MDWE-hardened targets: measure what breaks,
  decide adapt-vs-document.

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
