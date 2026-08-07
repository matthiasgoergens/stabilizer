# Scoping: is resurrecting Stabilizer the right basis for statistically sound performance evaluation in 2026?

Status: DRAFT — two empirical inputs still pending (marked ⏳ below): the
period-container run of the original, and the build-verification of
`parsa/stabilizer`. Everything else is final. Written 2026-08-07.

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
since 2019), and the thirteen-year port is smaller than it looks — ~2,100
lines total, with community forks having already carried the pass to LLVM 12
(Dead2, heap/link working, code/stack crashing) and, if its README is
truthful, to LLVM 21 with the PIE problem solved (`parsa/stabilizer`,
verification ⏳). The honest framing is therefore not "resurrect a dead
project" but "finish and validate a resurrection that is already most of the
way through the mechanical part" — with the substantive risks being threads,
unwinding, and the unvalidated statistics, not API churn.

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
  without `-no-pie`. ⏳ Build verification running; this claim decides the
  recommendation's shape.
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

## 2. Can the thing even run on 2026 hardware? (source read + ⏳ empirical)

The brief's kernel/CPU risk list mostly dissolved on reading the code
(`runtime-analysis.md`): RWX `mprotect`/`mmap` is legal by default; CET
shadow stack stays dormant for binaries without opt-in property notes; the
relocation jumps are direct so IBT/`endbr64` does not bite; `mmap_min_addr`
is irrelevant. Expectation: the original runs in a default container, and
the real risks are elsewhere — threads (the runtime is structurally
single-threaded), unwinding (relocated code has no `.eh_frame`), and the
pass rewrite. ⏳ The period-container run will confirm or refute the
"runs by default" expectation; the parsa verification tests the same
question on a modern toolchain.

## 3. Recommendation

⏳ Held open pending the two empirical results, but the shape is already
determined by what is above:

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
2. **If `parsa/stabilizer` builds and survives re-randomisation** on LLVM 21,
   the port question mostly disappears: the work becomes validate, fix
   residuals (threads? unwinding?), and run the three-way comparison with it.
3. **If it does not**, the Dead2 experience (code/stack crashing on modern
   LLVM despite serious effort) plus whatever the container run shows becomes
   the honest cost estimate for reviving the load-bearing half — and the
   deflationary outcome ("the LLD flag plus DieHard-style heap randomisation
   captures most of what matters; within-run normality remains theoretically
   attractive but nothing delivers it") is a publishable, useful finding.
4. Either way: **drop Darwin and PPC**, rewrite the Python 2 tooling
   trivially, and treat the normality replication as the first experiment any
   revived Stabilizer runs, because nobody has ever checked it.

## What was not controlled / coverage gaps

Each scoping note carries its own explicit coverage statement; the material
ones: the fork survey did not build anything (the parsa/Dead2 READMEs are
authors' claims — parsa's now under test); one broad 162-hit GitHub query for
further detached copies was not triaged; Semantic Scholar was rate-limited
throughout (OpenAlex coverage ≈ 89% of citations); pre-2020 llvm-dev mailing
lists were not searched; Kristof Beyls's talks are cited second-hand.
