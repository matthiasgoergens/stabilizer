# NEXT — Stabilizer resurrection scoping (state as of 2026-08-07 evening)

Goal: RESURRECTION-BRIEF.md tasks 1–4. Task 1–3 done; task 4 piloted; task 5
(port) two-thirds probed. **Nothing has been pushed anywhere — all work is
local commits only; pushing needs Matthias's approval.**

## Done, with evidence

- **SCOPING.md** (repo root): conditional-yes recommendation, adversarially
  reviewed and corrected. History: draft 1b626e5 → review applied c72eeb4 →
  oracle cd2eab1 → heap fix 50f05c2 → stack fix + pilot cd2fc63. In-family
  refutation agent found 2 blocking overclaims (both re-verified by me
  before applying — see commit c72eeb4's message); DeepSeek cross-model
  pass returned CONFIRMED (scoping-notes/deepseek-verdict.txt).
- **Research notes** in scoping-notes/ (each with its own coverage
  statement): fork-survey.md (51 repos/163 branches; standouts
  parsa/stabilizer, detached Dead2/stabilizer), citation-graph.md (104/117
  citers, no successor/replication found by abstract triage), llvm-rfc.md
  (nothing rejected — never proposed; pcc's in-tree run_benchmark.py is
  the native K-seed harness), alternatives.md (no within-run rerandomiser
  exists anywhere; DieHard maintained), runtime-analysis.md (mechanism +
  2026 viability + RNG addendum).
- **Original works on 2026 hardware**: period container
  (~/prog/stabilizer-period/, NOTES.md + libquantum-851-results/):
  libquantum 851 2, ~173 epochs/run, modes separate and combined, exit 0,
  output byte-identical to uninstrumented oracle.
- **parsa/stabilizer port probe** (~/prog/stabilizer-parsa-fix/stabilizer,
  local clone of 2bffc191c9): builds zero-patch on LLVM 21.1.8, PIE works.
  - `-Rheap` crash FIXED, commit f9ed534: modern DieHard ShuffleHeap
    malloc bypasses shuffle buffer >MaxSize, free does not → null from
    never-filled bin. gdb-confirmed (gdb-heap-crash-02.log). Verified
    ~150 epochs ×4 runs, byte-identical output.
  - `-Rstack` crash FIXED, commit 29afeef: getRandomByte() cursor reset
    to sizeof(int) not 0 — bug byte-identical in 2013 original
    (runtime/Util.h:48). Crashed only because modern .bss is 32× larger
    and the buffer sat at its exact end. Verified ~170 epochs ×2 runs.
  - 2013-pinned Heap-Layers does NOT compile against the port
    (build-04-pinned-scratch.log) — version-skew confound closed:
    adaptation was forced, carried exactly one bug.
- **Research finding**: the original's stack-pad randomness was largely
  inert as shipped (getRandomByte sole consumer = stack pads). Derivation
  in runtime-analysis.md addendum. Externally quotable only after harness
  confirmation (see Unverified).
- **Baseline pilot ran** (~/prog/stabilizer-baseline/, own git repo,
  results.csv, NOTES.md): libquantum, clang/lld 22.1.8 -O2, P-core
  pinned, ASLR on. Within-build CV 0.71%; 10 padding seeds → CV 1.77%
  with 50.75% of variance between-seed. Sizing: ~6× more runs needed for
  1% effect if layout ignored. 90/90 runs output-correct.
- FOLLOW-UPS.md: phantom-speedup archaeology (Matthias's idea, recorded).

## In flight

- **-Rcode epoch-2 sweep() crash**: background agent diagnosing on top of
  f9ed534+29afeef. Prime suspect given the heap fix: same ShuffleHeap
  asymmetry on the CODE heap (function allocations ≫256 B; the
  ShuffleFreeGuard was data-heap-only). Success criterion: all three
  modes combined, libquantum 851 2, ~170 epochs, byte-identical.

## Blocked / pending

- **codex cross-model pass**: quota-blocked until 2026-08-08 ~20:43
  (error in task log). Run against SCOPING.md revision cd2fc63 or later.
- **Push to matthiasgoergens/stabilizer**: needs Matthias's go-ahead.
- **Contacting Parsa Amini / upstream authors**: only with approved text;
  two working fixes + the RNG finding are the material.

## Unverified beliefs (do not quote as fact)

- The 256-byte-window characterisation of getRandomByte's steady state
  (uint8_t wrap → 4 random + 252 .bss bytes/cycle) is a source-read
  derivation; confirm with a ~10-line harness before external use.
  (The reset-to-4 bug itself IS verified: gdb + fix + 170-epoch runs.)
- Dead2's README claim that SZ_HEAP/SZ_LINK "work" on LLVM 12 was never
  build-verified; treat with the same scepticism parsa's claim earned.
- Pilot anomalies unexplained: PADDED within-seed CV (~1.5%) > SINGLE CV
  (0.71%) despite identical binaries; DIEHARD Shapiro-Wilk p=0.035.
  Small n — re-examine at scale, don't build on them.

## Next actions, in order

1. Collect the -Rcode agent's result; fold into SCOPING.md §1/§3. If all
   three modes pass combined, the port-tractability question is closed
   affirmatively (then: propose fixes upstream to parsa — text via
   Matthias).
2. Confirm the getRandomByte cycle with a tiny harness (pure userspace,
   minutes); update runtime-analysis.md addendum from "derived" to
   "measured" (or correct it).
3. Codex pass after quota reset; then decide with Matthias: scale the
   baseline (BASELINE.md proper: more benchmarks, more reps, Stabilizer
   arm now that the port heals) vs. write up first.

Cross-model review of session diffs: not run — no uncommitted diff; codex
quota-blocked. DeepSeek reviewed the *document* (CONFIRMED), not the code.
The two port fixes live in ~/prog/stabilizer-parsa-fix/stabilizer (not
this repo) — worth a codex review of those two commits tomorrow.
