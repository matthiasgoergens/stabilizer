# Stabilizer resurrection — brief for the agent picking this up

Written 2026-08-07. Read this before touching any code.

**The first deliverable is a scoping report, not a port.** If you find yourself
fixing LLVM API breakage in week one, you have skipped the question this brief
exists to ask.

## Tasks

In order. Do not skip ahead to task 4.

1. **Scoping pass: Stabilizer versus the LLD flag versus anything else.**
   Answer "is resurrecting Stabilizer the right basis for statistically sound
   performance evaluation in 2026?" See *The question you must answer first*.
   This is the bounded first deliverable and the reason this session exists.
2. **Survey the 46 forks properly** for existing unbitrotting attempts, and the
   citation graph for academic successors. A five-minute pass is recorded below
   as a starting hint only — redo it properly; it is your job, not settled.
3. **Measure the baseline**: how much of Stabilizer's benefit does
   `--randomize-section-padding` already deliver? See *The cheaper intermediate
   deliverable*. Worth having regardless of what happens to the port.
4. **Only if tasks 1–3 justify it**: a port plan, with each runtime risk
   assessed individually.

## Why anyone cares

Compiler and linker decisions — function order, section padding, stack frame
offsets, heap placement — move benchmark results by several percent, in either
direction, for reasons that have nothing to do with the change under test.
Mytkowicz, Diwan, Hauswirth and Sweeney, *"Producing Wrong Data Without Doing
Anything Obviously Wrong!"* (ASPLOS 2009), showed link order and
environment-variable size alone can be enough to invert a conclusion. So a
measured "3% faster" may be layout luck that an unrelated one-line edit
elsewhere would reshuffle.

Stabilizer (Curtsinger & Berger, ASPLOS 2013) attacks this by *repeatedly
re-randomising* the layout of code, stack frames and heap objects **at
runtime**. Two consequences, and the second is the important one:

1. Layout effects are averaged over rather than baked into one build.
2. Because the re-randomisation happens many times within a single execution,
   layout effects become **normally distributed**, which is what licenses
   ordinary parametric statistics (the paper uses ANOVA). This is the property
   no static/link-time approach gives you.

Claimed overhead: under 7% median. Paper:
<https://people.cs.umass.edu/~emery/pubs/stabilizer-asplos13.pdf>

Immediate motivation here: an AFL++ micro-optimisation
(`~/prog/aflpp-postprocess-memcpy/`) where a single-run comparison showed a
"3.7% improvement" from eliding a **one-byte** `memcpy` — i.e. entirely
artefact. That is the class of error this tooling exists to prevent.

Origin of the thread: Matthias's own HN comment,
<https://news.ycombinator.com/item?id=43318225> — "some benchmarking project
that deliberately randomised these compiler decisions, so that they could give
you more stable estimates of how well your code actually performed, and not
just how well you won or lost the linker lottery." The replies to it are worth
reading; they are where the LLD option below surfaced.

## Verified current state — facts, not impressions

Checked 2026-08-07 against the live repo and this machine:

- Upstream `ccurtsinger/stabilizer` README says outright: "This project is no
  longer being actively maintained, and only works on quite old versions of
  LLVM." Requires **LLVM 3.1**, GCC 4.6.2, Python 2.7.
- Last substantive commit to `pass/` or `runtime/` is **2013-05-20**. The 2021
  commits are LICENSE files only.
- 46 forks. A **five-minute pass** (sort by push date, eyeball) found none with
  meaningful modernisation: the only non-trivial post-2016 activity is
  `fusiled/stabilizer` (pushed 2018-11-21), and `emeryberger/stabilizer`
  (2022-06-23) looks trivial. **Treat this as a hint, not a result** — it is
  push dates, not diffs, and a fork could carry real work under an old
  timestamp. Task 2 is to do this properly. See `forks-survey.txt` for the raw
  listing that pass produced.
- No successor project is named anywhere in the README or on the PLASMA lab's
  `memory-landscape` index.

Repo shape: `pass/` (the LLVM pass: `Stabilizer.cpp`, `LowerIntrinsics.cpp`),
`runtime/` (`libstabilizer.cpp`, `Function.cpp`, `Heap.cpp`, `Trap.h`,
`Jump.h`, `Arch.h` — the runtime relocation machinery), `platforms/` (per-arch
makefiles, x86_64/i386/ppc, Linux and Darwin), `szc` (the compiler driver),
plus `run.py`/`process.py` in Python 2.

## The question you must answer first

**Is resurrecting Stabilizer the right way to get statistically sound
performance evaluation in 2026, or has the ground shifted under it?**

Do not assume the answer is yes because this repo is the one that got forked.
Candidates to evaluate, at minimum:

### 1. `lld --randomize-section-padding=<seed>`

