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
3. **Get the original running in a period container** — LLVM 3.1, GCC 4.6.2,
   Python 2.7 era userspace. Far cheaper than porting, and it gives you the
   reference implementation that everything else is measured against. See
   *Running the original against period software*. Read the kernel caveat there
   before assuming a container is sufficient.
4. **Measure the baseline**: how much of Stabilizer's benefit does
   `--randomize-section-padding` already deliver, **on code built by today's
   clang and gcc**? See *The cheaper intermediate deliverable*. Worth having
   regardless of what happens to the port. Note task 3 does not simply extend
   this into a clean three-way comparison — period Stabilizer measures 2012
   compiler output, so the eras are confounded; read the caveat in task 3
   before designing the contrast.
5. **Only if tasks 1–4 justify it**: a port plan, with each runtime risk
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

## Running the original against period software

Before porting anything, try to make the original *work* in a container with
period-correct userspace: LLVM 3.1 (released May 2012), GCC 4.6.2, Python 2.7.
Ubuntu 12.04 LTS or Debian wheezy are the natural base images and are still
pullable. Both podman and docker are available on this machine; podman rootless
is the default choice.

Why this is worth doing first:

- It is far cheaper than a port, and it either produces a **working reference
  implementation of the technique** or tells you early that the design cannot
  run on a modern machine at all — which is itself the answer to the scoping
  question.
- It lets you reproduce the paper's own numbers as a correctness check on your
  harness before you trust it on anything new.
- If a port does happen, this is the oracle you differential-test against.

**Be clear about what it does NOT buy, because it is easy to oversell.**
Stabilizer compiles the program under test with *its own* toolchain, so
anything you measure in a period container is **2012 compiler output**. That
limits it in a way that goes to the heart of the project:

- Replicating "`-O3` over `-O2` is indistinguishable from noise" in the
  container replicates a fact about **LLVM 3.1**. It is a calibration of your
  harness and a historical check, not a result anyone benchmarking today needs.
  *The interesting modern version of that question — is `-O3` still noise on
  LLVM 22? — cannot be answered without a tool that works on modern output.*
- People want to know whether their benchmark of a change to code built with
  **today's** clang or gcc is trustworthy. Modern output differs in ways that
  bear directly on layout sensitivity: different inlining and vectorisation,
  LTO, PGO, and layout optimisers such as BOLT and Propeller. A 2012 binary is
  not a proxy for that.
- **This is a confound in the three-way comparison, so do not run it naively.**
  "LLD flag on clang 22 output" versus "Stabilizer on LLVM 3.1 output" compares
  two techniques applied to *different programs*, and any difference is
  uninterpretable. If you want a clean contrast, either hold the compiler fixed
  within each comparison (LLD-flag-versus-nothing on modern output; Stabilizer's
  own `link`-versus-`code`/`stack`/`heap` contrast on period output, which the
  original harness already gives you) and compare the *ratios* rather than raw
  numbers, or state plainly that the cross-era comparison is qualitative.

Which cuts both ways for the scoping decision, and you should say so explicitly
in `SCOPING.md`: the LLD flag's decisive practical advantage is that it works
on modern toolchains **today**, whatever its statistical limitations; and the
strongest argument *for* the port is precisely that no period-container result
can speak to modern compiler output.

**The caveat that decides whether this works: a container gives you a period
*userspace*, not a period *kernel*.** Stabilizer's runtime relocates live code
and stack frames using `mmap`/`mprotect` and trap-based patching (`Trap.h`,
`Jump.h`, `Function.cpp`), and that machinery interacts with kernel-side and
CPU-side policy that a container does not roll back: W^X enforcement, CET/IBT
shadow stacks and `endbr64` requirements on recent CPUs, `vm.mmap_min_addr`,
ASLR behaviour, and any newer `seccomp`/hardening defaults. Old GCC and old
LLVM you get for free; a 2012 kernel you do not.

