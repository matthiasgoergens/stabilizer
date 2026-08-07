# Scoping: is resurrecting Stabilizer the right basis for statistically sound performance evaluation in 2026?

Status: adversarially reviewed 2026-08-07 and revised in response — the
review (in-family refutation agent) found the first draft overclaimed its
two load-bearing inferences, and this version corrects them. The first
verification experiment it demanded has since landed and passed (original
survives ~173 re-randomisation epochs on a real benchmark, all modes, §2);
the second resolved the Heap-Layers confound and yielded the first fix —
the `-Rheap` crash is root-caused and repaired (§1). A cross-model pass
(DeepSeek, reasoning model) returned CONFIRMED against this revision
(`scoping-notes/deepseek-verdict.txt`); a codex pass is queued behind
quota as a second family. The recommendation remains conditional on the
task-4 baseline either way.

Evidence base: `scoping-notes/` in this repo — `runtime-analysis.md` (source
read of the runtime and pass), `fork-survey.md` (51 repos, all 163 branches
compared by ahead/behind; diffstats plus commit messages for the 15 ahead
branches, full patches spot-read for two), `citation-graph.md` (104 of ~117
citing papers, title-and-abstract triage), `llvm-rfc.md` (the full
RFC/PR/lineage record), `alternatives.md` (tools and practice outside the
citation graph), plus raw API dumps backing each. Claims below cite those
files; each carries its own explicit coverage statement, and this summary
tries not to claim more than those statements support.

## The answer in three sentences

Nothing maintained does what Stabilizer did: no tool anywhere combines
code, stack and heap randomisation with *within-run* re-randomisation, and
the nearest partial forms are DieHard's per-malloc shuffling (heap only,
maintained) and security-world re-randomisers (unusable research
artifacts); LLVM never rejected such a facility — nobody ever proposed one.
Both empirical checks were encouraging but bounded: the **original builds
and runs on 2026 hardware, and survives sustained re-randomisation on a
real benchmark** (period container: `libquantum 851 2`, ~173 epochs per
run, each mode separately and all combined, output byte-identical to the
uninstrumented oracle), and **`parsa/stabilizer` builds clean against
LLVM 21 with PIE working**, then crashes at three sites under sustained
execution — a
lower bound from one benchmark, with the crash sites located but not yet
root-caused, a possible Heap-Layers-version confound not yet excluded, and
at least one further known bug class (TLS, fixed in the magras lineage,
absent from parsa's ancestry) untriggered by that benchmark. The
recommendation is therefore **conditional, per the brief's own gate**: run
the baseline experiment (task 4) to learn whether per-build padding plus
heap randomisation is good enough, while in parallel probing the port's
tractability by fixing the first crash — and commit to the full
resurrection only if the baseline shows the cheap route insufficient and
the probe shows the port sound.

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
  (`tests/libquantum 851 2`) every mode crashes before producing output:
  `-Rheap` on the program's first `realloc` (in
  `stabilizer_realloc → ShuffleHeap::free → SizeHeap::getSize`, null-derived
  fault) — before any timer fires; `-Rcode` deterministically on the 2nd
  re-randomisation epoch in `FunctionLocation::sweep()`; `-Rstack` on the
  1st epoch in the stack-pad refresh (crash site logged, not root-caused).
  Precision about what this does and does not establish: these were
  **crash sites, not diagnosed defects** when found; the count is a
  **lower bound** from one single-threaded C benchmark at one input (a
  further known bug class — TLS access via the relocation table, fixed in
  the magras lineage — is absent from parsa's ancestry and untriggered by
  libquantum). **The `-Rheap` crash has since been root-caused and
  fixed** (2026-08-07, `~/prog/stabilizer-parsa-fix/NOTES.md`, fix commit
  `f9ed534` in the local clone): modern DieHard's `ShuffleHeap::malloc()`
  bypasses its shuffle buffer for objects over `MaxSize` (256 B here) but
  `free()` lacks the matching bypass, so freeing any larger object swaps
  a null out of a never-filled slot — gdb-confirmed. The Heap-Layers
  confound resolved instructively: the *unmodified* port does not even
  compile against 2013-pinned dependencies (template-arity mismatch), so
  parsa's heap adaptation was necessary and carried exactly one asymmetry
  bug, now guarded. Post-fix: `-Rheap` libquantum survives ~150 epochs
  across 4 runs with byte-identical output; the combined mode advances to
  the known `-Rcode` epoch-2 crash; `-Rstack`/`-Rcode` behaviour
  unchanged. Two crashes remain: `-Rstack` epoch-1 (site known, cause
  unknown) and `-Rcode` epoch-2 in `FunctionLocation::sweep()`.
  The oracle gap the adversarial review exposed (the original's first
  libquantum run, at input 128, finished before any epoch fired) has since
  been closed: on `libquantum 851 2` the **original survives ~173 epochs
  per run in every mode**, separately and combined, output identical to
  uninstrumented (`~/prog/stabilizer-period/NOTES.md`,
  `libquantum-851-results/`). Every parsa crash is therefore a regression
  relative to a demonstrably working design on the same kernel — in the
  port's own changes or its unpinned dependencies. One design nuance
  recorded there: `-Rheap` is not epoch-gated at all (ShuffleHeap
  randomises at every `malloc`), so its correctness evidence is the clean
  output under sustained shuffled allocation, not a timer count. "Tested
  with LLVM 21" is true of the build and a one-shot trivial program;
  currently false for sustained measurement use, where every mode fails.
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
- **No replication or refutation found** of Stabilizer's normality claim or
  its O2-vs-O3 headline — by title-and-abstract triage of 104 of ~117
  citing papers (`citation-graph.md`; a replication buried in a paper's
  evaluation section would evade this method). The claim the whole method
  rests on appears never to have been independently tested — an
  opportunity as much as a warning. Honest corollary the first draft of
  this document dodged: thirteen years of nobody building or even
  formally asking for these properties is also evidence about demand, and
  the normality argument itself carries less weight in 2026 than in 2013 —
  bootstrap/permutation/HPT methods make parametric licensing largely
  optional at benchmark-scale n, so the case for within-run
  re-randomisation must rest on variance-efficiency per run and on the
  stack/heap axes, not on "licenses ANOVA" alone.
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

