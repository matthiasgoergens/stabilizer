# Follow-up investigations

Ideas queued behind the scoping/baseline work. Recorded here so they survive
the session; none of these is started.

## Meta: old papers and once-working software as a project mine

Observation (Matthias, 2026-08-07, mid-resurrection): "In general, old
papers and once working software seems to be a great mine for projects."
This session is supporting evidence: one 2013 paper + bitrotted tool
yielded, in a day, a working resurrection (three root-caused fixes), two
bug classes relevant upstream (DieHard's ShuffleHeap free asymmetry; the
original's RNG cursor bug), a finding about the original paper's own
validity (stack randomisation partially inert as shipped), and a
measured, publishable number (51% between-seed variance). The same
pattern surfaced earlier with Hat → Hoed in Haskell tracing. What makes
the mine rich: the hard ideas are already validated by publication, the
bitrot is usually shallower than it looks (here: two dependency-drift
bugs), and nobody else is looking — thirteen years of forks and not one
was verified by building. Candidate heuristics for picking targets:
citation count high but zero maintained implementations; README says
"unmaintained" but the fork network shows repeated independent revival
attempts (demand signal); the blocking work is mechanical (API churn)
rather than conceptual.

Addendum (same day): mine by *lab signature*, not just by paper. Matthias
recalled "moving GC for C via overlaying sparse pages" — that is Mesh
(Powers/Tench/Berger/McGregor, PLDI 2019, plasma-umass/Mesh, still
maintained as of 2026-03). Same PLASMA lab as Stabilizer/DieHard/Coz;
their common signature is "randomness + virtual-memory tricks against
unmanaged-code constraints". A lab with one resurrectable hit likely has
siblings: the maintained ones (Mesh, DieHard, Coz) are design references
and tooling, the dead ones are candidates. Mesh is also a candidate
second randomising-allocator arm for BASELINE.md (checks DieHard-arm
findings are not allocator-specific).

## Give Mesh a serious AFL++ run

(Matthias, 2026-08-07.) Mesh is dormant-but-alive (last push 2026-03-25 —
five months quiet; note "alive" is repo metadata, not a build
verification — first step is confirming it builds against current
clang/glibc at all). It is a production-intent allocator whose meshing
path (copy live objects between pages, remap two virtual pages onto one
physical page, under concurrency) is exactly the kind of rarely-exercised
machinery fuzzing finds bugs in — and the DieHard ShuffleHeap asymmetry
we just root-caused shows this code family's free-path edge cases go
unnoticed for years. Shape: reuse the stabilizer-stress approach — a
stdin-driven malloc/free/realloc op-sequence shim linked against
libmesh, ASAN+UBSAN, hypothesis for structured adversarial sequences
(sizes straddling size-class and page-occupancy boundaries to force
meshing), then AFL++ for long campaigns; specifically try to trigger
meshing during mutation (allocate to ~50% page occupancy, free
alternating objects, keep allocating during the mesh window). Any find
is upstreamable to a maintained repo with a responsive owner — quick
feedback loop, and a natural companion PR to the DieHard/ShuffleHeap
report if we send that.

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
