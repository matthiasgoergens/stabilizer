# Alternatives to resurrecting Stabilizer — independent sweep

Scoping question: in 2026, what are all the credible ways to control or average
over layout-induced measurement bias (code layout, stack frames, heap
placement) when benchmarking a code change? Baseline known already:
`lld --randomize-section-padding` (per-build, code sections only) and Coz
(different question — causal profiling, not layout control).

This note covers everything else found by direct web search / `curl` / `gh`,
organised by the five angles given. It deliberately does **not** re-do the
citation-graph crawl of papers citing Stabilizer — that's another agent's job.

Stabilizer itself, for reference: randomizes the placement of **functions,
stack frames, and heap objects**, and does so **repeatedly during a single
run** (via periodic re-randomization at safe points), not just once per
process launch. That combination — three layout dimensions, all randomized
repeatedly within one execution — is the bar every alternative below is
measured against. Upstream (`ccurtsinger/stabilizer`) has not been pushed to
since **2021-09-29**; two forks (`magras/stabilizer-fork`,
`Dead2/stabilizer`) got it building on newer toolchains as of **2023**, but
neither has moved since. No fork or independent project has replaced it.

---

## 1. ASLR as a free randomizer

Linux ASLR (`randomize_va_space=2`, with a PIE binary) randomizes, **once per
process launch**: the load base of the executable, the base of each shared
library, the stack base, the heap base (`brk`), and the base of each `mmap`
region. Run the binary N times and you get N samples of those four base
offsets.

**What it does NOT randomize**, precisely — this is the crux of why it's a
partial substitute, not a full one:

- **Relative layout within the binary.** Function-to-function offsets,
  section order, and padding between symbols are fixed by the linker at
  build time. ASLR moves the whole `.text` segment around as a block; it
  never reorders or re-pads what's inside it. Two functions that are 64
  bytes apart in the binary stay 64 bytes apart in every run, so cache-line
  and prefetcher interactions between them are invariant across ASLR draws.
- **Stack frame internal layout.** Local variable ordering, padding, and
  register-spill slots within a single function's frame are fixed by the
  compiler for a given build. ASLR randomizes where the *stack itself*
  starts, not the internal shape of any one frame.
- **Heap allocation order / relative object placement.** The base of the
  heap arena moves, but the sequence in which a fixed allocator (glibc
  malloc, jemalloc, tcmalloc, mimalloc — none of which are randomizing by
  design) hands out addresses for a given call sequence is deterministic.
  Object A ends up at the same offset from object B on every run.
- It's also **per-run, not within-run**: once the process starts, layout is
  fixed until it exits and a new process draws a fresh set of bases. This is
  exactly the gap Stabilizer's periodic re-randomization at runtime was
  built to close — a single execution is "one sample from the space of
  program layouts, regardless of the number of runs" within that process,
  as the Stabilizer project page puts it.

**Practitioner consensus on disabling ASLR for benchmarking is mixed but
leans toward "disable it, but say so."** Both LLVM's own benchmarking guide
(`llvm.org/docs/Benchmarking.html`) and Google Benchmark's variance-reduction
doc explicitly recommend disabling ASLR (`echo 0 >
/proc/sys/kernel/randomize_va_space`, or per-run via `setarch $(uname -m) -R
./binary`) as one line item in a broader noise checklist (turbo boost,
frequency scaling, CPU pinning, SMT). Google Benchmark even ships
`benchmark::MaybeReenterWithoutASLR(argc, argv)` to do this from inside
`main()` (falls back gracefully under seccomp/Docker where the re-exec is
blocked). The rationale given is the opposite of Stabilizer's: disabling
ASLR is presented as a way to get **reproducible** (not statistically valid)
numbers for a single machine/CI run, accepting that this specific run may not
be representative of the population of possible layouts. No methodology paper
found that recommends *leaving ASLR on across many runs* as a deliberate
substitute for Stabilizer-style randomization and then doing the statistics
properly (see hyperfine finding under §4, which is the closest thing to this
in practice, and is not literally ASLR — it's environment-variable padding,
which perturbs the same downstream address space via a different knob).

