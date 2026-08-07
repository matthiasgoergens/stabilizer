# Scoping: is resurrecting Stabilizer the right basis for statistically sound performance evaluation in 2026?

Status: complete, pending adversarial review. Written 2026-08-07. All
inputs in, including both empirical runs (period container; parsa
verification).

Evidence base: `scoping-notes/` in this repo — `runtime-analysis.md` (source
read of the runtime and pass), `fork-survey.md` (all 51 repos, 163 branches,
real diffs), `citation-graph.md` (104 of ~117 citing papers), `llvm-rfc.md`
(the full RFC/PR/lineage record), `alternatives.md` (tools and practice
outside the citation graph), plus raw API dumps backing each. Claims below
cite those files; each of them carries its own explicit coverage statement.

## The answer in three sentences

Nothing maintained does what Stabilizer did, and the parts of it that have no
substitute — stack-frame randomisation and *within-run* re-randomisation, the
property that makes layout noise Gaussian and licenses ANOVA — exist nowhere
else in any form: not in tools, not in follow-up papers, not in anything LLVM
considered and rejected (they never considered it). The gap is real, the need
is documented (criterion.rs has had an open issue asking for exactly this
since 2019), and both empirical checks landed favourably: the **original
runs correctly on 2026 hardware** (period container, full `-Rcode -Rheap
-Rstack`, libquantum correct), and **`parsa/stabilizer` builds clean against
LLVM 21 with PIE working** — its runtime then crashes under sustained
re-randomisation (three precisely characterised logic bugs, see §1), which
is a debugging task with a working oracle, not a thirteen-year port. The
honest framing is therefore not "resurrect a dead project" but "fix three
characterised runtime bugs in an existing LLVM 21 port, then validate the
statistics nobody has ever replicated" — with the substantive remaining
risks being threads, unwinding, and that validation, not API churn.

## 1. The alternatives, evaluated

### lld `--randomize-section-padding` (+ its ecosystem)

Maintained, in-tree, zero setup. But the archaeology (`llvm-rfc.md`)
sharpened what it is and is not:

- It was **never a considered-and-narrowed version of Stabilizer**. Nobody in
  the RFC or PR discussed runtime re-randomisation, stack frames, or heap;
  Stabilizer was mentioned once, by Emery Berger, five months after merge.
  There is no "LLVM rejected the full mechanism" evidence because no full
  mechanism was ever proposed. The closest prior art — Kristof Beyls's
  basic-block-level perturbation at Arm — was reportedly set aside on CI cost,
  not on merit (second-hand, from a talk paraphrase).
- The flag's author (Peter Collingbourne) then built the thing to compare
  against: **`lld/utils/run_benchmark.py`**, in-tree since 2025-05-02. It
  links N variants at N padding seeds, samples them round-robin, interleaves
  A/B, and delegates to hyperfine (which adds per-invocation environment-size
  jitter). His own description: "almost all of what Stabilizer does except
  for heap randomization". His floated LD_PRELOAD heap-jitter extension was
  never implemented.
- Structurally it covers **code layout only, per-build**: no stack frames, no
  heap, no within-run resampling, hence no normality argument — you get
  blocking over K seeds and (if you want parametric statistics) an unverified
  distributional assumption, or you go non-parametric and pay in runs.
- Note `--shuffle-sections` (2020) also exists and *reorders* rather than
  pads; the RFC explicitly argues padding is the more realistic
  counterfactual and reordering is itself a bias source. Both remain in-tree;
  shuffle drifted into a bug-hunting tool (`llvm-rfc.md` §3).

**Verdict**: the right *baseline* and the right *harness skeleton* (task 4
should extend `run_benchmark.py`, not reinvent it), and very possibly "good
enough" for the common A/B case — that is an empirical question our
baseline experiment is designed to answer. Not a substitute for the full
mechanism.

### Coz

Different question (which code to optimise, not whether a change helped).
Maintained, same lab, and confirmed by the citation crawl to be PLASMA's
actual post-2013 direction — a pivot, not a successor. Not the right basis
here. (One paragraph, as the brief requested.)

