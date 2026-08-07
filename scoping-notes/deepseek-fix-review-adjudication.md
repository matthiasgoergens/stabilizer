# Adjudication of the DeepSeek reviews of the two port fixes (2026-08-07)

Three deepseek-refute passes ran against the port fixes and the RNG trace
(verdicts committed verbatim alongside this file). Per the standing rule,
disagreements were resolved by reading the actual code, not by trusting
either side. Outcomes:

## RNG 256-cycle trace (`deepseek-rng-trace-verdict.txt`): CONFIRMED

DeepSeek stepped the 2013 `Util.h` and reproduced the claimed state
machine exactly (period 256; offsets 0-3 valid/stale, 4-255 OOB; uint8_t
wrap; per-TU copies). Status upgrade: the trace is now independently
re-derived cross-family, but still *derived twice, measured never* — the
10-line harness remains queued before any external quotation.

## Stack fix review (`deepseek-fix-stack-verdict.txt`): both findings real, neither invalidates the fix

1. **First-four-zeros defect persists post-fix — TRUE.** `_randCount`
   starts 0 over a zeroed buffer with the refill gate at 4, so calls 1-4
   return 0x00 in both the original and the fixed code. Residual
   randomness-quality defect, also present in the 2013 original.
   Trivial candidate fix: initialise `_randCount = sizeof(int)` so the
   first call refills. → recorded as an open item in NEXT.md; not applied
   yet (out of the -Rstack fix's minimal scope).
2. **Signal reentrancy — technically real, practically narrow here.**
   Both consumers of `getRandomByte()` run in signal-handler context
   (onTrap→relocate, onTimer), and the runtime is single-threaded by
   design, so the lost-update interleavings need nested signal delivery
   mid-function. Becomes a genuine hazard the moment threads are added —
   which is already the port plan's number-one structural risk. Noted in
   the thread-safety work item rather than patched piecemeal now.

## Heap fix review (`deepseek-fix-heap-verdict.txt`): both findings refuted by code + measurement

1. **"Cannot compile" — REFUTED.** DeepSeek read `UnshuffledHeap::free(ptr)`
   as a call on an unrelated class. In fact `UnshuffledHeap` =
   `KingsleyHeap<DataSource, DataSource>` is the *grandparent base*
   (ShuffleHeap's SuperHeap), so the qualified call is legal on `*this`
   and deliberately skips the shuffle override — precisely the fix's
   mechanism. Empirically settled anyway: the patched runtime built and
   passed four full verified runs (~150 epochs each).
2. **"Realloc in-place shrink across MaxSize mis-routes free" — REFUTED
   for this stack.** `ANSIWrapper::realloc` (Heap-Layers
   `wrappers/ansiwrapper.h:89-117`) returns the same pointer only when
   `getSize(ptr) == sz` exactly (metadata untouched); every other case is
   malloc-new + memcpy + free-old, each side taking the guard
   consistently. No code path updates size metadata in place, so the
   posited scenario cannot arise through this wrapper.

   Residual noted while checking: if Kingsley's header/rounding ever puts
   a ≤MaxSize *request* into a >MaxSize *bin*, the guard would free
   unshuffled what malloc shuffled — the reverse asymmetry. That
   direction is benign (skips shuffle bookkeeping; pulls nothing from
   never-filled slots) and the 600+ observed epochs saw no fault, but it
   is worth one assert in a debug build during the port proper.

## Round 2 (later the same day): PR draft, pilot statistics, code-heap fix

Three further passes at Matthias's request. Outcomes:

- **PR draft (`deepseek-pr-draft-verdict.txt`): REFUTED on two claims;
  half right.** (a) The 4-of-256 RNG steady-state sentence is indeed
  unmeasured — already the gating condition for posting; unchanged.
  (b) "Matching the original 2013 code" was called unattested — but the
  attestation exists (`~/prog/stabilizer-period/NOTES.md`,
  `libquantum-851-results/`); the refuter was not given that file. My
  context-selection error, not a draft error. However, checking the
  wording exposed a genuine imprecision: the port and the original were
  each verified against *their own era's* uninstrumented build; no
  cross-era output diff was done. Draft reworded to say exactly that.
- **Pilot statistics (`deepseek-pilot-stats-verdict.txt`): REFUTED, and
  the refutation under-counted.** DeepSeek correctly attacked the power
  sketch (pooled vs within-seed σ; and more deeply, that a
  one-binary-per-arm design suffers a *bias* replication cannot fix).
  My independent recomputation (`recompute_pilot_stats.py`, run on the
  raw CSV) found more: the pilot's "50.75% between-seed" was SSB/SST,
  not a variance component — the correct decomposition is **30.1%
  between-seed (σ_b ≈ 0.98% of mean), ANOVA p = 0.059**; and the
  PADDED-arm within-seed anomaly traces to a run-order trend
  (r = −0.39, p = 0.03) that the round-robin design pushes into
  within-seed variance. SCOPING.md corrected; baseline agent re-briefed
  (global interleaving, order covariate, method-of-moments components,
  more seeds over more reps).
- **Code-heap fix (`deepseek-fix-code-verdict.txt`, rerun after an
  empty first reply): mechanism confirmed, one latent finding.** It
  verified no malloc/free classification mismatch exists at the
  size-class level (request-size vs getSize coincide across the
  MaxSize boundary for Kingsley classes), then flagged
  `ShuffleFreeGuard::free(NULL)` dereferencing in `getSize`.
  Adjudicated: real for the class in isolation, **unreachable in the
  actual composition** — both heap typedefs wrap the guard in
  `ANSIWrapper`, whose `free()` and `getSize()` are null-safe
  (verified, `ansiwrapper.h:63-67, 119-125`; `getSize(NULL)==0` also
  makes `stabilizer_free(NULL)` correct). A one-line defence-in-depth
  null guard is queued with the stress agent's micro-fix batch so the
  class is safe standalone.

The pattern matches the documented expectation: cross-model review is a
good confirmer and a decent needler (it surfaced the first-four-zeros
defect worth fixing), but its findings are reported evidence — one of
four was simply wrong about the code, and only reading the class
hierarchy and the wrapper source settled which.
