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
- Bug #5 is fixed locally in `~/prog/stabilizer-bug5/stabilizer`, commits
  `c6ccb07` (5a relocation classification) and `2936cb6` (5b/5c redesign and
  regression), on `master` two commits ahead of `origin/master`
  (`git status --short --branch`, exit 0).
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
  (`~/prog/stabilizer-bug5/logs/`; measured 2026-09-11).
- The original deterministic teardown reproducer now emits all five expected
  lines. `llvm-nm` confirms that no `stabilizer.dummy.*` symbols remain
  (`~/prog/stabilizer-bug5/repro/teardown.cpp`; measured 2026-09-11).
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
(`~/prog/stabilizer-bug5/logs/codex-bug5-fix-review-{1,2,3}.txt`).

## Next three actions

1. Rebase/consolidate the LLVM 21 fixes, including `2936cb6`, on a dedicated
   maintained branch; add CI and document the Linux x86_64 support boundary.
2. Pre-register and run a minimal Lean compatibility/calibration probe: exact
   output parity, LLVM/linker compatibility, threads/TLS/unwinding, allocator
   coverage, sustained re-randomisation and retained per-layout observations.
3. Apply the resulting treatment ladder to lean-zip/faster-lean: saved-binary
   controls, linker padding seeds, then Stabilizer code/stack/heap separately,
   randomised within short blocks with layout—not repeated execution—as the
   experimental unit.

## Unverified beliefs

Stabilizer may make small Lean effects more identifiable. Parsa may re-engage.
Treat both as hypotheses until a Lean calibration and an upstream response.