### The community forks — the survey corrected the five-minute pass

`fork-survey.md`; 51 repos (47 in the fork graph + the detached Dead2
cluster), all 163 branches compared, diffs read for everything ahead.

- **`parsa/stabilizer`** (Parsa Amini, STE||AR Group; fork-of-a-fork,
  invisible to the earlier pass): 26 commits ahead, linear, active
  **2026-02-14**. Claims "tested with LLVM 21". The 2026 commits do real
  runtime engineering on exactly the failure mode our source read predicted
  (`runtime-analysis.md` §5): new `CodeWindow`/`TextRelocations` runtime
  machinery that near-maps the code heap so RIP-relative references survive
  without `-no-pie`. **Verified 2026-08-07** (full record:
  `~/prog/stabilizer-parsa-verify/NOTES.md`, commit `2bffc191c9`, Ubuntu
  clang/opt 21.1.8 in rootless podman): builds with zero patches, pass
  loads as a new-PM plugin, HelloWorld runs under every randomisation mode
  as a default PIE binary. But under sustained execution
  (`tests/libquantum`) every mode crashes before producing output:
  `-Rheap` on the program's first `realloc` (in
  `stabilizer_realloc → ShuffleHeap::free → SizeHeap::getSize`, null-derived
  fault) — before any timer fires; `-Rcode` deterministically on the 2nd
  re-randomisation epoch in `FunctionLocation::sweep()`; `-Rstack` on the
  1st epoch in the stack-pad refresh. These are port logic bugs, not
  toolchain/kernel blocks — the period-container run of the *original*
  passes the identical benchmark with all three modes on, so the mechanism
  is sound, the bugs are localised, and a differential-testing oracle
  exists. "Tested with LLVM 21" is true of the build and a one-shot trivial
  program; false for sustained measurement use.
- **`Dead2/stabilizer`** (Hans Kristian Rosbach; *detached* repo, invisible
  to fork-graph walks): LLVM 12, CMake+Docker+CI, multi-contributor, stale
  since 2023-08. Its README is the most honest status report in the whole
  network: `SZ_HEAP`/`SZ_LINK` work, `SZ_STACK`/`SZ_CODE` crash. That is: an
  independent team already found the heap/link half portable and the
  code/stack half — the load-bearing half — the hard part.
- **`magras/stabilizer:fix-tls`**: the 42-commit LLVM 6→14 lineage
  (fusiled→dendibakh→jgall→magras, CERN-affiliated), with genuine TLS crash
  fixes, feeding into Dead2's line.
- Everything else is trivial. No fork work was ever offered upstream as a PR,
  which is why none of it was discoverable from the upstream repo.

### The field at large

- **No reimplementation anywhere** (Rust, Go, modern C++): both the
  alternatives sweep and the citation crawl came up empty, independently.
- **No paper ever replicated or refuted** Stabilizer's normality claim or
  its O2-vs-O3 headline. Thirteen years, ~117 citations, zero replications
  (`citation-graph.md`). The claim the whole method rests on has never been
  independently tested — that is an opportunity as much as a warning.
- The methodological successors went **statistical instead of mechanical**:
  Kalibera & Jones's effect-size confidence intervals, non-parametric HPT
  (proposed, unmerged, in pyperf). These reduce the *damage* of layout bias;
  they do not remove or sample it.
- Axis-by-axis substitutes: code layout per-build (lld flags), heap
  (DieHard — same lab, **still maintained as of 2026-04**; Scudo;
  hardened_malloc), environment size (hyperfine's Mytkowicz trick, effect
  size unvalidated even by its author). **Stack frames: nothing, anywhere.**
  Within-run re-randomisation: only security-world research artifacts
  (Shuffler, TASR, Morpheus), none usable, all design references.
- ASLR-plus-many-runs randomises four base addresses per run but no relative
  layout, no frame internals, no allocation order — and no head-to-head
  study of ASLR-as-sampler vs Stabilizer exists (`alternatives.md` §1).

