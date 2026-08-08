Title: Fix out-of-bounds read in getRandomByte() (fixes -Rstack crash; latent since 2013)

I've been exercising the LLVM 21 port on tests/libquantum and -Rstack
crashes on the first re-randomisation epoch. The cause is in
getRandomByte() (runtime/Util.h) and is unchanged from the original
ccurtsinger/stabilizer, not introduced by the port; that repository is no
longer actively maintained, so I'm sending the fix here.

The refill branch resets the read cursor to sizeof(int) instead of 0.
After the first four calls, each refill leaves the cursor at 4, so it
reads past the 4-byte buffer (with a uint8_t wrap every 256 calls) until
it reaches unmapped storage and faults. Its only callers are the two
stack-pad update sites; the observed crash is in -Rstack.

Two commits: the one-line cursor reset (0 instead of sizeof(int)), and a
follow-on so the first call refills (otherwise the first four bytes are
zero-initialised rather than RNG output). I verified the state machine
with an instrumented harness: pre-fix reproduces the OOB walk exactly,
post-fix stays in the buffer with a refill every four calls. This PR fixes
-Rstack only; the heap/code crashes are a separate PR. With it,
tests/libquantum (851 2) runs to completion under -Rstack over ~170
epochs, output byte-identical to an uninstrumented build.


---
POSTED 2026-08-09: https://github.com/parsa/stabilizer/pull/1 (branch matthiasgoergens:llvm21-rng-fix). -Rstack smoke on the branch: 6/6 pass, output oracle-identical.
