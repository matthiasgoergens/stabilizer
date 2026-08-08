# Scoping: is resurrecting Stabilizer the right basis for statistically sound performance evaluation in 2026?

Status: adversarially reviewed 2026-08-07 and revised in response — the
review (in-family refutation agent) found the first draft overclaimed its
two load-bearing inferences, and this version corrects them. The first
verification experiment it demanded has since landed and passed (original
survives ~173 re-randomisation epochs on a real benchmark, all modes, §2);
the second resolved the Heap-Layers confound and yielded the fixes (§1).
Cross-model history, honestly scoped: a DeepSeek pass returned CONFIRMED
against the 2026-08-07 revision (`deepseek-verdict.txt`) — that verdict
**predates the port fixes and stress results and does not confirm the
present text**; a codex pass on 2026-08-08 returned **WEAKENED**
(`codex-scoping-verdict.txt`), and this revision is the response to it:
stale contradictory prose removed, the reversed bzip2 preliminary and
the ~2× overhead disclosed in §0/§3, coverage claims re-scoped to what
the notes' own statements support, the gate pre-registered (§3.1a), and
an evidence manifest added (`scoping-notes/evidence-manifest.md`) so the
out-of-repo validation artefacts are pinned. Codex separately CONFIRMED
all five port-fix commits (`codex-fixes-verdict.txt`). A second DeepSeek
round then reviewed *this* revision — the whole document
(`deepseek-scoping-r2-verdict.txt`) and the gate specifically
(`deepseek-gate-verdict.txt`), both REFUTED — converging with codex on
two points: "tractability condition met" overstated
feasibility-to-complete into usable-now, and two convenience-sample
benchmarks cannot decide the general question. Adjudicated in
`cross-model-round3-adjudication.md` (both accepted as WEAKENED: the
probe conclusion survives; the wording and the gate's reach did not).
This revision moves the recommendation's centre of gravity accordingly
(§0, now leaning deflationary) and reframes the gate as
necessary-not-sufficient (§3.1a). The recommendation remains conditional
on a *diverse-workload* baseline, of which the running two-benchmark run
is only a calibration/existence stage.

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
Both empirical checks landed: the **original builds, runs, and survives
sustained re-randomisation on 2026 hardware** (period container:
`libquantum 851 2`, ~173 epochs per run, each mode separately and all
combined, output byte-identical to the uninstrumented oracle), and
**`parsa/stabilizer` now works on LLVM 21**: it built clean with PIE
working, initially crashed at three sites under sustained execution, and
all three were root-caused and fixed the same day (§1, §3.2), then
stress-tested (984k AFL execs, 0 crashes) — with the caveat that
"works" is attested on two single-threaded C benchmarks, and the known
port debt (threads, TLS, unwinding, hardened targets) is real. Two
honest complications temper the enthusiasm: **preliminary, low-powered
bzip2 data came out *reversed* from the paper's normality prediction**
(per-build padding means looked normal, Stabilizer's within-run samples
did not — §3.1) and **measured Stabilizer overhead was ~2×, far above
the paper's <7% median claim**, both awaiting the clean re-run. The
recommendation is therefore **conditional and, on current evidence,
leaning deflationary** — a shift from this document's earlier
"conditional yes, likely port", forced by three adversarial reviews
across two model families (codex + DeepSeek; §0 provenance). Precisely:
the tractability *probe* succeeded — the crashes are localised,
modest-effort bugs, not structural walls, so *completing* the port is
engineering rather than research — but the port is **not** a usable
general benchmarking tool today: it works on two single-threaded C
benchmarks while threads, TLS, C++ unwinding, hardened targets and
debugger support are all open (§3.2). So the honest position is: **the
working modern port is a deliverable in its own right** (preserved,
fixed, on a 2026 toolchain); **expect the cheap route** (K-seed padding +
DieHard, under proper randomised-paired methodology) **to suffice for
most benchmarking**; and **commit to finishing the port only if a
diverse-workload baseline shows a gap the cheap route cannot fill.** The
two-benchmark experiment now running (§3.1a) is a calibration/existence
stage, not that diverse-workload decision.

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
cluster), all 163 branches compared by ahead/behind; classification of
the 15 ahead branches from diffstats plus commit messages, with two full
patches spot-read (the survey's own coverage statement is the authority
on what was and was not examined). Note one internal discrepancy: the
`alternatives.md` sweep, written concurrently, knew only the two frozen
2023 forks — the fork survey then found `parsa/stabilizer`; the survey
supersedes the sweep on fork facts.

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
  unchanged. **Update, same day: all three crashes are now fixed** —
  `-Rstack` was `getRandomByte()`'s cursor-reset bug (byte-identical in
  the 2013 original, see §3 item 3), and `-Rcode` was the *same*
  ShuffleHeap asymmetry as `-Rheap`, on the code heap (every relocated
  function exceeds the 256 B bypass; gdb: `reqSz=4096`, `ptr` swapped to
  null). Fix commits `f9ed534`, `29afeef`, `19137a3` in
  `~/prog/stabilizer-parsa-fix/stabilizer`. Post-fix, the LLVM 21 port
  runs `libquantum 851 2` under all modes combined for ~165-170 epochs,
  three independent runs, output byte-identical to the uninstrumented
  oracle — matching the period original's behaviour. Side-finding: gdb
  breakpoints corrupt the runtime's `int3` trap protocol (both write
  0xCC), extending the known ptrace/strace limitation.
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
   A pilot of exactly this design has now run
   (`~/prog/stabilizer-baseline/`, 90 runs, libquantum, host clang/lld
   22.1.8, P-core pinned, ASLR on): within-build CV 0.71%; under 10
   padding seeds total CV 1.77%, with a between-seed variance component
   of **30% (σ_between ≈ 1.0% of mean; ANOVA p = 0.059 at 3 reps/seed —
   suggestive, not yet established)**. Corrected numbers: the pilot's
   own analysis initially reported "50.75% between-seed", which was
   SSB/SST — the seed factor's R², inflated by seed-mean estimation
   noise — caught by a cross-model review of the statistics plus an
   independent recomputation
   (`scoping-notes/recompute_pilot_stats.py`). The right framing is
   also not "6× more runs": a one-binary-per-arm comparison carries an
   **irreducible layout bias of order 1% of mean** — the size of
   effects people typically claim — and no amount of replication fixes
   a bias. The pilot's within-seed-CV anomaly traced to a run-order
   trend (r = −0.39, p = 0.03 in the PADDED arm only): round-robin
   spreads a seed's reps across the session, so slow drift lands in
   within-seed variance — the full experiment interleaves all arms
   globally and models run order. Normality numbers at pilot n decide
   nothing yet.
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
1a. **Pre-registered gate criteria** (written 2026-08-08 ~23:00, while
   the v2 batch is mid-run and unexamined beyond its first pairs;
   responding to codex's "unspecified gate lets either outcome be
   rationalised" and DeepSeek's "two benchmarks cannot decide the general
   question, and 'on both benchmarks' is gameable by workload choice").

   **The two-benchmark run is necessary-not-sufficient, and its two
   directions are not symmetric** — this is the key correction. libquantum
   and bzip2 are a convenience sample (single-threaded, call-heavy, from
   the tool's own suite), so:
   - A *pass* for the cheap route on them ("padding + DieHard controls
     layout variance below the effect of interest here") **does not
     generalise** — it cannot license "cheap route sufficient in general".
   - A *failure* — the cheap route leaving σ_b above the effect of
     interest, or Stabilizer demonstrably capturing variance the cheap
     route cannot — **is an existence proof** and does license
     "cheap route insufficient for at least some real workloads".
   So this stage can argue *for* the port (by existence) but cannot on its
   own argue *against* it. The against-decision requires the diverse
   portfolio below.

   **Metrics and analysis** (this stage). Primary estimand: within-pair
   treatment/PLAIN wall-time ratio from the globally randomised,
   PLAIN-paired schedule. Inference: seed as a random effect; seed-level
   BCa bootstrap (≥10k) plus a blocked permutation cross-check;
   method-of-moments components descriptive only. **Report the bootstrap
   CI *width* of σ_b against the 0.5%-of-mean line, and call any result
   whose CI straddles it INDETERMINATE** rather than forcing a verdict
   (DeepSeek's point: the pilot already sat near the boundary with wide
   uncertainty). Normality comparisons must respect aggregation symmetry
   (per-run vs per-run, per-seed-means vs per-epoch-means), never raw
   Shapiro across differently-aggregated distributions. Smallest effect
   of interest: 1% (the motivating AFL++ case claimed 3.7%); 0.5% σ_b
   threshold = half that, so 15+-seed blocking drives layout noise well
   under it.

   **The actual resurrection decision** needs a diverse workload
   portfolio, not these two: SPEC CPU2017 single-threaded subset, at
   least one genuinely multi-threaded workload, and one C++/exception
   workload — precisely the axes the port does not yet support, so that
   evaluation is gated behind the port debt in §3.2, not runnable today.
   Overhead is workload-dependent (the ~2× seen here is on the worst-case
   short call-heavy programs; the paper's <7% was SPEC median), so the
   overhead question is likewise open until the portfolio runs — do not
   read ~2× as a settled fail. Capability constraints (threads, TLS,
   unwinding, hardened targets, debugger support) are decision inputs,
   not footnotes: a port that never clears them is not a general tool
   however the variance numbers land. If the diverse portfolio shows the
   cheap route sufficient, the honest deliverable is BASELINE.md + the
   upstreamed harness improvements, and the port stops at "preserved,
   working, documented" — itself a worthwhile outcome.
2. **Probe the parsa runtime's tractability — COMPLETE, answer:
   tractable.** All three characterised crashes root-caused and fixed
   same-day (`~/prog/stabilizer-parsa-fix/`, commits `f9ed534` `29afeef`
   `19137a3`): two were one dependency-drift bug worn twice (modern
   DieHard ShuffleHeap's free-side bypass asymmetry, on the data then
   the code heap), one was a latent 2013 bug (the RNG cursor reset).
   Success criterion met: all modes combined, ~170 epochs, three runs,
   byte-identical output — parity with the period original on this
   benchmark. The fixes were cross-model reviewed and adjudicated
   (`scoping-notes/deepseek-fix-review-adjudication.md`) and then
   stress-tested (`~/prog/stabilizer-stress/NOTES.md`): 23k+ hypothesis
   op-sequences and an 80-min AFL++ run (983,748 execs, **0 crashes**,
   two hangs both triaged to benign large-alloc timeouts) against the
   real heap compositions; the RNG steady-state trace measured, not
   just derived (12/12 properties confirmed); a 7-run soak plus a second
   workload (bzip2) passing all modes. The adjudication's open residual
   (a malloc/free bypass-classification mismatch) was proven unreachable
   analytically and by exhaustive size sweep — MaxSize 256 is itself a
   Kingsley class boundary. Two follow-on commits landed and were
   verified: the RNG first-four-zeros micro-fix (`24df701`) and a
   defence-in-depth null guard (`6b263a4`).
   The probe condition in the gate above is therefore satisfied; the
   baseline condition remains open. Known remaining debt for the port
   proper: the TLS bug class (magras `4e154b8f`) untriggered by
   libquantum; threads; unwinding; debugger incompatibility (gdb/strace
   vs the `int3` protocol — gdb breakpoints and the runtime both write
   0xCC and corrupt each other's restore bookkeeping) to document; and
   two hardening items from cross-model review: `setHandler` leaves
   `sigaction.sa_mask` uninitialised (pre-existing upstream — garbage
   mask on a stack struct), and `getRandomByte`'s signal-handler
   reentrancy becomes real the moment threads exist. (The
   first-four-zeros RNG residual is fixed, `24df701`.) Engage Parsa
   Amini (a two-day commit burst in Feb 2026, prior burst April 2023 —
   an intermittent solo effort, not an active team) with the five
   commits — subject to the house rule that nothing is posted without
   Matthias approving the text.
3. **Drop Darwin and PPC** (Context test is Mach-O-only anyway), rewrite
   the Python 2 tooling trivially, and treat the **normality replication
   as the first experiment any revived Stabilizer runs** — no independent
   test of it was found (see §1's epistemic caveat), and it is the claim
   everything else stands on. If normality fails to replicate, the
   remaining case for within-run re-randomisation is variance efficiency
   and the stack/heap axes — decide then whether that is enough.
   The replication just acquired an extra reason to exist: fixing the
   port's `-Rstack` crash exposed that **the original's stack-pad
   randomness was largely broken as shipped** — `getRandomByte()` returns
   four zero bytes, then cycles a 256-byte window that is ~98% adjacent
   `.bss` contents and ~2% RNG output (bug byte-identical in the 2013
   source; full derivation in `runtime-analysis.md` addendum, to be
   confirmed with a small harness before quoting externally). The paper's
   `stack` ablation therefore ran with partially inert randomisation, and
   a fixed-RNG replication may genuinely move the numbers — in either
   direction.

## Language scope: not C-only, but the port's envelope is what binds

Asked because it bears on "the right basis in 2026". Stabilizer is an LLVM
pass plus a runtime; the pass operates on LLVM IR, so it is
**frontend-agnostic in principle** — anything that emits LLVM bitcode can
be instrumented. This is not hypothetical: parsa's `szc` driver already
handles **two** frontends through one bitcode→`opt`→link path — clang
(C/C++) and **Flang (Fortran)** (`szc` lines ~60, 106, 144-168). rustc is
an LLVM frontend and can emit bitcode (`--emit=llvm-bc`), so the pass
could mechanically be pointed at Rust too.

The binding constraint is not the language but the **runtime envelope**,
and Rust sits outside it on multiple axes at once — which is why "support
Rust" and "clear the port debt in §3.2" are the same task:
- **Threads** — idiomatic Rust and `std` are threaded; the runtime is
  structurally single-threaded. Bites immediately.
- **Unwinding** — Rust panics unwind via `.eh_frame`; relocated code has
  none. `panic=abort` sidesteps it but changes semantics.
- **TLS** — Rust `std` uses thread-locals pervasively; that is exactly
  the bug class (magras `4e154b8f`) absent from parsa's lineage.
- **Allocator** — Rust routes allocation through `GlobalAlloc`, not C
  `malloc`/`free`; the heap arm needs a `#[global_allocator]` shim
  forwarding to the Stabilizer heap or it simply does not apply.
- **Entry point** — the runtime hijacks `main`; Rust starts via
  `lang_start`.
So: code/stack randomisation on Rust is plausible with real integration
work, the heap arm needs an allocator shim, and the whole collides with
the single-threaded-runtime limitation. A Rust (or C++/exceptions, or any
threaded) workload is therefore a **port-debt probe**, not "just another
benchmark" — the same conclusion the gate reached from the variance side.

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
