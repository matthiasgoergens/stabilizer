# NEXT — Stabilizer resurrection (state at 2026-09-11)

## Goal

Make Stabilizer reliable enough to improve performance investigations in
`~/prog/lean/faster-lean`; use lean-zip as the first flagship layout-sensitive
application. The immediate target is Linux x86_64 with LLVM 21, not the broad
platform support claimed by the inherited README (user direction, 2026-09-11;
`ROADMAP.md:1-14`; `~/prog/lean/faster-lean/NEXT.md`).

## Verified state

- The three earlier fixes remain open, cleanly mergeable PRs against Parsa:
  RNG [#1](https://github.com/parsa/stabilizer/pull/1), heap/code free
  [#2](https://github.com/parsa/stabilizer/pull/2), and timer teardown
  [#3](https://github.com/parsa/stabilizer/pull/3). None has comments or reviews
  (`gh api repos/parsa/stabilizer/pulls?state=open`, exit 0, 2026-09-11).
- Bug #5 is fixed on the maintained code line in
  `~/prog/stabilizers/stabilizer-bug5/stabilizer`. Commits `c6ccb07` and
  `2936cb6` implement the relocation-classification fix and the code-layout
  redesign; `4767fcc` also incorporates the signal-mask initialisation needed
  by the LLVM 21 line. A focused upstream branch, `bug5-code-layout`, contains
  only the two bug-#5 commits rebased directly onto Parsa's `master`.
- The fix removes linker-adjacent dummy/code-limit assumptions, obtains final
  function extents from ELF `STT_FUNC` sizes, validates all metadata before
  patching entries, and reserves an explicit 32-byte patchable entry. Relocated
  copies omit the entry padding; x86_64 forwarding is one checked `jmp rel32`.
  CET branch protection, stripped symbol tables, duplicate function addresses,
  invalid extents and ambiguous boundary relocations fail explicitly
  (`2936cb6`, chiefly `pass/Stabilizer.cpp`, `runtime/Function.h`,
  `runtime/TextRelocations.cpp`, and `runtime/Jump.h`).
- The new `tests/CodeLayout` regression includes small C++ functions, global
  construction/destruction and a call after the 500 ms re-randomisation epoch.
  It failed against the old design with exit 134 and passes after `2936cb6`;
  ten repeated epoch-crossing runs also passed
  (`~/prog/stabilizers/stabilizer-bug5/logs/`; measured 2026-09-11).
- The original deterministic teardown reproducer now emits all five expected
  lines. `llvm-nm` confirms that no `stabilizer.dummy.*` symbols remain
  (`~/prog/stabilizers/stabilizer-bug5/repro/teardown.cpp`; measured
  2026-09-11).
- The final full suite passed after the CET guard: HelloWorld, CodeLayout,
  libquantum and bzip2 (`podman exec bug5-fix make --directory
  /work/stabilizer test`, exit 0, 2026-09-11). A stripped-binary negative
  control and a CET-enabled compile both failed early with the intended clear
  diagnostics (measured 2026-09-11).
- The existing thread design is not implemented. The runtime remains
  structurally single-threaded; full Rust, and likely realistic Lean workloads,
  require deliberate thread/TLS/unwinding validation (`ROADMAP.md:56-96`).
- The faster-lean harness already identifies layout as a nuisance and supports
  independently shuffled blocks. Its current experiments use one linked layout;
  its `NEXT.md` names layout-seed replication and Stabilizer compatibility as
  the next measurements (`~/prog/lean/faster-lean/NEXT.md`, read 2026-09-11).

## Maintenance reality

- Canonical
  [`ccurtsinger/stabilizer`](https://github.com/ccurtsinger/stabilizer) says it
  is no longer actively maintained. Its last code-affecting commit was
  in 2016; 2021 only updated documentation/licences (`gh api
  repos/ccurtsinger/stabilizer/commits`, exit 0, 2026-09-11).
- [`parsa/stabilizer`](https://github.com/parsa/stabilizer) is the strongest
  LLVM 21 base, but its last push was 2026-02-14 and the three August PRs have
  received no response (`gh api repos/parsa/stabilizer`, exit 0, 2026-09-11).
- The earlier community fork
  [`Dead2/stabilizer`](https://github.com/Dead2/stabilizer) last pushed in 2023
  (`gh api repos/Dead2/stabilizer`, exit 0, 2026-09-11).
- Operational conclusion: Matthias should act as de facto technical maintainer
  of the resurrection—releases, CI, triage, compatibility policy and docs—while
  continuing to offer changes to Parsa. This is not formal upstream ownership.

## Refutations and boundaries

- The old bug-#5 hypothesis that lld relocation relaxation caused the corruption
  was refuted by matching disassembly and relocation records; the faults were
  Stabilizer's adjacent-layout assumptions
  (`scoping-notes/bug5-findings.md:12-38`).
- Bug #5 passing C/C++ tests does not establish Lean compatibility, thread
  safety, low overhead, statistical normality or improved investigation
  productivity. None has yet been measured on a Lean artefact.

## Cross-model review

Three high-effort Codex reviews ran. The first exposed a portability assertion
and the second an ENDBR/CET mismatch; both were fixed. The final review found no
actionable regression and ran runtime syntax checks plus all four test binaries
(`~/prog/stabilizers/stabilizer-bug5/logs/codex-bug5-fix-review-{1,2,3}.txt`).
Kimi K3 independently reviewed the final two-commit fix, rebuilt it against
LLVM 21, reran all four tests and returned `APPROVE`; its findings were minor
hygiene and test gaps
(`~/prog/stabilizers/stabilizer-bug5/logs/kimi-bug5-review.txt`).

## Next three actions

1. Publish the maintained code-only `master`, preserve these plans on
   `resurrection-notes`, and offer the focused bug-#5 branch to Parsa.
2. Pre-register and run the existing faster-lean linker layout-seed
   replication. Treat each linked layout as the experimental unit and first
   verify that the seeds cause meaningful address variation.
3. Run a minimal Stabilizer/Lean compatibility probe: exact output parity,
   LLVM/linker compatibility, threads/TLS/unwinding, allocator coverage and
   sustained re-randomisation. Add CI and document the Linux x86_64 boundary.

## Unverified beliefs

Stabilizer may make small Lean effects more identifiable. Parsa may re-engage.
Treat both as hypotheses until a Lean calibration and an upstream response.