## 2. Can the thing even run on 2026 hardware? On this host, yes — verified.

The brief's kernel/CPU risk list mostly dissolved on reading the code
(`runtime-analysis.md`): RWX `mprotect`/`mmap` is legal by default; CET
shadow stack stays dormant for binaries without opt-in property notes; the
relocation jumps are direct so IBT/`endbr64` does not bite; `mmap_min_addr`
is irrelevant. The period-container experiment then **confirmed this
empirically** (2026-08-07, full record in
`~/prog/stabilizer-period/NOTES.md`): LLVM 3.1 + Clang 3.1 built from
release tarballs in an Ubuntu 12.04 rootless-podman container (no distro
clang-3.1 package ever existed in that pool; Heap-Layers/DieHard pinned to
2013 commits — the unpinned build-time clone in `common.mk` would
otherwise fetch 2026 HEAD), and Stabilizer built clean and **ran correctly
under full `-Rcode -Rheap -Rstack`** on this host (Arch, kernel 7.1.5,
i9-13900K — a CET-capable CPU): HelloWorld, a 3 s re-randomisation stress
test (6 epochs/run, 3 runs), and `tests/libquantum`. The adversarial
review caught that the first libquantum run (input 128) completed before
any epoch fired; the follow-up closed the gap decisively: **`libquantum
851 2` under `-Rcode`, `-Rstack`, `-Rheap` and all three combined — ~173
epochs per run — exit 0 every time, output byte-identical to the
uninstrumented oracle**. One number to treat with care: the instrumented
runs took ~85-90 s against the oracle's 36 s. That is NOT a valid
overhead measurement (debug-instrumented builds, untuned config, one
benchmark) and must not be quoted against the paper's "<7% median" —
but it flags overhead measurement as a first-class task for
`BASELINE.md`.
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
toolchains. The parsa verification answered part of the modern-toolchain
question: PIE binaries on current glibc *do* work (HelloWorld, all
modes) — the sustained-execution crashes are upstream of any kernel/CPU
policy. Scope this correctly, though: **no hardware/OS-level risk
materialised on this one permissive host**, for binaries that opt into no
modern protections (non-PIE or unmarked, no CET notes — and no CET check
was recorded for the parsa binaries). Both experiments share one kernel
and one machine; an MDWE-hardened host, a locked-down container runtime,
or a toolchain that turns on `-fcf-protection` by default (as hardened
distros do) would each reopen the question. "Retired on this host, with
these binaries" is the supportable claim.

