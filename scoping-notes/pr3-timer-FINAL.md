Title: Disarm the re-randomization timer at teardown (fixes intermittent exit-time fault)

Separate from the heap and RNG crashes (other PRs): there's an
intermittent fault at process exit under code randomization. main()
(runtime/libstabilizer.cpp) never disarms ITIMER_REAL before returning, so
a SIGALRM delivered after stabilizer_main() returns runs onTimer against
state that is being torn down — the "Placing traps" path writes trap bytes
into function memory that may already be unmapped, faulting in onFault.

It's a race, so it's intermittent: I measured roughly 3/13 crashes on
repeated combined-mode runs and 1/6 under -Rcode alone, same signature
each time. It surfaced clearly while bringing up a Rust target, whose
longer post-main exit tail widens the window; the existing C benchmarks
hit it rarely enough to look flaky.

The fix sets a shutting_down flag (checked at onTimer entry, so an alarm
already in flight becomes a no-op) and disarms the timer with setTimer(0)
on stabilizer_main's return. Verified in combination with the heap-fix PR
(needed so -Rcode reaches teardown at all): tests/libquantum (851 2) under
-Rcode -Rheap went from 3/13 aborts to 20/20 clean, output byte-identical
to an uninstrumented build. Not specific to any mode — it's shared
teardown code — though -Rcode/combined exercise it most.