So: if it fails, **diagnose whether the blocker is userspace or kernel/CPU
before concluding anything**. A userspace failure is a build problem. A
kernel-or-CPU failure is a finding — it means the original technique needs
rework to run on 2026 hardware regardless of LLVM version, and that reshapes
the port estimate substantially. If you land there, a VM with a period kernel
is the next rung, and worth it only to establish the reference numbers.

Record what you tried and what the failure mode was either way. "It did not
build" is not a useful note; "it built, and died in `Function::relocate` with
SIGSEGV on an `endbr64`-guarded target" is.

## Mine the original benchmarks and experimental design — they are in this repo

Do not invent an evaluation from scratch. The original work came with a full
experimental apparatus, and most of it is sitting in this checkout.

**`tests/` vendors three SPEC CPU2006 benchmarks in full, with their input
sets**: `bzip2` (plus `input.combined`), `libquantum`, and `perlbench` (with
`checkspam.pl`, `diffmail`, `perfect`, `scrabbl`, `splitmail` inputs and a
bundled Perl library tree). Also `HelloWorld` and `Context` — the latter has a
`stub.asm` and is presumably a runtime context-switch test. **This matters
practically**: SPEC CPU2006 is licensed and you should not assume access, but
these three give a licence-free partial replication out of the box.

**`run.py` encodes the paper's factorial design.** The configs are:

    code, code.stack, code.heap.stack, stack, heap.stack, heap, link

That is the ablation over *which* layout components get randomised
(`szc -Rcode -Rstack -Rheap` in any combination), crossed with optimisation
level (`O0`/`O1` via `szclo.cfg`, `O2`/`O3` via `szchi.cfg`, mapped to SPEC
base/peak tuning) and input size (test/train/ref).

**The `link` config is the one to look at hardest.** It is special-cased in
`run.py` to rebuild the binary on every iteration and take a single run each,
i.e. it is *static, per-build* layout randomisation — the direct conceptual
analogue of `lld --randomize-section-padding`. The other configs are
Stabilizer's *runtime re-randomisation*. **So the comparison this brief asks
you to make already exists as a contrast in the original design**: `link`
versus `code`/`stack`/`heap`. Reuse that structure rather than inventing one,
and you also get to check your results against the paper's.

**`process.py` already does the statistics**, including the normality testing
that the entire methodological argument rests on: `scipy.stats.shapiro`
(Shapiro-Wilk) and `anderson`, behind the `-norm` flag, plus `-trim` and
`-all`. It is Python 2 and parses SPEC `.rsf` files, so it needs rewriting, but
the analysis logic is the specification.

**A sharp, falsifiable prediction to test.** Stabilizer's normality claim comes
specifically from re-randomising *within* a run. The LLD flag randomises once
per build. So the prediction is that per-build LLD-padding samples need **not**
be Gaussian, while Stabilizer's within-run samples should be — and Shapiro-Wilk
on both, using the original `-norm` machinery, decides it. If LLD-flag samples
turn out normal anyway, much of the argument for the port collapses and you
have a clean, cheap finding. If they are not, you have quantified exactly what
the port would buy. Either way this is a better experiment than "is Stabilizer
faster", and it is the one most likely to settle the question.

**The paper's headline result is the obvious replication target**: that `-O2`
significantly beats `-O1`, while *"the performance impact of `-O3` over `-O2`
optimizations is indistinguishable from random noise"*. See also fgiesen's
write-up, linked from the README.

Distinguish two versions of that, because they are worth very different things.
Reproducing it **on LLVM 3.1 in the period container** validates your harness
against a published number — useful, but it is a fact about a 2012 compiler.
Asking it **of a 2026 LLVM** is the result people would actually act on, and it
is a genuine contribution: fourteen years of optimisation work later, is `-O3`
still indistinguishable from noise? That version requires a tool that works on
modern compiler output, which is exactly the capability under scoping here. If
the answer to task 1 is "the LLD flag is enough", then run it with the LLD flag
and publish that; if it is not enough, this experiment is the concrete thing
the port would unlock, and should be named as such in the justification.

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
