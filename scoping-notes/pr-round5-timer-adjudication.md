# Round 5: the teardown fix is incomplete (both families converged)

2026-08-09. After PR2 posted, artefact smoke-testing PR1's branch surfaced
an intermittent exit-time SIGABRT. I first (wrongly) guessed it was
environmental; the Phase-1b Rust frontend agent then root-caused it: the
re-randomization timer is never disarmed at teardown, so a SIGALRM after
stabilizer_main returns runs onTimer's "Placing traps" path against
unmapping code → onFault ABORT (3/13 combined, 1/6 -Rcode).

First fix (commit b274f86 / branch llvm21-timer-fix): shutting_down flag
checked at onTimer entry, set before setTimer(0). Verified onTimer path:
20/20 clean vs 3/13. Posted for review as draft PR3.

**Codex (DO-NOT-POST) and DeepSeek (REFUTED point 2) independently found
the same hole: onTrap is unguarded.** SIGTRAP stays installed and code
entries stay trapped, so an atexit handler or C++ destructor that calls a
trapped instrumented function enters onTrap during teardown and hits the
same class of fault — and returning early from onTrap is invalid (execution
resumes on the int3). My 20/20 didn't cover it because libquantum calls no
trapped functions at exit. So the timer fix is a correct *partial* fix, not
a complete shutdown protocol.

Both families agree on what a complete fix needs:
1. onTimer guard + disarm timer (done) — but also block SIGALRM through
   exit, don't claim every in-flight alarm is a no-op.
2. SIGTRAP: before destructors run (i.e. right after stabilizer_main
   returns, in the wrapper main), UN-trap every registered function entry
   (restore a normal forwarding jump / original bytes) so teardown calls
   don't trap. Cannot be fixed by an onTrap early-return.
3. sigemptyset(&sa.sa_mask) in setHandler (currently uninitialised —
   pre-existing).
4. A deterministic teardown test: an atexit/destructor callback invoking an
   instrumented function after user main returns; repeat-run atop PR1.

Decisions:
- PR1 (heap): POST-WITH-EDITS applied; posted as parsa/stabilizer#2,
  reworded self-contained (references the timer race as a fix "in progress",
  not an unposted PR).
- PR3 (timer/teardown): HELD. Rework into a complete shutdown protocol
  (untrap + block signals + sa_mask), verify with the onTrap teardown test
  AND the onTimer stress, re-review both families, then post.
- Structure stays two separate PRs (heap = Heap.h, teardown = libstabilizer.cpp).
