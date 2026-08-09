Title: Disarm the re-randomization timer at process teardown (fixes intermittent exit-time fault under -Rcode)

Depends on the -Rheap/-Rcode PR and the -Rstack PR — please merge after
those. On its own this branch still hits those crashes (its functions and
even internal data-heap use go through the unfixed paths), so it's the top
of the small fix stack rather than independently testable.

Under code randomization there's an intermittent SIGABRT at process exit.
main() (runtime/libstabilizer.cpp) never disarms ITIMER_REAL before
returning, so a SIGALRM delivered after stabilizer_main() returns runs
onTimer's "Placing traps" path against code being torn down and faults in
onFault. It's a timing race, so intermittent: ~3/13 on combined-mode runs,
~1/6 under -Rcode alone; it surfaced clearly bringing up a Rust target,
whose longer post-main exit tail widens the window.

The fix sets a shutting_down flag (checked at onTimer entry, so an alarm
already in flight is a no-op) and calls setTimer(0) on stabilizer_main's
return — disarming the outstanding timer and making any later delivery a
no-op. Also sigemptyset(&sa.sa_mask) in setHandler, which was previously
uninitialised.

Verified on top of the heap and stack PRs: tests/libquantum (851 2) under
-Rcode -Rheap went from 3/13 exit-time aborts to 20/20 clean, output
byte-identical to an uninstrumented build.

One question you might have: onTrap can also run during teardown if a
destructor or atexit calls a still-trapped function. I checked — it's
benign while the code is mapped (the function simply relocates and runs;
20/20 with an atexit probe that calls a never-relocated, still-trapped
function at exit). The only ways it faults are a program calling into
code it has already dlclose'd (which faults at instruction fetch, before
the trap, regardless) or a program corrupting its own heap in a
destructor (which aborts on the double-free itself, before onTrap). So I
didn't add function-untrapping; happy to include it as belt-and-braces if
you'd prefer.


---
POSTED 2026-08-09: https://github.com/parsa/stabilizer/pull/3 (branch matthiasgoergens:llvm21-timer-fix). Scope: onTimer teardown fix + sa_mask; untrap NOT shipped (measured unnecessary). Depends on #1/#2.