Merged into LLVM 2024-12-13 (llvm/llvm-project PR #117653). From the commit
message: *"randomly inserts padding between input sections using the given
seed. It is intended to be used in A/B experiments to determine the average
effect of a change on program performance, while controlling for effects such
as false sharing in the cache which may introduce measurement bias."* Design
rationale RFC:
<https://discourse.llvm.org/t/rfc-lld-feature-for-controlling-for-code-size-dependent-measurement-bias/83334>

**Verified present on this machine**: `ld.lld --version` → LLD 22.1.8, and the
flag appears in `--help`.

This is maintained, in-tree, and needs no setup. But it is **link-time and code
layout only** — no stack frames, no heap, and crucially no *re*-randomisation
within a run, so it does not deliver Stabilizer's normality argument. You get
to average over layouts by building at K seeds and treating seed as a blocking
factor; you do not get "layout noise is Gaussian, therefore ANOVA".

### 2. Coz

Same lab, still maintained. **Answers a different question** — causal
profiling tells you which code to optimise, not whether your change made things
faster. Do not conflate the two. Worth evaluating only if you conclude the
useful deliverable is "what should I work on" rather than "did my change help".
If it is not the right basis, say so in one paragraph and move on.

### 3. Whatever else exists that we have not found

Search properly rather than trusting this brief's list. Specific things to
chase: BOLT and Propeller (layout *optimisers* — likely the wrong direction,
but confirm); anything in the SPEC/LLVM benchmarking-infrastructure world;
academic follow-ups citing the Stabilizer paper (use the citation graph, e.g.
Semantic Scholar / OpenAlex, not just web search); any Rust or newer-LLVM
reimplementation. Also check whether the LLVM community discussed a fuller
Stabilizer-like facility in the RFC thread above and rejected or deferred it —
**what was rejected and why is the most predictive evidence you can find**, and
the hardest to dig up.

## The cheaper intermediate deliverable — do this first

Rather than committing to a thirteen-year API port up front, **measure how much
of Stabilizer's benefit `--randomize-section-padding` already delivers.**

This is a smaller, self-contained piece of work; it is immediately useful to
anyone benchmarking today; and it is the evidence that tells us whether the
full port is worth doing at all. It is also a good baseline to have regardless
of what happens to the port — if Stabilizer is later revived, this is what it
has to beat.

Shape of it:

- Pick benchmarks with known layout sensitivity (the Mytkowicz paper's cases
  are a natural starting point; SPEC-like or smaller is fine — you do not need
  SPEC CPU2006 licences).
- Quantify the *layout-induced variance* under: (a) a single ordinary build,
  (b) K builds at K different `--randomize-section-padding` seeds, and, if you
  get it working, (c) Stabilizer proper.
- The interesting number is what fraction of the layout variance the LLD flag
  captures. If it captures most of it, the case for the full port weakens
  considerably and that is a publishable, useful finding on its own. If it
  captures little — e.g. because stack and heap placement dominate for the
  workloads tested — that is the argument *for* the port, with evidence.
- Report the residual: what the LLD flag structurally cannot control (stack
  frames, heap, intra-run re-randomisation and hence the normality argument).

A negative or deflationary result here is a good outcome, not a failure. See
the standing preference: a clean, well-established negative beats a marginal
positive scraped out of noise.

## If the port does go ahead — known hard parts

Do not estimate this as "update the API calls".

- The LLVM pass side (LLVM 3.1 → 20+) is tedious but tractable: opaque
  pointers, the new pass manager, `TargetMachine` and MC-layer churn,
  `IntrinsicLibcalls` almost certainly rotted.
- **The runtime is the load-bearing risk.** `Trap.h`, `Jump.h`, `Function.cpp`
  relocate *live* functions and stack frames during execution. That interacts
  with: modern ASLR and W^X / `PROT_EXEC` policy, control-flow integrity
  (CET/IBT, `endbr64`), position-independent executables by default, unwinder
  and `.eh_frame` assumptions, and thread-safety on a many-core machine. Any of
  these can be a hard block rather than a porting chore. Assess them **before**
  committing, and be willing to report "this cannot be revived as designed".
- Darwin and ppc support: almost certainly drop rather than port; say so.
- Python 2 tooling: rewrite, trivially, but do not let it distract.

## Deliverables, in order

1. `SCOPING.md` — the answer to "is this the right basis", with the alternatives
   evaluated, the fork survey confirmed or corrected, and a recommendation.
   Include the citation-graph search and any evidence of what upstream LLVM
   already rejected or deferred.
2. `BASELINE.md` — the measured LLD-flag-versus-nothing comparison described
   above, with real numbers, confidence intervals, and an explicit statement of
   what was *not* controlled.
3. Only then, if warranted, a port plan with the runtime risks assessed
   individually.

## House rules for this work

- Ground every claim in something measured or read at the source. Version
  numbers, flag availability and repo state go stale — re-verify rather than
  trusting this document, including the facts above.
- Never install into a shared package environment. Use a container (podman or
  docker, both available) or a project-local toolchain. A solver that decides
  to rearrange the system LLVM would be a bad day.
- Long options in scripts. Separate shell invocations rather than `&&`/`;`
  chains or pipes. `nice ionice` in front of anything that compiles.
- Commit as you go; small commits with reasons in the messages. Record *why* an
  approach was rejected, not just that it was — that is the part which is
  unrecoverable later.
- Nothing gets published, posted upstream, or pushed to a branch backing a PR
  without Matthias approving the text first.

## Related work in this tree

- `~/prog/aflpp-postprocess-memcpy/` — the AFL++ case that prompted this,
  including `FINDINGS.md`, which documents a measurement instrument that turned
  out to be invalid (queue-content comparison between runs) and how the control
  caught it. Worth reading as an example of the failure mode.