## 2. Can the thing even run on 2026 hardware? Yes — verified empirically.

The brief's kernel/CPU risk list mostly dissolved on reading the code
(`runtime-analysis.md`): RWX `mprotect`/`mmap` is legal by default; CET
shadow stack stays dormant for binaries without opt-in property notes; the
relocation jumps are direct so IBT/`endbr64` does not bite; `mmap_min_addr`
is irrelevant. The period-container experiment then **confirmed this
empirically** (2026-08-07, full record in
`~/prog/stabilizer-period/NOTES.md`): LLVM 3.1 + Clang 3.1 built from
release tarballs in an Ubuntu 12.04 rootless-podman container (no distro
clang-3.1 package survives; Heap-Layers/DieHard pinned to 2013 commits —
the unpinned build-time clone in `common.mk` would otherwise fetch 2026
HEAD), and Stabilizer built clean and **ran correctly under full
`-Rcode -Rheap -Rstack`** on this host (Arch, kernel 7.1.5, i9-13900K —
a CET-capable CPU): HelloWorld, a 3 s re-randomisation stress test
(6 epochs/run, 3 runs), and `tests/libquantum` with correct output.
`readelf`/`objdump` confirm the binaries carry no CET markings and no
`endbr64`, so the CPU's CET support is inert for them by the opt-in ABI —
including the `push`+`ret` trampoline a shadow stack exists to catch.
Host-specific result, stated as such: an MDWE-hardened host remains a
plausible blocker elsewhere. Two incidental findings: `strace -f` reliably
SIGILLs the runtime (ptrace vs `int3` self-patching — almost certainly true
in 2013 too, a debugging limitation to document); and `tests/Context` is
Darwin-only (Mach-O yasm), reinforcing "drop Darwin".

Remaining risks are therefore exactly the ones the source read named:
threads (the runtime is structurally single-threaded), unwinding (relocated
code has no `.eh_frame`), and pass/runtime correctness on modern
toolchains. The parsa verification answered the modern-toolchain half: PIE
binaries on current glibc *do* work (HelloWorld, all modes) — the crashes
it found under sustained execution are logic bugs in that port's runtime,
upstream of any kernel/CPU policy. Between the two experiments, every
hardware/OS-level risk in the brief is now retired; what remains is
ordinary (if delicate) systems debugging plus the two structural gaps.

## 3. Recommendation

**Yes — resurrecting Stabilizer is the right basis**, on notably better
terms than the brief assumed, and via `parsa/stabilizer` rather than a
fresh port. The empirical results resolved the open branches as follows:
the mechanism runs on 2026 hardware (period container); a clean LLVM 21
build with working PIE already exists (parsa); and the distance between
"builds" and "usable" is three precisely characterised runtime crashes
with reproducers, a deterministic trigger, and a working reference
implementation to differential-test against. Concretely:

