# Citation-graph scoping: successors to Stabilizer (Curtsinger & Berger, ASPLOS 2013)

Question: is there a maintained, more modern tool or technique that supersedes
Stabilizer for statistically sound performance evaluation (runtime
re-randomisation of code/stack/heap layout to make layout noise averageable
and normally distributed)?

**Short answer: no.** No actively maintained tool reimplements or extends
Stabilizer's core mechanism (compiler-pass-driven runtime re-randomisation of
code, stack *and* heap layout, for the purpose of measurement rather than
security). The closest thing is a community fork that ports the original code
to LLVM 12 but by its own README has broken code/stack randomisation, leaving
only heap and link-order randomisation reliable. The field's actual
successors went two different directions instead: better *statistical*
treatment of layout noise (rather than eliminating it by re-randomising), and
re-randomisation as a *security* mechanism (ASLR-adjacent, not a measurement
tool). Both are documented below.

## Data sources and coverage

- **Semantic Scholar**: the public, unauthenticated `api.semanticscholar.org`
  endpoint returned HTTP 429 ("Too Many Requests") on every one of five
  attempts spread over the session (`s2-search.json` through
  `s2-search5.json` in this directory). A Google-indexed web search snippet
  independently reports Semantic Scholar's citation count for this paper as
  **117**. I could not pull S2's citing-paper list directly; all citation-graph
  traversal below is from OpenAlex.
- **OpenAlex**: the paper is split across **four separate OpenAlex work
  records** for what is bibliographically the same ASPLOS 2013 paper (an
  artefact of how OpenAlex ingested the ACM proceedings vs. the SIGARCH
  Computer Architecture News reprint vs. at least one incomplete/duplicate
  entry): `W2165134790` (87 citations), `W4252148751` (15), `W4243656446`
  (6), `W2186284206` (2). I pulled the full citing-work list for all four via
  `/works?filter=cites:<id>` (paginated with cursors; script at
  `scoping-notes/fetch_citations.py`), merged and deduplicated by DOI/ID,
  and got **104 unique citing works** (raw per-record dumps:
  `citing-w2165134790.jsonl`, `citing-w4252148751.jsonl`,
  `citing-w4243656446.jsonl`, `citing-w2186284206.jsonl`; merged:
  `citing-merged.jsonl`).
- **Coverage statement**: 104 unique citing papers examined (title + abstract
  triage on all of them) against a Semantic-Scholar-reported total of 117 —
  roughly 89% coverage by count. The gap is plausibly citations OpenAlex
  attaches to none of the four split records, or S2-only indexed venues
  (theses, workshops). This is good coverage for triage purposes but not
  exhaustive; a paywalled or S2-only citing paper could in principle exist
  that this sweep missed. I did not find any signal in web searches or GitHub
  that such a paper exists (e.g. no "Stabilizer considered harmful" or
  formal replication study turned up anywhere, not just in the citing set).

## Shortlist: candidate successors / alternatives

### Direct code/stack/heap layout-randomisation tools

**Dead2/stabilizer (LLVM 12 fork)** — the only actively-updated fork of
Stabilizer itself. GitHub: `Dead2/stabilizer`, 27 stars, 3 forks, last push
2023-08-15, 9 open issues. Its README is explicit: it "supports LLVM version
12, although there seem to be some crashes with `SZ_STACK` or `SZ_CODE`
enabled. `SZ_HEAP` and `SZ_LINK` seem to work fine however," and separately,
"Help is wanted for testing and fixing the remaining crashes." It dropped GCC
support (DragonEgg was never ported past old LLVM) and rewrote the compiler
wrapper for compatibility with modern build systems. This is evidence
*for* the motivating question in the negative direction: the maintainer's own
assessment is that Stabilizer's core code+stack randomisation does not work
reliably on any modern LLVM, and heap-only randomisation is what actually
functions. The original `ccurtsinger/stabilizer` repo (592 stars, 47 forks,
last pushed 2021-09-29) states in its own README: "This project is no longer
being actively maintained, and only works on quite old versions of LLVM
[3.1]." I checked all 20 network forks of the original repo by star count;
none beyond Dead2's is a substantive independent port — the rest are stale
mirrors (`plasma-umass/stabilizer`, Berger's own lab org, last pushed 2014;
`emeryberger/stabilizer`, last pushed 2022, appears to be a personal mirror
not new work).

**Scrambler: dynamic layout adaptation** (Onward! 2016 companion
proceedings, DOI `10.1145/2984043.2998549`). Authors (via Crossref): David
Chang, Thu Nguyen, Niko Takayesu — **not** PLASMA lab; a websearch result
initially and wrongly suggested Curtsinger/Berger authorship (it was picking
up a citation to Stabilizer in the reference list, not the actual byline;
corrected here). Abstract (short, companion/poster-length paper): "changes to
a program's layout — the placement of code and data in memory — can change
performance by more than the effect of standard optimization techniques,"
and describes a system that "runs C and C++ programs with a randomized
layout, and monitors these programs for evidence of layout-related
performance [issues]." This reads as an online/adaptive-optimization tool
(adapt layout at runtime to *improve* production performance) rather than a
measurement-debiasing tool like Stabilizer, though the boundary is fuzzy from
the abstract alone. No public code found (no GitHub repo under obvious
names/authors); this looks like a workshop-scale student project that did not
propagate further — it has no citations of its own in the merged set.

