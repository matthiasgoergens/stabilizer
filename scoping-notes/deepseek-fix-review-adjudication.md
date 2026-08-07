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

## Meta

The pattern matches the documented expectation: cross-model review is a
good confirmer and a decent needler (it surfaced the first-four-zeros
defect worth fixing), but its findings are reported evidence — one of
four was simply wrong about the code, and only reading the class
hierarchy and the wrapper source settled which.
