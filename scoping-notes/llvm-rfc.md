# LLVM/lld community position on layout randomisation for measurement — archaeology

Sources fetched and saved under this directory: `rfc-topic.json` / `rfc-thread-plaintext.txt`
(Discourse RFC thread, all 9 posts — the thread is short enough that Discourse's
first-page payload already contains every post, no pagination was needed),
`pr-117653-*.json` (GitHub PR #117653: body, 32 inline review comments, 10
issue-level comments), `phab-D74791.html` / `phab-D74791-comments.txt` (original
2020 Phabricator review that added `--shuffle-sections`), `commit-*.txt` /
`git-log-*.txt` / `git-pickaxe-*.txt` (git archaeology in the local
`~/prog/llvm-project` checkout), `run_benchmark.py` (the actual measurement
harness that shipped as a follow-up), `discourse-search-*.json` (site-wide
Discourse searches for "Stabilizer", "measurement bias", "shuffle-sections"),
and `maskray-hyrums-law.html` / `-plaintext.txt` (a 2026 blog post by the lld
maintainer who reviewed the PR, independently corroborating the seed
semantics and stating explicitly "What LLVM is not doing").

## TL;DR

The LLVM community never held a debate that **rejected** a Stabilizer-style
runtime re-randomisation facility. Nobody proposed one in-tree, so there was
nothing on the table to vote down. What actually happened is narrower and more
interesting than "rejected": Stabilizer was mentioned exactly once, by Emery
Berger himself, five months *after* `--randomize-section-padding` had already
been designed, reviewed and merged — too late to shape the design. The
`--randomize-section-padding` author (Peter Collingbourne, Google) replied that
he had *independently* converged on a weaker, static-only analogue of
Stabilizer's approach: build N differently-padded binary variants at link time
and sample across them at run time, "almost all of what Stabilizer does except
for heap randomization." Heap randomisation via `LD_PRELOAD`-intercepted
`malloc` was floated in that same reply as a cheap way to close the gap, but as
of this fetch (2026-08-07) no such patch exists anywhere in `llvm-project`.
Runtime code/stack re-randomisation within a single execution — the part of
Stabilizer that makes layout noise Gaussian — was never discussed at all, by
anyone, in either the RFC or the PR. It is out of scope for a *linker* by
construction (lld runs once, at link time, and exits), not something the
community weighed and turned down.

## 1. The RFC thread (discourse.llvm.org/t/83334, 9 posts, Nov 2024 – May 2025)

**Post 1 (pcc / Peter Collingbourne, Google, 2024-11-26)** opens the RFC. Key
content, in the author's own words:

> Previous work in this area includes the –shuffle-sections flag to LLD, which
> causes the section order to be shuffled according to the result of a random
> number generator. However, –shuffle-sections has the following downsides:
> - It randomizes section layout, which is not a realistic counterfactual
>   because functions in the same TU are normally clustered together, and this
>   by itself can cause performance regressions due to decreased locality that
>   may lead to measurement bias.
> - It prevents the use of the feature together with section layout optimizing
>   features such as Propeller, call-graph sorting and –symbol-ordering-file.

This is the RFC's own "alternatives considered and rejected" section — it
rejects using `--shuffle-sections` itself as the measurement-bias tool, on two
independent grounds: (a) full section reordering is itself a bias-introducing
intervention, unrealistic vs. real code growth, and (b) it is mutually
exclusive with the layout-optimisation techniques (Propeller, call-graph
sort, `--symbol-ordering-file`) that the whole point of the feature is to be
usable *alongside*. The proposed replacement, `--shuffle-padding=SEED`
(renamed to `--randomize-section-padding` during review, see §2), keeps
section order intact and only perturbs sizes via padding — explicitly framed
as modelling "a future version of the program in which changes to the code
have increased the sizes of various functions," a more realistic
counterfactual. The post also admits the proposal is not a full fix: "this is
by no means a perfect control for the measurement bias because the insertion
of padding would itself introduce a measurement bias."