1. **The baseline experiment (task 4) is worth doing regardless, and should
   be built on `lld/utils/run_benchmark.py` + the original repo's factorial
   design** (`link` vs `code`/`stack`/`heap` configs, `process.py`'s
   normality machinery as specification). It answers the question the LLD
   flag's own authors never asked: how much of the layout variance does
   per-build padding capture, and are its samples Gaussian?
   Add a **DieHard arm**: `LD_PRELOAD`ing DieHard (maintained, same lab —
   and mechanically the same idea as Stabilizer's own ShuffleHeap) closes
   exactly the heap gap Collingbourne named and never built, for the cost of
   one environment variable and no rebuild. It mirrors the paper's `heap`
   config, keeping results comparable to the published ablation. DieHard is
   a *component* here, not a candidate foundation: it has no pass and no
   code/stack machinery, so it cannot carry the axes that have no
   substitute. Caveat to state in `BASELINE.md`: its ~2× heap expansion and
   allocation overhead mean that arm measures layout variance *under
   DieHard*, not under glibc malloc — fine for A/B, but a different
   allocator regime.
2. **Fix the parsa runtime, using the original as oracle.** Three
   characterised bugs, in rough order of attack: the `-Rheap` `realloc`
   crash (likely the easiest — the original passes the same test, so the
   defect is in the port's heap-wrapper changes; note `libstabilizer.cpp`'s
   `stabilizer_free` dispatches on `getSize(p) == 0`, a fragile
   own-heap-vs-foreign-pointer test that the port's rework may have
   broken); the `-Rstack` first-epoch crash in the pad refresh; the
   `-Rcode` second-epoch crash in `FunctionLocation::sweep()` — the
   mark/sweep path `runtime-analysis.md` flagged as highest-risk, and the
   same region where Dead2's effort stalled. Reproducers and crash logs:
   `~/prog/stabilizer-parsa-verify/`. Engage Parsa Amini (active three
   weeks of commits in Feb 2026) once there is something concrete to
   offer — subject to the house rule that nothing is posted without
   Matthias approving the text.
3. **Drop Darwin and PPC** (Context test is Mach-O-only anyway), rewrite
   the Python 2 tooling trivially, and treat the **normality replication
   as the first experiment any revived Stabilizer runs** — nobody has
   checked it in thirteen years, and it is the claim everything else
   stands on.

## Stretch goal: upstreaming into LLVM proper

The archaeology says the door is open but narrow. Nobody has ever proposed a
Stabilizer-like facility in-tree, so it has never been rejected; the padding
flag's author is actively interested in measurement bias (the RFC,
`run_benchmark.py`, Google-internal `-falign-functions=32`), and Berger
himself surfaced in the thread. A realistic ladder, easiest first:

1. **The `LD_PRELOAD` heap-jitter library pcc floated and never built**
   (RFC post 9). Small, explicitly wanted, and our DieHard-arm baseline
   numbers would be the motivating evidence.
2. **Generalise `lld/utils/run_benchmark.py`** beyond benchmarking lld
   itself — pcc wished for exactly this in the same post.
3. **The full runtime facility**, only with `BASELINE.md`-grade evidence
   that per-build padding is insufficient. Expect the in-tree objections to
   be CI cost (what shelved Kristof Beyls's basic-block perturbation at Arm)
   and RWX/CET hygiene (the runtime needs writable+executable pages and
   `ret`-based dispatch, which fights every hardening trend), not novelty.

## Languages, and the Rust question

Everything substantive is C++: the pass (947 lines, LLVM C++ API), the
runtime (~1,100 lines, Heap Layers templates, machine-code-emitting structs
in `Jump.h`/`Trap.h`); `szc`/`run.py`/`process.py` are Python 2 (rewrite,
trivially). A Rust port splits cleanly:

- **Pass: stays C++.** Rust LLVM bindings wrap the narrower C API, lag
  releases, and would foreclose upstreaming. Not worth it.
- **Runtime: a plausible later candidate.** Small, standalone, C-ABI
  boundary (trivial from Rust). The core operations (live code patching,
  signal-context rewriting, allocation in handlers) stay `unsafe` either
  way, so safety gains land mostly in the bookkeeping — but Rust's
  concurrency discipline is a real asset for the thread-safety redesign,
  which is the genuinely new engineering a revival needs. Tension to
  resolve first: a Rust runtime conflicts with the LLVM-proper stretch goal
  (compiler-rt is C/C++). Decide the North Star before rewriting.

## What was not controlled / coverage gaps

Each scoping note carries its own explicit coverage statement; the material
ones: Dead2's README claims were not verified by building (parsa's were —
and partially refuted, which suggests treating Dead2's "SZ_HEAP works" with
the same scepticism); one broad 162-hit GitHub query for further detached
copies was not triaged; Semantic Scholar was rate-limited throughout
(OpenAlex coverage ≈ 89% of citations); pre-2020 llvm-dev mailing lists were
not searched; Kristof Beyls's talks are cited second-hand; the
period-container result is host-specific (an MDWE-hardened host could still
block RWX pages); and neither experiment tested threaded programs, C++
exceptions through relocated frames, or any workload beyond the repo's own
tests.
