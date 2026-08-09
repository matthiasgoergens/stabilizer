# PR3 scope: the untrap protocol fixes no reproducible bug (evidence)

2026-08-09. Taking over bug-#4 verification from the agent, the pre-fix
baseline produced a course-correcting result.

## Two distinct teardown paths
- **onTimer teardown** ("Placing traps" branch runs when a SIGALRM fires
  after stabilizer_main returns): the REAL, measured bug. Reproduced on
  libquantum combined-mode at 3/13 (and 1/6 -Rcode); fixed by **b274f86**
  (shutting_down flag in onTimer + setTimer(0)); post-fix 20/20. PROVEN.
- **onTrap teardown** (a destructor/atexit calls a still-trapped
  instrumented function after main returns → int3 → onTrap relocates):
  the THEORETICAL sibling codex+deepseek flagged when they said b274f86
  alone was incomplete. The untrap protocol (49ffde3) guards it.

## Evidence that onTrap-teardown is benign for normal programs
The deterministic probe (teardown_probe.cpp): `neverCalled` is trapped at
startup, never called during the run (so never relocated → still trapped),
and called only from an atexit handler post-teardown.
- On **b274f86 (no untrap)**: probe 20/20 clean, and a captured run shows
  `neverCalled` executing at teardown with exit 0. It has an int3 at entry
  and no crash → onTrap fired and forwarded it benignly (if onTrap had NOT
  fired, the int3 would SIGTRAP-crash; it didn't). So onTrap-at-teardown is
  exercised and harmless while code is mapped.
- The fault codex/deepseek theorised requires the function's code to be
  **unmapped** at call time (dlclose of a shared lib whose function an
  atexit later calls) — a use-after-unload in the program itself, which the
  probe does not and normal exit does not create. Not reproduced.

## Conclusion / proposed PR3 scope
- PR3 = the **onTimer teardown fix (b274f86)** — proven, minimal — plus the
  `sigemptyset(&sa.sa_mask)` hygiene one-liner.
- The **untrap protocol (49ffde3) is NOT shipped as a bug fix**: it guards a
  path that does not fault for normal (non-dlclose) programs. Mention
  onTrap-at-teardown honestly in the PR as benign-in-practice, with the
  dlclose caveat, and offer the untrap guard if the maintainer wants
  belt-and-braces. Keeps the diff to what is demonstrated.
- This reverses the earlier "ship the complete untrap protocol" plan; it is
  going back through codex+deepseek before PR3, since it also reverses their
  earlier "b274f86 is incomplete" verdict — now answered with measurement.

## Resolution (2026-08-09): measured — ship onTimer-only

codex: CONFIRMED (ship onTimer-only). DeepSeek: UNVERIFIABLE, proposing a
"decisive" test — a destructor double-frees to corrupt the heap, then an
atexit calls a still-trapped function, so onTrap allocates from the
corrupted heap. Disagreement resolved by running DeepSeek's exact test on
both builds:
- b274f86 (onTimer-only): 10/10 abort, **ran-trapped=0**.
- 49ffde3 (untrap): 10/10 abort, **ran-trapped=0** — identical.
- Abort message: **"free(): double free detected in tcache 2"** — glibc
  catches the program's own double-free in the destructor, BEFORE
  trapped_func/onTrap runs. The onTrap-on-corrupted-heap path DeepSeek
  hypothesised is never reached; untrap changes nothing.
So untrap has no measurable benefit: the dlclose case faults at instruction
fetch before the int3 (untrap can't help — codex's point), and the
heap-corruption case aborts on the program's own bug before onTrap.
**Decision: PR3 = onTimer teardown fix + sigemptyset(sa_mask). Untrap not
shipped.** Both families' positions reconciled by the measurement; codex's
scope confirmed.

## Post-decision vindication (2026-08-09)

The teardown agent, continuing on the (unshipped) untrap protocol, found
that its first untrap implementation (`49ffde3`) was **itself buggy —
20/20 crashes on the teardown probe**: untrap()'s "restore original bytes"
path is unsafe for never-relocated functions (their compiled prologue's
relocation-table reference is only valid post-relocation). A corrected
version (`becfc86`, routing never-relocated functions through the same
relocate() path onTrap uses) is 20/20 clean. It also re-confirmed the
shipped code: **b274f86 (onTimer-only) is 20/20 clean on the probe.**

So not shipping untrap was doubly correct: it fixes nothing reproducible
AND its obvious implementation introduced a fresh 20/20 crash. Shipping the
"complete protocol" would have handed the maintainer a new bug. The
corrected untrap (`becfc86`, in `~/prog/stabilizer-teardown-fix/stabilizer`)
is archived, not shipped — available only if a future dlclose/unmap use
case ever justifies it. This is the "minimal proven fix beats the elegant
complete one" lesson, measured.