**Post 2 (pcc)**: links the companion PR #117653.

**Post 3 (Peter Smith / smithp35, Arm)**: generally positive, no objection to
scope. Two substantive contributions: (a) a request that results eventually be
published against real benchmarks — "That could help others plan their own
experiments with this option. That's outside the scope of a PR though, perhaps
a blog-post or future LLVM dev meeting topic" (deferred, not rejected, and
still outside scope as of this fetch — no such blog post or dev-meeting talk
was found); (b) points to **prior LLVM work on the exact same problem**:
Kristof Beyls's talks *"Automated performance-tracking of LLVM-generated
code"* and *"Towards ameliorating measurement bias in evaluating performance
of generated code,"* noting "random perturbation is mentioned, I think at one
point being done at the basic block level rather than at link time... Running
multiple experiments on regular performance tracking was deemed to be too
expensive given the speed of the machines running the benchmarks though." This
is the closest anyone comes in the thread to citing an *even more aggressive*
randomisation scheme (basic-block-level perturbation) that was tried and
**deferred for cost reasons** (CI compute budget), not correctness or design
reasons — but this is reported second-hand from old conference talks, not
re-litigated in the thread itself.

**Post 4 (Peter Waller / peterwaller-arm)**: supportive, cross-links a
BOLT patch (`--pad-funcs-before`, PR #117924) doing something adjacent for
BOLT-optimised binaries.

**Posts 5–7 (Maksim Panchenko / maksfb, BOLT maintainer, and
peterwaller-arm)**: a side-thread about whether it's better to measure on top
of a BOLT/Propeller-"optimal" layout on both sides of an A/B experiment rather
than introducing randomisation at all. Resolved amicably — BOLT optimises for
known micro-architectural effects but "does not take into account all
potential micro-architectural effects," so perturbation-based measurement and
layout-optimisation are treated as complementary, not competing, techniques.
No rejection either way.

**Post 8 (Emery Berger / emeryberger, 2025-05-06 — five and a half months
after the RFC, four and a half months after merge)**: introduces Stabilizer by
name for the only time anywhere in this archaeology:

> As a point of information, my research group produced a pretty extensive
> randomization approach designed to control for measurement bias (the
> problem pointed out by the Mytkowicz et al. paper cited above), called
> Stabilizer, which also appeared at ASPLOS and which was inspired by that
> work. Stabilizer works differently than what is proposed here and also goes
> beyond it in some ways. It was implemented in LLVM but a very old version, so
> the code is very much not directly usable, but the approach is described
> pretty thoroughly in our paper... The WASMtime folks also have discussed
> using a number of the techniques we outline.

He then quotes the Stabilizer abstract in full (runtime re-randomisation of
code/stack/heap, Gaussian noise, ANOVA-based evaluation, the -O2-vs-O3
noise-floor finding).

**Post 9 (pcc, 2025-05-07, final post in the thread)** is the whole answer to
this brief's central question:

> Thanks Emery, I've also heard about Stabilizer. I recently developed a
> script for benchmarking changes to lld itself that IIUC does almost all of
> what Stabilizer does except for heap randomization. It would be great to see
> a similar script developed for benchmarking clang or maybe even generalized
> somehow for benchmarking other projects. Now I'm wondering if heap
> randomization wouldn't be too hard to implement on top of that. We can
> LD_PRELOAD a library that intercepts calls to malloc() and with low
> probability increases the size argument so as to move the allocation to the
> next size class. It's a less comprehensive approach than the one that
> Stabilizer uses but maybe it's good enough.

That script is real and lands in-tree three days later (§4). Heap
randomisation was floated as an easy LD_PRELOAD hack, not as an in-execution
re-randomisation scheme, and **was never implemented** — I grepped the local
`llvm-project` checkout's history for follow-up commits and searched GitHub
for related PRs; none exist. The thread ends there. No maintainer, and nobody
else, weighed in on Stabilizer, on runtime re-randomisation, or on
stack/heap randomisation for measurement, before or after this exchange.

## 2. GitHub PR #117653 review (`ELF: Introduce --randomize-section-padding option`)

Merged 2024-12-13 into `main` (10 review rounds, 32 inline comments + 10
issue comments, reviewers: Peter Smith (smithp35), Fangrui Song (MaskRay,
lld/ELF de facto maintainer), Michael Platings (mplatings)). The review is
entirely implementation-level — nobody proposed a broader or runtime facility,
and nobody invoked Stabilizer (it postdates the PR by five months). The
notable threads:

- **Naming**: the flag started life as `--shuffle-padding` (boolean-sounding).
  MaskRay objected on LLD naming-convention grounds ("We call these options...
  We don't use 'flag' to name non-boolean options"). Separately, Michael
  Platings argued the *name itself* was misleading because "shuffle" implies
  reordering when nothing is reordered: "`--randomize-section-padding` might
  be clearer. Brevity is overrated!" pcc agreed and renamed it — this is the
  origin of the discrepancy between the RFC title/PR title (`shuffle-padding`)
  and the merged flag name (`--randomize-section-padding`).
- **Should section patterns be hardcoded or user-configurable** (MaskRay,
  echoing `--shuffle-sections`'s `<glob>=<seed>` syntax): pcc deferred this
  explicitly rather than rejecting it — "In a future extension, we may
  consider adding support for custom section patterns like with
  `--shuffle-sections`... this doesn't need to happen in the initial
  implementation." As of this fetch, that extension has not been built; the
  flag still only targets a hardcoded set of section-name patterns
  (`.bss .data .data.rel.ro .rodata .text* .lbss .ldata .lrodata .ltext`,
  the last four added mid-review at MaskRay's request).
- **Alternative mechanism — alignment doubling instead of padding sections**
  (smithp35 suggested randomly doubling section alignment instead of inserting
  padding sections): pcc rejected this specific alternative with a stated
  reason — doubling alignment biases which functions get pulled onto a single
  cache line ("we want a roughly equal likelihood of the function being moved
  either to or from a single cache line"), and interacts badly with implicit
  alignment assumptions baked into linker scripts.
- **Linker-script / start-stop-symbol interaction** (smithp35 worried that
  inserting padding could silently break `MYOS_start`/`MYOS_end`-style linker
  script symbol pairs): pcc declined to silently disable the feature in that
  case, on the reasoning that "the user is opting into the feature with a
  flag" and a normal program shouldn't observably notice inserted padding —
  an explicit design choice to prefer surprising-but-consistent behaviour over
  silent feature degradation.
- Everything else is routine code review (test coverage for `.data`/`.bss`,
  `-fdata-sections` interaction, off-by-one/style nits).

No comment anywhere in the PR proposes runtime randomisation, stack/heap
randomisation, or function-order randomisation beyond section padding.

## 3. `--shuffle-sections` vs `--randomize-section-padding`: the full lineage

This is the "why did a second, overlapping flag get added" story the brief
asked about, and it turns out to be a case of **feature drift**, not
duplication:

1. **2020-02-19, Rafael Ávila de Espíndola (Google), commit `d48d3391569`
   (Phabricator D74791, `lld/ELF/Config.h` etc.)** — introduces
   `--shuffle-sections=<seed>`. Commit message, verbatim:
   > The use case for this is to introduce randomization in benchmarks. The
   > idea is inspired by the paper "Producing Wrong Data Without Doing
   > Anything Obviously Wrong!"... Doing this in lld is particularly
   > convenient as the --reproduce option makes it easy to collect all the
   > necessary bits for relinking the program being benchmarked.

   This is the *same* Mytkowicz et al. paper the 2024 RFC cites, and the
   *same* measurement-bias motivation. The Phabricator review
   (`phab-D74791-comments.txt`) is pure implementation review (`std::shuffle`
   turned out to be libstdc++/libc++/MSVC-implementation-dependent, causing a
   real Fuchsia/public-buildbot test failure days after landing, which is what
   motivated adding a portable `llvm::shuffle` — commit `7b44f0428af4`,
   "Add a llvm::shuffle and use it in lld"). No design objection was raised to
   the feature itself.

2. **2020-02-19/20, Fangrui Song (MaskRay), D74887** — extends shuffling to
   `.init_array`/`.fini_array`, explicitly "useful for detecting static
   initialization order fiasco." This is the first sign of the *second*,
   distinct use case the feature would grow into: **bug detection**, not
   measurement-bias control.

3. **2021-03-17, MaskRay, D98445 (commit `423cb321dfae`)** — special-cases
   seed `-1` to mean "deterministic reverse" rather than random shuffle,
   specifically because reversal is a *stable* transform (resilient to
   incremental relinks) good for reliably catching init-order bugs and
   "unfounded pointer comparison results of two unrelated objects" —
   explicitly a correctness/robustness use case, not a statistical one.

4. **2021-03-18, MaskRay, D98679 (commit `16c30c3c23ef`)** — generalises the
   seed argument to `<section-glob>=<seed>` so different section groups can be
   shuffled/reversed independently and others left untouched. Commit message:
   "the option is only used as debugging, so just drop the original form" —
   i.e. by 2021 the maintainer's own framing of `--shuffle-sections` was as a
   **debugging tool**, not a measurement instrument, even though that's how it
   was introduced.

By the time pcc wrote the 2024 RFC, `--shuffle-sections` had accreted two
independent design pressures pulling it away from being a good
measurement-bias counterfactual: it fully reorders sections (unrealistic, and
its own bias source per point 1 of the RFC), and it had been extended in
directions (glob-scoped, `-1` reversal) optimised for bug-hunting rather than
statistical A/B testing. `--randomize-section-padding` is best read as a
**purpose-built replacement for the measurement-bias use case specifically**,
built by the same community that had let the original tool's design drift
toward a different job — not a case of anyone deciding the old flag was wrong
and needed replacing outright; `--shuffle-sections` remains in the tree,
undeprecated, doing the debugging job it evolved into.

A 2026-05-10 blog post by MaskRay ("Fighting Hyrum's Law in LLVM",
`maskray.me/blog/2026-05-10-fighting-hyrums-law-in-llvm` — a tertiary source,
fetched and cross-checked against the primary git history above, not treated
as authoritative on its own) confirms this framing from the maintainer's own
side: he groups both flags under "Linker output... two ELF-only lld knobs
perturb layout details that no contract covers," describing
`--randomize-section-padding`'s purpose as making implicit dependencies
visible — "Callers grow dependencies on padding-induced offsets the linker
never promised — profile-guided pipelines, side-channel research, exploit
toolchains pinning to specific addresses. A seeded perturbation makes those
dependencies visible" — i.e. explicitly folds pcc's measurement-bias tool into
his own Hyrum's-Law-defense framing, rather than treating it purely as a
statistics tool. The same post has an explicit "What LLVM is not doing"
section, which is the single closest thing in this whole archaeology to a
community statement about scope limits: "The mechanisms above all target
surfaces no stable consumer should care about: bucket order, equal-element
sort order, init-array order. Debuggers, profilers, sanitizers, and
reproducible-build infrastructure consume those outputs and need them
stable." It draws one explicit contrast worth keeping distinct from
everything else in this brief: clang's `-frandomize-layout-seed` /
`__attribute__((randomize_layout))` (struct-field-order randomisation) is
"mechanically the same... But the intent is exploit mitigation, cribbed from
GrSecurity's Randstruct GCC plugin: per-build kernel hardening, not a
developer tool." This is the closest LLVM comes to a stack/heap/layout
randomisation feature outside lld, and it is a **security** feature, unrelated
to measurement bias, operating on struct layout rather than stack frames or
heap objects, and unconnected in any commit or discussion to either the RFC or
Stabilizer.

## 4. The actual follow-up: `lld/utils/run_benchmark.py`

Landed 2025-05-02 (commit `6b25cfbb98b2`, PR #138367, reviewers rnk/MaskRay/
smithp35), five days *before* pcc's Discourse reply describing it, three days
*after* Emery Berger's Stabilizer post. This is the concrete artifact behind
pcc's "almost all of what Stabilizer does except for heap randomization"
claim, and it is worth being precise about what it actually does, because it
is not runtime re-randomisation:

- Builds `--num-binary-variants` (example in the script: 16) **statically
  distinct link-time binaries** for both the base and test commit, each with
  a different `--randomize-section-padding=<i>` seed.
- At benchmark time, iteration `i` runs `lld-{base,test}$((i % num_variants))`
  — i.e. samples across the pre-built variants, rather than re-randomising
  layout inside one running process.
- Interleaves base/test runs to control for time-varying environmental
  factors, and hands off to `hyperfine`, which independently randomises
  environment-variable block size per invocation (a `execve`-time analogue of
  ASLR-style stack-offset noise) via its own `randomized_environment_offset`
  feature.
- Explicitly does **not** randomise the heap; the docstring/commit message
  says so directly, and no heap-randomisation code exists anywhere in the
  script or its history.

This is a discrete, static-binary approximation of Stabilizer's continuous,
in-process re-randomisation: instead of one binary whose layout is resampled
every execution, you get N binaries with (deliberately, per §1) an unrealistic
but bounded padding-only perturbation, sampled round-robin. It gets you
averaging-out of layout-dependent measurement bias across many *linker* runs;
it does not get you the Gaussian-noise guarantee Stabilizer derives from
resampling **within** a single binary across executions, and it does nothing
for stack or heap layout at all.

## 5. Current state (as of 2026-08-07)

| Flag | Landed | LLVM release | Docs | ELF only? |
|---|---|---|---|---|
| `--shuffle-sections=<glob>=<seed>` | 2020-02-19 (`d48d3391569`), extended through 2021 | shipped since LLVM 11 (original form); glob/`-1` syntax since LLVM 13 | `lld/docs/ld.lld.1` (man page) | Yes, ELF only |
| `--randomize-section-padding=<seed>` | 2024-12-13 (`64da33a58923`, PR #117653) | first tagged release `llvmorg-20.1.0` (confirmed via `git tag --contains`) | `lld/docs/ld.lld.1` (man page) **and** `lld/docs/ReleaseNotes.rst`, one bullet: "`--randomize-section-padding=<seed>` is introduced to insert random padding between input sections and at the start of each segment. This can be used to control measurement bias in A/B experiments." (confirmed at `releases.llvm.org/20.1.0/tools/lld/docs/ReleaseNotes.html`). It is *not* mentioned in the top-level, project-wide `llvm/docs/ReleaseNotes.md` (that file only covers `llvm/` proper; each subproject — lld, clang, etc. — keeps its own) | Yes, ELF only |
| `lld/utils/run_benchmark.py` | 2025-05-02 (`6b25cfbb98b2`, PR #138367) | ships with the tree from that commit forward, not a user-facing linker flag | inline docstring only | n/a (Linux + tmpfs + hyperfine required) |

**Known users**: the RFC and PR are both authored/reviewed by Google engineers
(pcc, Rafael Ávila de Espíndola historically at Google) working on lld
itself and benchmarking infrastructure; smithp35/peterwaller-arm (Arm) and
maksfb (BOLT, Meta) participated as reviewers with adjacent interests
(BOLT/Propeller optimal-layout measurement) but there is no evidence in
either thread of Fuchsia, Chromium, or any other named downstream project
having *adopted* either flag for their own benchmarking pipelines — Fuchsia
appears in this archaeology only as the CI system that caught the 2020
`std::shuffle` portability bug, an unrelated incident. A separate, unrelated
2025 RFC (`Enhancing function alignment attributes`, discourse topic 88019,
post 4) has pcc mentioning Google "currently pass `-falign-functions=32` in
our internal builds in order to reduce the measurement bias effect of
functions changing size" — a second, independent Google-internal
measurement-bias mitigation technique (function alignment, not lld padding),
confirming this is an active, ongoing concern for that team beyond the single
RFC, but it's a different mechanism from anything asked about here and I did
not chase it further.

## 6. Explicit coverage statement

**Fully covered, high confidence:**
- Every post in the RFC thread (9/9, confirmed complete via Discourse's
  `post_stream.stream` array — no pagination existed to miss).
- Every inline review comment (32/32) and issue comment (10/10) on PR
  #117653, fetched via `gh api --paginate` and confirmed as a single page
  each.
- The commit-level history of `--shuffle-sections` from introduction through
  its 2021 extensions, via direct git archaeology (`git log --grep`, `git log
  -S`) in a local full clone, cross-checked against the original Phabricator
  review thread for D74791.
- Confirmed absence of any heap-randomisation follow-up: searched local git
  history and GitHub PR search; none found.
- Confirmed `--randomize-section-padding` **is** documented in lld's own
  release notes (`lld/docs/ReleaseNotes.rst`, published as part of
  `releases.llvm.org/20.1.0/tools/lld/docs/ReleaseNotes.html`) and absent only
  from the separate, project-wide `llvm/docs/ReleaseNotes.md`. (An earlier
  draft of this document got this backwards after a `grep -i -o
  '.\{80\}randomiz.\{80\}'` pattern silently failed to match across an
  embedded newline in the minified HTML — caught by re-running a plain
  substring grep during a self-check pass, and by then locating the exact
  commit that added the release-notes bullet via `git log -p`. Recorded here
  because it's the kind of tooling failure mode — silent, not an error — worth
  distrusting on principle.)

**Partially covered / lower confidence:**
- Discourse-wide search for "Stabilizer" is unreliable as a *negative* signal
  because Discourse's search tokenises "Stabilizer" down to "stabil-" and
  mostly returns unrelated "stability"/"stabilize" threads (ABI stability,
  API stability, etc.) — I manually inspected the result list and found
  exactly one genuine hit (the RFC thread itself, post 8). I did not attempt
  an exhaustive crawl of the mailing-list archives (llvm-dev pre-Discourse) or
  of cfe-dev, which are outside Discourse's index and outside this fetch's
  scope; if Stabilizer was discussed there, it wouldn't show up in this
  search.
- Kristof Beyls's two conference talks (cited second-hand in RFC post 3) were
  **not** independently fetched or watched — I'm relying on smithp35's
  paraphrase that they discussed basic-block-level random perturbation and
  found repeated-experiment tracking too CI-expensive. This is the one place
  in the record where something closer to Stabilizer's granularity was
  reportedly tried and set aside, and I could not verify the primary source
  within this task's scope.
- The MaskRay blog post is dated 2026-05-10, well after both the RFC and PR;
  it is *retrospective commentary*, not part of the original decision record,
  and I've treated it accordingly (labelled tertiary, only used for framing
  and for corroborating facts already independently verified from primary
  git history).
- compiler-rt/clang scan for measurement-motivated stack/heap randomisation
  flags: grepped `clang/include/clang/Driver/Options.td` for
  randomiz(e|ation) near stack/heap, and grepped compiler-rt sources for
  randomiz* near measur*/benchmark*/bias*. Both came back empty. This is a
  grep-based negative result, not an exhaustive read of every compiler-rt
  runtime (scudo, gwp-asan etc. do heap-layout randomisation, but every
  reference I found frames it as ASLR/exploit-mitigation, never as a
  measurement tool) — treat "no measurement-motivated flag found" as
  reasonably confident, not certain.

**Not covered at all:**
- I did not search llvm-dev/cfe-dev mailing list archives predating the 2020
  Discourse migration.
- I did not check whether WASMtime (mentioned by Emery Berger as having
  "discussed using a number of the techniques we outline") ever acted on
  that; it's outside the llvm-project repo and outside this brief's stated
  scope.