**Code Shaker** — "A proper performance evaluation system that summarizes
code placement effects" (PASTE 2013, DOI `10.1145/2462029.2462035`).
Authors: Masahiro Yasugi, Yuki Matsuda, Tomoharu Ugawa (Kyushu Institute of
Technology). Conceptually the closest thing to Stabilizer found in the whole
sweep — it targets exactly the same phenomenon (code placement biasing
branch predictor / icache behaviour) — but uses a different mechanism:
instead of Stabilizer's *runtime* re-randomisation of a single binary, it
statically generates a batch of "artificial programs that differ from the
evaluation target program (almost) only in their code placement" and
statistically summarises across that batch. No public code found; no GitHub
project named "Code Shaker" or attributable to these authors turned up. It is
a 2013 workshop paper, published essentially simultaneously with Stabilizer,
so it's a contemporary rather than a successor, and shows no sign of ongoing
development.

### Security-motivated re-randomisation (excluded per task scope, noted for completeness)

A large fraction of the citing set is exactly the "cite in passing for
ASLR/security" bulk the task said to ignore: **SPAM** (Stateless Permutation
of Application Memory, 2020, LLVM pass, defence against memory-disclosure
attacks), **Adelie** (continuous ASLR for Linux drivers, 2022), **Oxymoron**
(fine-grained memory randomisation for code sharing, 2014), **CodeArmor**,
**KPointer**, **MagBox**, **Timely Rerandomization for Mitigating Memory
Disclosures** (2015), and a dedicated 2019 survey, **"A Survey of Research on
Runtime Rerandomization Under Memory Disclosure."** These all reuse
Stabilizer-adjacent mechanisms (compiler-pass-driven layout randomisation)
but for attack-surface reduction, not measurement — confirmed by abstract
text in every case (e.g. SPAM: "a software defense that enables fine-grained
data permutation ... resilience against attacks"). None claim or attempt
statistically sound performance evaluation as a goal. This is a genuinely
large and active research area, just not the one the motivating question is
about.

### Statistical-methodology alternatives (treat layout noise as a nuisance parameter rather than eliminating it)

This is where the field's actual live activity is.

**Kalibera & Jones line** (Tomáš Kalibera, Richard Jones — Kent). Two papers
in the citing set, 7 years apart, same authors:
- *"Rigorous benchmarking in reasonable time"* (ISMM 2013) — proposes an
  autocorrelation-aware repeated-measures methodology to get valid confidence
  intervals without the enormous repetition costs of naïve statistics.
- *"Quantifying Performance Changes with Effect Size Confidence Intervals"*
  (arXiv 2007.10899, 2020) — explicit continuation. Abstract states most
  systems papers "failed to address the non-deterministic execution of
  computer programs (caused by factors such as memory placement, for
  example), and none addressed non-deterministic compilation," and proposes
  a statistical model producing results of the form "system A is faster than
  system B by 5.5% ± 2.5%, with 95% confidence." This is the most direct
  living continuation of Stabilizer's underlying concern (layout-induced
  noise corrupting comparisons) found anywhere in the citing set — but note
  the strategy is orthogonal to Stabilizer's: it treats layout noise as one
  more source of measurement uncertainty to quantify statistically, not
  something to eliminate via re-randomisation. I found no dedicated public
  tool/package implementing this specific method (checked GitHub and web
  search; R packages named `RESI`/`effectsize`/`MBESS` implement generic
  effect-size confidence intervals but are not by these authors and don't
  address the non-deterministic-compilation angle).

