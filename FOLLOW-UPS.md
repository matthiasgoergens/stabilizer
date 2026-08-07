# Follow-up investigations

Ideas queued behind the scoping/baseline work. Recorded here so they survive
the session; none of these is started.

## Phantom-speedup archaeology: re-test historical "performance improvements", revert the ones that were noise

Idea (Matthias, 2026-08-07): look for projects that made performance
improvements in the past, and check whether they actually helped or were
random chance. If the latter, undo the changes — assuming that makes the
code simpler.

Why this is a natural follow-up rather than a separate project: it is the
consumer-side application of exactly the tooling this repo is scoping. A
"3% faster" commit measured once, years ago, on one layout, is one draw
from the layout lottery — the same failure mode as the AFL++ one-byte
`memcpy` case that started this whole thread
(`~/prog/aflpp-postprocess-memcpy/FINDINGS.md`). The paper's own headline
(-O3 vs -O2 indistinguishable from noise) is the compiler-level version of
the same claim.

Sketch of a method:

1. Mine `git log` of a target project for commits whose messages claim a
   measured win ("N% faster", "speeds up", "optimize hot path"), filtered
   to ones that *added complexity* (size of diff, new special cases, new
   caches, manual inlining/unrolling) — the revert candidates. Commits that
   both claimed a win and simplified code are not interesting here.
2. For each candidate: build at the commit and at its parent, and A/B them
   under layout-controlled measurement (the task-4 baseline harness:
   `run_benchmark.py`-style K padding seeds, DieHard arm for heap, plus
   whatever the scoping recommends — Stabilizer proper if revived).
3. Classify: real win (survives layout control), phantom (indistinguishable
   from noise), or inverted (was a regression hiding in layout luck).
4. For phantoms whose revert simplifies the code: prepare the revert with
   the measurement as evidence.

Caveats to design around, recorded now so they are not re-derived later:

- **"Was noise then" vs "is noise now."** Hardware and compilers moved; a
  change may have genuinely helped in 2019 and be irrelevant on 2026
  hardware, or vice versa. The honest claim after re-measurement is "does
  not help on current hardware/toolchain, and simplifies the code" — which
  is still a perfectly good revert justification, just a different one.
  Distinguishing the two would need period hardware/toolchains and is
  usually not worth it.
- **Selection effect.** Projects keep the wins that were easy to measure;
  the phantom rate among *complexity-adding micro-optimisations* is the
  interesting number, and measuring it across enough commits is itself a
  publishable observation (a field base rate for the Mytkowicz effect in
  the wild).
- **Maintainer relations.** A stream of "your old optimisation was noise"
  PRs is exactly the failure mode the house rules exist for: findings
  accumulate into one write-up per project, every claim survives a
  refutation pass first, and nothing is posted without Matthias approving
  the text.
- **Good first targets** are projects with benchmark-heavy cultures and
  micro-optimisation-dense histories (compression libraries, hash
  libraries, allocators, codecs — e.g. the zlib-ng orbit is adjacent:
  Dead2, who maintains zlib-ng, also maintained the LLVM-12 Stabilizer
  fork, and might be a sympathetic early collaborator). AFL++'s
  postprocess `memcpy` case is the already-in-hand prototype example.

Dependency: needs the task-4 baseline harness working (at minimum the LLD
K-seed + DieHard arms). It is also the best *demo* of that harness — a
concrete "this tooling changed a real decision" story.
