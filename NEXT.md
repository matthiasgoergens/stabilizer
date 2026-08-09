# NEXT — Stabilizer resurrection (state as of 2026-08-09)

**Bash outage (2026-08-09, ~1h) — RESOLVED, root-caused.** Symptom: every
command (even `true`) returned exit 1, no output = `fork()` failing. Cause:
memory exhaustion under peak parallelism (several multi-GB agent processes
+ podman LLVM/Rust builds + baseline batch + AFL at once). Evidence: ~21s
cumulative *full* PSI memory stall, 51 GiB swap used, `overcommit_memory=0`
(heuristic overcommit denies fork ENOMEM under pressure). NOT a pid cap
(cgroup 2914/153301), no visible OOM-kill (dmesg/journal root-restricted).
**Mitigation: cap concurrent heavy agents/containers (~2–3, not 5+).**

## Live on parsa/stabilizer (all verified + two-family reviewed)
- **#1** getRandomByte OOB → fixes `-Rstack` (branch `llvm21-rng-fix`).
- **#2** ShuffleFreeGuard → fixes `-Rheap` + deterministic `-Rcode` sweep
  crash (branch `llvm21-heap-fixes`).
- **#3** disarm timer at teardown + sa_mask → fixes intermittent exit-time
  `-Rcode` fault (branch `llvm21-timer-fix`). Depends on #1/#2. Scope
  settled by measurement: untrap protocol NOT shipped (no measurable
  benefit — see `scoping-notes/pr3-scope-finding.md`).
- The three are a stack; full `-Rcode` reliability needs all three.
- Our fork carries these + `llvm21-fixes` (all 5). Scoping repo pushed.

## Bug #4 (teardown race) — DONE, posted as PR #3 (see above). Below is history.
- Symptom: SIGALRM/SIGTRAP after `stabilizer_main` returns runs onTimer/
  onTrap against torn-down code → fault. Partial fix (onTimer only) was
  `b274f86`; both codex+deepseek found onTrap is an unguarded sibling.
- **Complete protocol committed** at `~/prog/stabilizer-teardown-fix/
  stabilizer`, branch `teardown-protocol`, commit `49ffde3` (on b274f86):
  `Function::untrap()` (restores forwarding jump or saved header) called
  over all functions in wrapper main after stabilizer_main returns;
  `sigprocmask(SIG_BLOCK, SIGALRM)`; `sigemptyset(&sa.sa_mask)` in
  setHandler. Residual: 1-instruction window before the block (harmless,
  documented).
- Verified so far: 2 clean post-fix runs of a deterministic teardown probe
  (atexit calls an instrumented fn; both "never-relocated/trapped" and
  "already-forwarded" cases), stdout matched libquantum(851,2). Logs in
  `~/prog/stabilizer-teardown-fix/.../teardown-notes/run-03*`.
- ⛔ STILL NEEDED before PR3: pre-fix baseline fault-rate on b274f86 (for
  contrast); ≥20 repeats of the teardown probe; ≥20 libquantum
  `-Rcode -Rheap` non-regression (must stay 0 aborts like b274f86's 20/20);
  oracle byte-diff; commit `teardown_probe.cpp` + the `shor.c` probe call
  site + NOTES.md (currently uncommitted); then codex+deepseek on the diff;
  then create branch from 2bffc191c9 and post PR3 (text draft:
  `~/prog/stabilizer/scoping-notes/pr3-timer-FINAL.md`, will need updating
  to describe the complete protocol, not just the timer).

## Bug #5 — ROOT-CAUSED (2026-08-09), not a PR yet. See scoping-notes/bug5-findings.md
-Rcode corruption on tiny/C++-static-heavy binaries. Repro deterministic
(`~/prog/stabilizer-bug5/repro/teardown.cpp`). Initial lld-relaxation
hypothesis REFUTED by objdump/readelf; bug is in Stabilizer's own runtime.
THREE defects (codex-confirmed): 5a applyTextRelocs internal/external test
keys on S not S+A (FIX COMMITTED in bug5 repo, safe, insufficient alone);
5b 32-byte FunctionHeader overwrites the next function when thunks are
16 B apart (structural); 5c code.size() from a linker-reordered dummy
underflows (structural). Alignment hack fixed 5b but activated 5c →
reverted. **Decision pending: (a) report to parsa as an issue + offer 5a;
(b) do the 5b/5c redesign then PR; (c) hold.** Issue text needs approval.

## Done this session (committed + pushed unless noted)
- SCOPING.md: conditional/deflationary recommendation, adversarially
  reviewed (codex WEAKENED→addressed, 2× deepseek), pre-registered gate.
- BASELINE.md (`~/prog/stabilizer-baseline`): load-robust design validated
  (63% load CV → within-pair ratio r~0); cheap route ~0% overhead,
  Stabilizer ~2–2.7×; between-seed variance below gate on bzip2,
  indeterminate on libquantum; normality NOT claimed (needs dedicated
  stable-load test). Follow-ups: dedicated normality run; padding-seed
  build only made 15/20.
- ROADMAP.md: Rust north star. Phase 1 spike + Phase 1b real szc `-lang=rust`
  frontend DONE (single-threaded panic=abort, all modes, global_allocator
  live; `~/prog/stabilizer-rust-frontend`). Phase 2 threads DESIGN done
  (`~/prog/stabilizer-threads-design/DESIGN.md`, Shuffler-based). Phase 3/4
  pending.
- FOLLOW-UPS.md: phantom-speedup archaeology; Mesh AFL++; old-papers-mine.

## Next actions when Bash returns, in order
1. Commit this NEXT.md (scoping repo) + push.
2. Finish bug-#4 verification (⛔ list above); if clean, codex+deepseek the
   diff, then post PR3.
3. Consider bug #5 as its own investigation/PR.
4. Baseline follow-ups (dedicated normality experiment; fix padding-seed
   build) if continuing the measurement thread.