**"Statistical Performance Comparisons of Computers"** (Chen et al., IEEE
TC 2014) proposes HPT (Hierarchical Performance Testing), a non-parametric
framework avoiding the normality assumption Stabilizer's re-randomisation
was specifically designed to satisfy. Informal implementations exist (a bash
implementation for the PARSEC benchmark suite, and at least one Python port
applied to the "Faster CPython" benchmark dataset per a web search result),
and it surfaced as a **feature request in `psf/pyperf`** (Python's standard
performance-benchmarking library, issue #168, "Add Hierarchical Performance
Testing (HPT) technique to `compare_to`?") — i.e. mainstream, actively
maintained tooling (pyperf: 949 stars, pushed 2026-08-05) is aware of this
alternative statistical approach, though the issue does not show it as
merged/implemented.

**DataMill** (2015, "a distributed heterogeneous infrastructure for robust
experimentation") and the associated PhD thesis *"Measuring and Predicting
Computer Software Performance: Tools and Approaches"* (Augusto Born de
Oliveira, 2015) — a University of Waterloo project taking a third strategy:
control "hidden factors" (their term, explicitly listing "binary link order,
process environment size, compiler generated randomized symbol names" among
others) by running experiments across a heterogeneous, community-sourced
fleet of machines rather than by randomising layout within a single run. No
GitHub project named `datamill` (or similar) related to benchmarking turned
up in a repository search; the project appears to not have survived as
public infrastructure past the thesis.

**"Systems research is running out of time"** (HotOS 2021) — a position
paper about timer precision/accuracy in systems research, tangential to
layout randomisation specifically but part of the same broader
"our measurement methodology in systems research is not rigorous enough"
critique lineage that includes Stabilizer.

**Coz** (causal profiling, Curtsinger & Berger again, SOSP 2015 /
CACM 2018) — this is PLASMA lab's actual next project in this space, and it
is the one still maintained (`plasma-umass/coz` on GitHub). But per the
task's own framing, it answers a different question ("where should I optimise
to get the most speedup," using virtual/causal delays) rather than Stabilizer's
question ("is this measured performance difference real, or layout noise").
I did not find any PLASMA lab output between 2013 and the present that
extends Stabilizer's own mechanism; Coz is the closest thing organisationally
but is a genuine pivot, not a successor.

**Other tangential hits in the citing set**, briefly: *Talus* (2015, removes
cache-partitioning performance cliffs — a different kind of "cliff," not
about statistical evaluation), *VarCatcher* (2016, characterises rather than
eliminates parallel-workload variability), *Emulating cache organizations...
performance cloning* (2015, workload cloning for design-space exploration,
unrelated purpose), *Taming performance variability* (Google, OSDI-adjacent
2018, large-scale empirical study of server-to-server variability in a
warehouse-scale fleet — a different, "why is my datacenter's performance
noisy" question, not benchmarking methodology), *Fex* (2017, a
containerised, extensible systems-evaluation *framework*, not
layout-specific), *Helical* (ACM REP 2025, a high-level DSL for specifying
experimental hypotheses so that constants/sources-of-variability are explicit
in the artefact — general experiment-design tooling, not layout-randomisation
specific; abstract via OpenAlex was empty, description above is from a
web search of the ACM page), and *What's Wrong with My Benchmark Results?
Studying Bad Practices in JMH Benchmarks* (2019, static-analysis tool for bad
JMH microbenchmark patterns — practical/tooling but scoped to Java
microbenchmarks, not layout noise per se).

## Replication / critique findings

I found **no paper in the citing set, and no result from targeted web
searches, that explicitly attempts to replicate or refute Stabilizer's two
headline claims** — (1) that re-randomisation makes layout effects
approximately normally distributed (validated in the original paper via
Shapiro-Wilk), and (2) that `-O3` vs `-O2` on SPEC CPU2006 is statistically
indistinguishable from noise. The closest things to a critique are indirect:
Kalibera & Jones (2020) note that as of their survey, essentially no systems
paper addresses "non-deterministic compilation" rigorously — an implicit
comment on the state of the field including tools like Stabilizer, not a
targeted critique of it. The open, unresolved **`bheisler/criterion.rs`
issue #334** ("Eliminate memory layout bias when measuring (with LLVM
stabilizer)", opened 2019-09-21 by `fasterthanlime`, still open with no
linked PR) is useful practical evidence in a different register: it shows
that seven years ago a mainstream, still-actively-maintained benchmarking
tool's maintainers were made aware of exactly this problem and Stabilizer as
the fix, and to date have neither adopted Stabilizer nor built an
alternative — the problem is simply undocumented/unaddressed in that
ecosystem's day-to-day tooling.

## Bottom line

For someone asking "should I go find a maintained modern version of
Stabilizer": there isn't one that fully does what Stabilizer did. The most
usable path today is the **Dead2/stabilizer fork** if heap-only
randomisation on LLVM 12 is sufficient for the use case (code/stack
randomisation is reported broken); if a from-scratch, currently-maintained
re-implementation covering code+stack+heap on a modern LLVM is required, it
does not appear to exist. If the actual need is "statistically sound
performance evaluation" more broadly rather than specifically layout
re-randomisation, the live, still-updated lineage to look at is Kalibera &
Jones's effect-size / uncertainty-quantification line and the
non-parametric HPT approach that has at least been proposed for adoption in
`psf/pyperf` — both sidestep the layout-normality problem statistically
rather than solving it mechanically the way Stabilizer did.

## Files in this directory

- `fetch_citations.py` — OpenAlex citation-pagination script used for this sweep.
- `citing-w*.jsonl` — raw per-OpenAlex-record citing-work dumps.
- `citing-merged.jsonl` — deduplicated merge of all four (104 records).
- `openalex-*.json`, `crossref-*.json`, `gh-*.json`, `readme-*.md` — raw API
  responses backing specific claims above, kept for re-checking.
- `s2-search*.json` — failed (429) Semantic Scholar API attempts, kept as
  evidence of what was tried.