## 3. Recommendation

**Conditional yes — pursue the resurrection via `parsa/stabilizer`, but
gate the commitment on the baseline experiment, exactly as the brief
demands** ("only if tasks 1–4 justify it"). What the scoping established:
the mechanism runs on 2026 hardware (on this host, for unhardened
binaries); a clean LLVM 21 build with working PIE already exists; and the
known distance between "builds" and "usable" is three located crash
sites — a lower bound, not an estimate, from one benchmark, with a
Heap-Layers-version confound still open and Dead2's year of stalled
multi-contributor effort as the cautionary prior for the code/stack half.
What would change the answer to "no": the baseline experiment showing
per-build padding + DieHard captures most of the layout variance for
realistic workloads, or the probe fixes revealing structural rather than
localised defects. Run both prongs in parallel; neither blocks the other:

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
2. **Probe the parsa runtime's tractability, using the original as a
   partial oracle.** Status: the probe's first rung is already cleared —
   the `-Rheap` crash is root-caused and fixed (see §1; DieHard
   `ShuffleHeap` malloc/free bypass asymmetry, guard layer added, ~150
   epochs verified). The version-skew question is closed: 2013-pinned
   dependencies do not compile against the port at all, so adaptation was
   forced and the port's composition was sound apart from this one bug.
   Remaining rungs: the `-Rstack` first-epoch crash (site known, cause
   unknown), then the `-Rcode` second-epoch crash in
   `FunctionLocation::sweep()` — the path most exercised by sustained
   re-randomisation, and plausibly related to wherever Dead2's `SZ_CODE`
   crashes lived (their README records *that* it crashed, not where —
   inference, not fact). Budget-limit these: if they resist diagnosis,
   that is itself the answer. Remember the TLS bug class (magras
   `4e154b8f`) awaits any workload using thread-local storage.
   Reproducers, crash logs and the fix: `~/prog/stabilizer-parsa-verify/`
   and `~/prog/stabilizer-parsa-fix/`. Engage Parsa Amini (a two-day
   commit burst in Feb 2026, prior burst April 2023 — an intermittent
   solo effort, not an active team) once there is something concrete to
   offer — one working fix already qualifies — subject to the house rule
   that nothing is posted without Matthias approving the text.
3. **Drop Darwin and PPC** (Context test is Mach-O-only anyway), rewrite
   the Python 2 tooling trivially, and treat the **normality replication
   as the first experiment any revived Stabilizer runs** — no independent
   test of it was found (see §1's epistemic caveat), and it is the claim
   everything else stands on. If normality fails to replicate, the
   remaining case for within-run re-randomisation is variance efficiency
   and the stack/heap axes — decide then whether that is enough.

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
block RWX pages); the parsa binaries' CET/property-note
status was not recorded; and neither experiment tested threaded programs,
C++ exceptions through relocated frames, or any workload beyond the repo's
own tests. This document was adversarially reviewed once (in-family); the
cross-model pass is pending quota.