**Verdict:** ASLR + many runs gives you free per-run randomization of four
base addresses. It is a real, zero-effort partial substitute for the
*between-run* component of Stabilizer's design, but it structurally cannot
reach the *within-binary* and *within-run* effects Stabilizer targets, and no
one appears to be running it that way as a rigorous substitute rather than as
noise to be turned off.

Sources: [Network World — how ASLR protects Linux](https://www.networkworld.com/article/966844/what-does-aslr-do-for-linux.html), [Ubuntu security docs — ASLR](https://documentation.ubuntu.com/security/security-features/process-memory/aslr/), [Google Benchmark — reducing_variance.md](https://google.github.io/benchmark/reducing_variance.html), [LLVM Benchmarking tips](https://llvm.org/docs/Benchmarking.html), [Stabilizer project page](https://people.cs.umass.edu/~emery/plasma/emery/stabilizer.html).

---

## 2. Benchmarking harnesses with statistical rigour

| Tool | Layout control? | Per-run / within-run | Maintained | Notes |
|---|---|---|---|---|
| **hyperfine** (sharkdp) | **Yes, deliberately.** Sets `HYPERFINE_RANDOMIZED_ENVIRONMENT_OFFSET` to a random-length string (0–4095 bytes) in the child's environment on every iteration, since v1.10.0 (2020). This is the Mytkowicz "environment size" trick, done automatically and unconditionally. | Per-run (new env var length each process launch, not mid-run) | Actively maintained | This is the single clearest example found of a mainstream tool bundling a Mytkowicz-lineage bias control by default. See §4 for the exact history — it's directly traceable to the ASPLOS'09 paper. It does **not** touch code/stack/heap layout directly; it perturbs them only as a side effect of environment size changing the initial stack pointer / argv-env block size. Confirmed effect size was inconclusive even to hyperfine's own author (see quoted PR discussion in §4) — this is a cheap knob, not a proven-adequate one. |
| **temci** (parttimenerd) | No layout randomization; does the opposite — **actively disables** ASLR (`disable_aslr` plugin) as part of its "make results reproducible" preset, alongside disabling hyperthreading, Intel turbo, and pinning via `cpuset`. | N/A (it's a controller, not a randomizer) | Last known activity dated; project by a single grad-student author, community-maintained fork status unclear as of this sweep | Represents the opposite philosophy to Stabilizer: eliminate variance rather than sample it and average over it. Useful as a complement (control everything *except* the dimension you want to average over), not a substitute. |
| **Krun** (Barrett, Bolz-Tereick et al., "VM Warmup Blows Hot and Cold", PACMPL 2017) | No explicit layout randomization found in the paper or `softdevteam/warmup_experiment` repo. Its contribution is statistical: changepoint analysis to detect whether/when a VM benchmark reaches steady state, rather than blindly discarding a fixed warmup prefix. | N/A | Research artifact tied to the paper; not a general-purpose tool people install today | Orthogonal axis to layout bias — addresses temporal (warmup) non-stationarity, not spatial (layout) bias. Complementary, not competing. |
| **ReBench** (Stefan Marr) | No layout control found; it's an orchestration/recording layer that wraps *other* harnesses via "gauge adapters" and records results reproducibly (YAML config, versioned data files). | N/A | Actively used in academic VM-benchmarking circles (used for Krun-adjacent and truffle/graal work) | Could *drive* a Stabilizer-equivalent if one existed, e.g. by varying `--shuffle-sections` seeds across runs and recording each as a documented experiment — but has no such integration today. |
| **Bencher.dev** | No layout control; its answer to noise is **dedicated bare-metal CI runners** plus change-point-detection analytics on the resulting time series, so "when a number moves, it's real." | N/A | Actively maintained, VC-backed product (2024–2026) | Addresses a different bias source (shared/noisy CI hardware), not layout. Does not shuffle or average over layout at all — it tries to make the single measured layout as stable and comparable as possible over time instead. |
| **Google Benchmark** | Disables ASLR by default via `MaybeReenterWithoutASLR`, as covered in §1. No further layout control. | Per-run (once, at process start) | Actively maintained | Documents the *rest* of the noise checklist (governor, turbo, taskset, SMT) well; treats layout purely as noise to eliminate. |
| **Rust criterion.rs** | None found. Pure statistics: runs thousands of iterations, models noise, does outlier classification via a Tukey's-method variant (IQR fences). No mention of ASLR, stack, or heap randomization in its docs or source search. | N/A | Actively maintained (`bheisler/criterion.rs`) | A within-process microbenchmark harness — for in-process loops, "layout" is largely fixed for the life of the process anyway (no re-exec between iterations), so it structurally can't apply a Stabilizer-style fix even if it wanted to; the closest lever available to it would be per-iteration heap shuffling, which it doesn't do. |
| **perflock** (Austin Clements, Go team) | No layout control; a locking wrapper (exclusive/shared modes) to stop concurrent jobs on a shared benchmarking host from perturbing each other. | N/A | Maintained under `aclements/perflock`, used inside the Go project's own perf tooling | Complementary "environment discipline" tool, same family as temci's cpuset control and cset shield — see §4. |

**Verdict:** hyperfine is the one mainstream harness that has adopted a
Mytkowicz-lineage layout perturbation as a default, and it is directly
citable to the source paper. Every other harness surveyed either (a) treats
layout purely as noise to be eliminated (temci, Google Benchmark, LLVM docs),
or (b) is statistically rigorous about a different axis entirely (warmup
non-stationarity for Krun, environmental noise for Bencher, distributional
outliers for criterion.rs) without touching layout at all. None reimplements
Stabilizer's "randomize repeatedly within a single run and treat layout as a
population to sample" model.

Sources: [hyperfine changelog](https://raw.githubusercontent.com/sharkdp/hyperfine/master/CHANGELOG.md), [hyperfine issue #235](https://github.com/sharkdp/hyperfine/issues/235), [hyperfine PR #241](https://github.com/sharkdp/hyperfine/pull/241), [temci docs](https://temci.readthedocs.io/en/latest/temci_exec.html), [temci repo](https://github.com/parttimenerd/temci), [Krun/warmup paper](https://arxiv.org/abs/1602.00602), [warmup_experiment repo](https://github.com/softdevteam/warmup_experiment), [ReBench](https://rebench.readthedocs.io/en/latest/), [ReBench repo](https://github.com/smarr/ReBench), [Bencher.dev](https://bencher.dev/), [Bencher explanation of continuous benchmarking](https://bencher.dev/docs/explanation/continuous-benchmarking/), [Google Benchmark reducing_variance](https://google.github.io/benchmark/reducing_variance.html), [criterion.rs](https://github.com/bheisler/criterion.rs), [criterion.rs analysis docs](https://bheisler.github.io/criterion.rs/book/analysis.html), [perflock](https://github.com/aclements/perflock).

---

## 3. Layout-manipulation tools usable in reverse

| Tool / mechanism | What it randomizes | Confirmed capability | Maintained | Effort | Substitutes for |
|---|---|---|---|---|---|
| **`lld --shuffle-sections=<seed>`** | Code/data section *order* within the output (as opposed to `--randomize-section-padding`, which only inserts gaps). Confirmed exact semantics from the man page: `-1` reverses order, `0` picks a random seed, any other value is a deterministic seed. Directly credits "the idea inspired by... Producing Wrong Data Without Doing Anything Obviously Wrong!" per the LLVM review (D74791). | Yes — real flag, shipped in lld, added specifically for benchmarking reproducibility research (used to compile SPEC-style benchmarks under 99 pseudorandom layouts plus one default, per a linker-benchmarking reproducibility writeup found via LLVM Discourse). | Actively maintained (mainline LLVM/lld) | Trivial — one link-time flag, no source changes | Stabilizer's **code layout** dimension, but only **per-build/per-link**, not per-run and never within a single execution. You'd need to build N binaries with N seeds and treat each as a sample, which is exactly the workflow the discourse thread describes. |
| **`lld --randomize-section-padding=<seed>`** | Padding between sections in `.bss/.data/.rodata/.text*` etc. (already known baseline, included here for completeness/contrast with `--shuffle-sections`). | Confirmed via man page. | Same as above | Trivial | Code layout, per-build only — narrower than `--shuffle-sections` (padding only, not reordering). |
| **BOLT** (LLVM, ex-Facebook) | Code layout, but **only as an optimizer** — its reordering passes (`-reorder-functions=hfsort/cdsort`, `-reorder-blocks=cache+/ext-tsp`) are all profile-guided and deterministic given the same profile. Searched specifically for a `random` mode; none found in current docs/flags. | **Not confirmed as a randomizer.** BOLT is a one-way tool: it makes layout *better* for a specific workload, not different-but-equivalent for sampling purposes. | Actively maintained (mainline LLVM) | N/A — wrong tool for this job | Does not substitute for anything here; noted because the task explicitly asked to check. |
| **Propeller** (Google) | Code layout via "basic block sections" — a linker abstraction that lets each basic block be placed independently, driven by a profile (`Ex-TSP` algorithm). | Randomization is possible but **not built in**: per a DeepWiki summary of the `google/autofdo` docs, "a symbol ordering file with basic block sections can do random orderings without invoking Propeller" — i.e. you'd generate your own randomized ordering file and feed it to the linker, bypassing Propeller's optimizer entirely. Propeller itself has no `--random` flag found. | Actively maintained (Google, in production) | Moderate — requires `-ffunction-sections`/basic-block-sections build support plus a hand-written random symbol-order-file generator | Same ceiling as `--shuffle-sections`: per-build code layout only, and requires more build-system integration (basic block sections) than lld's flag for no extra randomization power. |
| **Symbol order files** (BFD linker scripts, gold plain-list files, LLD `--symbol-ordering-file`) | Code layout, at whatever granularity the section split allows (function-level by default, block-level with basic-block-sections). | Generic linker feature, not benchmarking-specific; a script that emits a shuffled symbol list is straightforward to write. | N/A (linker feature, always available) | Low-moderate — need `-ffunction-sections` and a generator script | Same ceiling as above; more portable across gold/BFD/LLD than lld-specific flags, at the cost of writing your own shuffler. |
| **DieHard** (Berger et al., same lab as Stabilizer) | **Heap only**: randomizes object placement within a heap sized at ≥2x the working set, to get probabilistic memory-safety guarantees as a side effect. | Confirmed alive: `emeryberger/DieHard` on GitHub, `pushed_at: 2026-04-26` (this year) — **actively maintained**, 423 stars, Apache-2.0, per-thread heap variant exists. | **Actively maintained** (contradicts the common assumption that 2013-era Berger-lab tools are all dead) | Low — drop-in `LD_PRELOAD` allocator | Stabilizer's **heap layout** dimension, per-run (new random placement on each process start, and randomized on each `malloc` call within a run per its design — closer to Stabilizer's "repeated within-run" model than anything else surveyed here, though for heap only, not code/stack). |
| **DieHarder** (Novark & Berger, USENIX WOOT'11) | Heap, hardened successor to DieHard — "adapts many protections used by the OpenBSD allocator, but improves upon randomized placement and randomized reuse by employing the randomization mechanism of DieHard," plus sparse virtual-address utilization. | Research-paper artifact; did not independently verify a maintained public repo distinct from DieHard in this sweep. | Unclear — not separately confirmed | — | Same heap dimension as DieHard, stronger security framing |
| **Scudo** (LLVM hardened allocator) | Heap: randomizes region start addresses in its Primary allocator and shuffles block order within `TransferBatch`es; can randomize per-thread cache assignment. | Confirmed via LLVM docs (`llvm.org/docs/ScudoHardenedAllocator.html`) | Actively maintained (ships with LLVM/Android/Fuchsia) | Low — `LD_PRELOAD` or link-time swap | Heap layout, per-run; production-grade and much more actively used than DieHard/DieHarder today, though its randomization is a security side-effect, not tuned for benchmarking statistics |
| **hardened_malloc** (GrapheneOS) | Heap: guard pages, slab randomization, padding | Confirmed via project docs | Actively maintained (GrapheneOS project) | Low — drop-in allocator, but adds real overhead (it's optimizing for exploit mitigation, not speed) | Heap layout, per-run; heaviest overhead of the allocator options here |
| **jemalloc / tcmalloc / mimalloc** | None by default — these are deterministic, arena/size-class-based allocators optimized for speed, not randomized | N/A | All actively maintained | Low | **Not** a substitute — included to make the contrast explicit: swapping allocators changes layout *once*, deterministically, giving you one more sample point, not a distribution. Useful only as an extra fixed data point (e.g. "does the regression hold under three different allocators"), not as an averaging mechanism. |
| **gold `--sort-section`** | Section ordering (name/alignment-based sort), a gold-linker-specific ordering knob | Exists as a documented gold feature but a shuffling/random mode was **not confirmed** in this sweep — treat as unverified | gold itself is in maintenance mode generally (binutils) | — | Unconfirmed; lld's `--shuffle-sections` is the better-evidenced option |

**Verdict:** the linker tools (`--shuffle-sections`, symbol order files, basic
block sections + hand-rolled shuffler) cover Stabilizer's **code layout**
axis, but only per-build — you must produce N binaries, not get N samples
from one. The allocator substitution family (DieHard, DieHarder, Scudo,
hardened_malloc) covers the **heap** axis and, notably, DieHard is the one
piece of 2013-vintage Berger-lab infrastructure confirmed still actively
maintained as of April 2026 — worth flagging given the whole point of this
scoping exercise is whether resurrecting *Stabilizer* specifically is
warranted. Nothing found covers the **stack frame** axis at all outside
Stabilizer itself — no tool surveyed randomizes stack frame layout,
per-build or per-run.

Sources: [D74791 — add --shuffle-sections to lld](https://reviews.llvm.org/D74791), [ld.lld man page](https://man.archlinux.org/man/extra/lld/ld.lld.1.en), [LLVM Discourse — improving reproducibility of linker benchmarking](https://discourse.llvm.org/t/improving-the-reproducibility-of-linker-benchmarking/86057), [BOLT README](https://github.com/llvm/llvm-project/blob/main/bolt/README.md), [Propeller ASPLOS'23 paper](https://dl.acm.org/doi/10.1145/3575693.3575727), [Propeller overview — DeepWiki](https://deepwiki.com/google/autofdo/7.1-propeller-overview), [Red Hat — practical guide to linker section ordering](https://developers.redhat.com/articles/2024/06/13/practical-guide-linker-section-ordering), [DieHard GitHub](https://github.com/emeryberger/DieHard) (repo metadata fetched directly, `pushed_at: 2026-04-26`), [DieHarder WOOT'11 paper](https://www.usenix.org/legacy/event/woot11/tech/final_files/Novark.pdf), [Scudo docs](https://llvm.org/docs/ScudoHardenedAllocator.html), [hardened_malloc experience report](https://dan-kir.github.io/2022/05/22/Experimenting-with-Hardened_malloc.html).

---

## 4. Environment-bias controls (Mytkowicz lineage)

The ASPLOS'09 paper ("Producing Wrong Data Without Doing Anything Obviously
Wrong!", Mytkowicz, Diwan, Hauswirth, Sweeney) is the direct ancestor of
Stabilizer (same measurement-bias problem; Stabilizer is the "fix it
properly" response, this paper is "detect it and/or randomize your way past
it cheaply"). Its two proposed remedies: **causal analysis** (to detect bias)
and **setup randomization** (to avoid it) — and it found that of 133 papers
surveyed from ASPLOS/PACT/PLDI/CGO, none adequately controlled for
measurement bias.

**Direct confirmed lineage into a shipping tool — hyperfine.** Traced this
end to end via GitHub: issue `sharkdp/hyperfine#235` (opened 2019-10-20) cites
the Mytkowicz paper by name and PDF link, and proposes "inserting a value
with a random length of say 0-4000 bytes into the environment." The
maintainer (sharkdp) replied he'd seen the same paper referenced in a
Strange Loop talk, built a prototype the same week, and shipped it as PR
`#241`, released in **hyperfine v1.10.0** (2020), extended to Windows in
v1.13.0. The mechanism: set `HYPERFINE_RANDOMIZED_ENVIRONMENT_OFFSET` to a
string of random length (0–4095 bytes, via `"X".repeat(rand::random::<usize>()
% 4096)`) in the child process's environment before every timed invocation.
Worth being honest about a finding from the same thread: **the maintainer
himself couldn't find a benchmark where this produced a clearly detectable
effect** ("I couldn't yet find a conclusive example program that experiences
a large change in execution time... it *looks* like the bottom distribution
is slightly wider... but it's hard to tell"). So this is a real, well-sourced,
shipped implementation of the Mytkowicz technique — but its own author never
established it does much in practice, on the examples he tried.

**Standard checklist tools, none doing layout-specific averaging — all doing
elimination instead:**

- **temci** — bundles `disable_aslr`, `disable_ht` (hyperthreading),
  `disable_intel_turbo`, and `cpuset` as a single "preset" combination,
  usable with one flag when run as root.
- **pyperf `system tune`** — the most complete single-command checklist
  found: disables Turbo Boost, sets scaling governor to `performance`,
  checks/enforces `randomize_va_space` full-randomization (yes — it checks
  ASLR is **on** and correctly configured as `2`, notably the opposite of
  the "disable it" advice from LLVM/Google Benchmark docs above — pyperf is
  reasoning about the *process's own* Python-level warm-up stability rather
  than cross-run reproducibility of C/C++ layout), sets/validates
  `isolcpus=` and `rcu_nocbs=` kernel boot parameters, pins worker processes
  to isolated CPUs, and stops `irqbalance` during runs.
- **`cset shield`** (cpuset) — `cset shield -c N1,N2 -k on` moves all
  threads (including kernel threads with `-k on`) off the named CPUs, then
  `cset shield --exec -- <cmd>` runs the benchmark exclusively on the
  shielded set. Standard tool referenced by the LLVM benchmarking doc,
  easyperf.net, and BenchmarkTools.jl's Linux tips page independently.
- **perflock** (§2) — host-level mutual exclusion so concurrent jobs on a
  shared benchmarking machine don't perturb each other; used inside Go's own
  benchmark tooling.
- **LLVM's own `Benchmarking.html`** and **Google Benchmark's
  `reducing_variance.md`** are the two most-cited canonical checklists;
  both independently arrived at the same list (ASLR, turbo, governor,
  `taskset`/`cset`, SMT, static linking, background processes) — this
  appears to be genuinely convergent, field-wide consensus rather than one
  document being copied.

**Verdict:** the checklist side of Mytkowicz's lineage is thoroughly
tooled and widely adopted (temci, pyperf, cset, perflock, and both major
compiler projects' own docs converge on the same list). The *averaging*
side (setup randomization as a statistical technique, not just an
elimination checklist) has exactly one confirmed, well-documented,
shipped implementation — hyperfine's environment-offset trick — and even
its author is candid that its effect size is unproven on the examples he
tried. No tool found does anything resembling Mytkowicz's "causal analysis"
half (detecting *which* environmental factor is biasing a specific result)
as a packaged, reusable capability.

Sources: [Mytkowicz et al., ASPLOS'09 PDF](https://www.inf.usi.ch/faculty/hauswirth/publications/asplos09.pdf), [hyperfine issue #235](https://github.com/sharkdp/hyperfine/issues/235) (fetched directly via `gh issue view`), [hyperfine PR #241](https://github.com/sharkdp/hyperfine/pull/241), [hyperfine CHANGELOG](https://raw.githubusercontent.com/sharkdp/hyperfine/master/CHANGELOG.md), [temci docs](https://temci.readthedocs.io/en/latest/temci_exec.html), [pyperf system-tune docs](https://pyperf.readthedocs.io/en/latest/system.html), [pyperf system.rst source](https://github.com/psf/pyperf/blob/main/doc/system.rst), [testbit.eu — cpuset profiling isolation](https://testbit.eu/2023/cgroup-cpuset), [easyperf — consistent Linux benchmarking results](https://easyperf.net/blog/2019/08/02/Perf-measurement-environment-on-Linux), [LLVM Benchmarking tips](https://llvm.org/docs/Benchmarking.html), [perflock](https://github.com/aclements/perflock).

---

## 5. Modern reimplementations and security-world re-randomization

**Rust/Go/general reimplementations — none found.** Searched crates.io,
GitHub, and general web for a Stabilizer-equivalent runtime layout
re-randomizer in any modern language ecosystem (2024–2026). Nothing came
up beyond the original C++/LLVM-pass codebase and its two 2023-frozen forks
(`magras/stabilizer-fork`, `Dead2/stabilizer` — both confirmed via GitHub API,
`pushed_at` 2023-07-26 and 2023-08-15 respectively, neither touched since).
Rust's `criterion` and equivalents in other ecosystems (§2) do not attempt
this. No LLVM pass, Rust compiler plugin, or Go runtime patch doing
Stabilizer-style repeated in-process re-randomization was found.

**Security-world runtime re-randomization — real, related, but built for a
different threat model and largely dormant as maintained software:**

| Project | What it randomizes | Frequency | Overhead reported | Status |
|---|---|---|---|---|
| **Shuffler** (Williams-King et al., OSDI'16) | Code locations, code pointers, data pointers | Continuous, ~50 ms period | 14.9% | Research artifact; no evidence of an actively maintained public tool found in this sweep |
| **TASR** ("Timely Address-Space Randomization") | Re-randomizes at "sensitive" system calls rather than on a fixed timer | Event-triggered | Reported low by its authors, but a follow-up analysis found ~30-40% additional overhead attributable to the `-Og` (not `-O2`) build the original evaluation used — i.e. the low-overhead claim doesn't hold at production optimization levels | Research artifact; not found as maintained software |
| **Morpheus** (Gallagher et al., ISCA'19) | Code pointers and data pointers, at the *hardware* level (a RISC-V-derived secure architecture, not a software tool) | ~10 ms period | ~5% | Academic hardware prototype, not something you can point at an existing x86/ARM binary at all |

**Why these don't repurpose cleanly for measurement:** all three optimize for
a security property — outrunning an attacker's reconnaissance-then-exploit
window — not for producing a clean statistical sample. Re-randomization
period is tuned to milliseconds for security reasons, which is far more
frequent than a benchmark needs (Stabilizer re-randomizes at safe points
tied to program structure, not a wall-clock timer), and none of the three
publish the layout metadata a benchmarking harness would need to correlate
"this randomization state" with "this measured interval." Morpheus in
particular is a hardware architecture, not deployable software. Repurposing
any of them would mean substantially rewriting the re-randomization trigger
and adding measurement instrumentation — closer to "write a new tool
inspired by the threat model" than "reuse existing software."

**Verdict:** nothing in the security re-randomization literature is a
drop-in or even a moderate-effort substitute. They're the closest *prior
art* to Stabilizer's actual mechanism (periodic in-process layout churn) of
anything surveyed in this entire sweep, which makes them useful as design
references if a resurrection or reimplementation is attempted, but none is
usable as-is.

Sources: [Shuffler OSDI'16 paper](https://www.cs.columbia.edu/~junfeng/papers/shuffler-osdi16.pdf), [Shuffler USENIX page](https://www.usenix.org/conference/osdi16/technical-sessions/presentation/williams-king), [Morpheus ISCA'19 paper](https://shibo-chen.github.io/publication/gallagher-morpheus-2019/gallagher-morpheus-2019.pdf), [magras/stabilizer-fork](https://github.com/magras/stabilizer-fork) (repo metadata fetched directly), [Dead2/stabilizer](https://github.com/Dead2/stabilizer) (repo metadata fetched directly), [ccurtsinger/stabilizer](https://github.com/ccurtsinger/stabilizer) (repo metadata fetched directly, `pushed_at: 2021-09-29`).

---

## What I could not determine

- **gold `--sort-section`**: I found references to gold having a section-sort
  capability but could not confirm whether it supports a random/shuffle mode
  as opposed to a fixed sort key (name/alignment). Treat as unverified;
  `lld --shuffle-sections` is the better-evidenced tool for this purpose and
  there's little reason to prefer gold here regardless.
- **DieHarder's current maintenance status** as a project distinct from
  DieHard — I found the original USENIX WOOT'11 paper and characterizations
  of it in later allocator-security literature, but did not independently
  locate and check a canonical, currently-maintained public repository the
  way I did for DieHard itself (confirmed via GitHub API).
  Also skipped: OpenBSD `malloc`'s own randomization features, which are
  mentioned as DieHarder's inspiration but were not directly investigated as
  a standalone benchmarking tool.
- **Quantitative effect size of hyperfine's environment-offset trick.** The
  only data point found is the author's own inconclusive experiment in
  issue #235 (2019/2020) — "the bottom distribution is slightly wider... but
  it's hard to tell." No later, larger-scale evaluation of whether this
  default actually changes measured variance in practice was found. This
  matters: it's the single clearest "tool bundles Mytkowicz's technique"
  example in the whole sweep, but its efficacy is essentially unvalidated by
  its own author, and I found no independent validation either.
  Complicating any future attempt to validate it: hyperfine also runs a
  configurable number of warmup iterations before timing starts, which is a
  large *known* confound sitting right next to this *unvalidated* one — any
  attempt to isolate the offset trick's effect would need to control for
  warmup count too.
- **Whether ASLR-plus-many-runs has ever been formally evaluated against
  Stabilizer** on the same benchmark suite, to get an actual quantitative
  answer to "how much of Stabilizer's effect do you recover for free." I
  did not find a paper or practitioner writeup doing this A/B comparison
  directly — the ASLR-for-benchmarking material I found is all either
  "how to disable it" (LLVM/Google Benchmark docs) or general ASLR
  explainer content, never a head-to-head against Stabilizer-style
  re-randomization. This is a real gap and the citation-crawl agent's
  results may fill it if any paper doing exactly this comparison cites
  Stabilizer.
- **BOLT/Propeller "hidden" random modes.** I checked documented flags and
  found none, but did not exhaustively grep either project's source for
  undocumented `-help-hidden` options (Propeller/BOLT both have hidden flag
  categories). A source-level check would be needed to fully close this off
  rather than rely on docs/search alone.
- **Whether any commercial/internal-only continuous-benchmarking platform
  (beyond the public ones checked — Bencher.dev) does layout randomization.**
  I did not have access to internal tooling at large companies (Google's,
  Meta's, or others' internal perf infrastructure) that might do more than
  what's published in the Propeller/BOLT/AutoFDO papers.
