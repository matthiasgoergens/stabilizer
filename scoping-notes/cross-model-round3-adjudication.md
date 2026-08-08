# Adjudication: codex + DeepSeek round 3 on the revised SCOPING.md (2026-08-08)

Three independent adversarial passes on the post-fix scoping document —
codex (`codex-scoping-verdict.txt`, WEAKENED), DeepSeek on the whole doc
(`deepseek-scoping-r2-verdict.txt`, REFUTED), DeepSeek on the gate
(`deepseek-gate-verdict.txt`, REFUTED) — plus codex CONFIRMING the five
fix commits (`codex-fixes-verdict.txt`). Resolved by reasoning against
the document and evidence, not by deferring to any verdict.

## Convergent finding (both families, high confidence): two benchmarks + overstated framing

Codex and both DeepSeek passes independently hit the same two things:

1. **"Tractability condition met" overstates.** I equivocated on
   "tractable" between (A) *the crashes are localised, modest-effort bugs
   rather than structural walls* — what the probe tested, and TRUE (three
   same-day root-caused fixes, cross-model CONFIRMED) — and (B) *the port
   is usable for general 2026 benchmarking* — FALSE (threads, TLS,
   unwinding, hardened targets, debugger all open; envelope = two
   single-threaded C benchmarks). The doc said "condition met," reading
   as (B). **Accepted.** Fix: split the two claims explicitly; the probe
   establishes feasibility-to-complete, not completion.

2. **The gate can't decide the general question from two benchmarks, and
   "on both benchmarks" is gameable.** libquantum and bzip2 are a
   convenience sample from the tool's own test suite, both single-threaded
   and call-heavy. **Accepted.** Fix: reframe the two-benchmark run as
   necessary-not-sufficient, encode the pass/fail asymmetry (a cheap-route
   *failure* or a Stabilizer *win* is an existence proof and informative;
   a cheap-route *pass* does not generalise), and make the real decision
   require a diverse workload portfolio (SPEC CPU2017 single- + at least
   one multi-threaded, C++/EH). Add bootstrap-CI-*width* reporting for
   σ_b so an indeterminate call (e.g. [0.2%, 0.9%] around the 0.5%
   threshold) shows up as indeterminate rather than a coin-flip verdict.

## Where I did NOT fully accept

- DeepSeek scoping §3 argues the deflationary outcome is nearly
  foreordained because measured overhead (~2×) is far outside the gate's
  <25%. **Partially accepted.** The lean is real and now stated. But the
  ~2× is on the two workloads that are *worst-case* for trap-based
  relocation (short, call-heavy); the paper's <7% was SPEC median
  (longer, less call-dominated). Overhead is workload-dependent, so the
  same "two benchmarks don't generalise" logic DeepSeek applied to
  variance applies to overhead — it cuts both ways. So: early indicators
  lean deflationary, but the overhead question is genuinely open pending
  the diverse suite, not decided.

- Both call the verdict REFUTED; I judge the underlying *probe* conclusion
  (crashes localised, port feasible to complete) survives — what fails is
  the *wording* that inflated it to "usable" and the *gate* that inflated
  two benchmarks to a general decision. So the honest net is WEAKENED,
  and the recommendation's centre of gravity moves from "conditional yes,
  likely port" to "continue; preserve the working port as a deliverable
  in its own right; expect the cheap route to suffice for most
  benchmarking; commit to finishing the port only if a diverse-workload
  baseline shows a gap it alone can fill." That shift is significant
  enough to surface to Matthias, not just edit silently.

## Consequences applied to SCOPING.md this round

Framing fixes only (numeric gate thresholds wait for the running v2
batch): tractability wording split into feasible-vs-usable; gate reframed
necessary-not-sufficient with the pass/fail asymmetry and a
workload-portfolio requirement; deflationary lean stated with the
workload-dependence caveat on overhead; bootstrap-CI-width added to the
analysis plan; recommendation centre-of-gravity moved and flagged for
Matthias.
