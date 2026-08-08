Title: Fix out-of-bounds read in getRandomByte() (fixes -Rstack crash; latent since 2013)

I've been exercising the LLVM 21 port on tests/libquantum and -Rstack
crashes on the first re-randomisation epoch. The cause is in
getRandomByte() (runtime/Util.h), and it's inherited verbatim from the
original ccurtsinger/stabilizer, not introduced by your port — I'm sending
it here because this is the maintained tree, in case you also want to
forward it upstream (ccurtsinger's repo looks unmaintained).

The refill branch resets the read cursor to sizeof(int) instead of 0, so
after the first four calls the cursor never re-enters the 4-byte buffer:
as a uint8_t it walks offsets 4..255 and wraps, reading out of bounds
until it hits unmapped memory and faults. getRandomByte()'s only callers
are the stack pads, which is why only -Rstack is affected.

Two commits: the one-line cursor reset (0 instead of sizeof(int)), and a
follow-on so the first call refills (otherwise the first four bytes are
zero-initialised rather than RNG output). I verified the state machine
with an instrumented harness: pre-fix reproduces the OOB walk exactly,
post-fix stays in the buffer with a refill every four calls. This PR fixes
-Rstack only; the heap/code crashes are a separate PR. With it,
tests/libquantum (851 2) runs to completion under -Rstack over ~170
epochs, output byte-identical to an uninstrumented build. Worth noting the
bug meant stack-pad randomisation wasn't really happening as intended,
though I haven't measured the effect on results.
