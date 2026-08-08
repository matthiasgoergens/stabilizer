# Evidence manifest — where the out-of-repo validation lives

Codex's scoping review (2026-08-08, `codex-scoping-verdict.txt`) correctly
noted that much of SCOPING.md's load-bearing validation lives under
`~/prog/...`, not in this repository, so a reader cannot audit it from the
stated evidence base. This manifest pins those artefacts: paths, key
revisions, and what each establishes. None of it is on GitHub; it is
local to Matthias's machine as of 2026-08-08. (The fixes themselves ARE
published — `matthiasgoergens/stabilizer` branch `llvm21-fixes`, tip
`6b263a4`.)

## The port and its fixes
- Repo: `~/prog/stabilizer-parsa-fix/stabilizer` (clone of
  `github.com/parsa/stabilizer` at `2bffc191c9`).
- Fix commits (also pushed to `matthiasgoergens/stabilizer:llvm21-fixes`):
  `f9ed534` (-Rheap guard), `29afeef` (-Rstack RNG cursor),
  `19137a3` (-Rcode guard), `24df701` (RNG first-call refill),
  `6b263a4` (null guard).
- Toolchain: Ubuntu 24.04 container, clang/opt/lld 21.1.8, rootless podman.
- Dependencies pinned for reproducibility: Heap-Layers
  `341fff3581be4327bc30ca81e90c6ba513942692`, DieHard `65be5ec1...`
  (recorded in `~/prog/stabilizer-period/NOTES.md`). Note: the *port's*
  build uses modern HEAD of these (2013-pinned does not compile against
  it); the *period* build pins 2013.

## Verification records
- `~/prog/stabilizer-parsa-verify/NOTES.md` + crash logs — the original
  three-crash characterisation (pre-fix).
- `~/prog/stabilizer-parsa-fix/NOTES.md` — root causes, gdb transcripts,
  per-fix verification (all-modes `libquantum 851 2`, byte-identical to
  oracle).
- `~/prog/stabilizer-period/NOTES.md` + `libquantum-851-results/` — the
  period-container oracle: original on LLVM 3.1, ~173 epochs, all modes,
  output-identical to its own uninstrumented build. THIS is the "matches
  the original" claim's basis — each side verified against its own era's
  compiler, no cross-era diff.
- `~/prog/stabilizer-stress/NOTES.md` — hypothesis (23k+ sequences),
  AFL++ (983,748 execs, 0 crashes, 2 benign hangs), RNG harness (12/12
  measured), soak, bzip2. Harnesses committed in that repo's git.

## Baseline
- `~/prog/stabilizer-baseline/` (own git repo). v1 batch
  `results_full.csv` = CONTAMINATED (do not quote; `BASELINE-draft.md`
  explains). v2 load-robust batch → `results_v2.csv` (running as of this
  writing). Analysis: `analyze.py` (harness) cross-checked by
  `scoping-notes/analyze_baseline_full.py` (independent).

## Not yet immutable
These live on one machine and are not backed up off it. Before any
external publication that relies on them (e.g. the reversed-normality
finding), the relevant logs and raw CSVs should be committed into this
repo or an archive, with container image digests recorded. Until then,
treat every `~/prog/...` reference as "verified locally, not yet
independently reproducible".
