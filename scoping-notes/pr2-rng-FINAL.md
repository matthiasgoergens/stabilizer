Title: Fix out-of-bounds read in getRandomByte() (fixes -Rstack crash; latent since 2013)

I've been exercising the LLVM 21 port on tests/libquantum, and -Rstack
crashes on the first re-randomisation epoch. The cause is a bug in
getRandomByte() (runtime/Util.h) — and it is inherited verbatim from the
original ccurtsinger/stabilizer, not introduced by your port; I'm sending
it here because this is the maintained tree, and mentioning it in case you
want to forward it upstream (ccurtsinger's repo appears unmaintained).

The refill branch resets the read cursor to sizeof(int) instead of 0, so
after the first four calls the cursor never returns into the 4-byte
buffer: being a uint8_t it walks offsets 4..255 and wraps, so in steady
state each 256-call cycle does one RNG draw (4 in-buffer bytes) and 252
out-of-bounds reads into adjacent storage — which eventually reaches
unmapped memory and faults. getRandomByte()'s only callers are the stack
pads (Function.cpp and the timer handler in libstabilizer.cpp), which is
why only -Rstack is affected.

Two commits: the one-line cursor-reset fix (0 instead of sizeof(int)),
and a follow-on initialising the count so the very first call refills
(otherwise the first four bytes are the zero-initialised buffer rather
than RNG output — a smaller latent issue in the same function). I verified
the state machine with an instrumented harness across the pre-fix,
cursor-fix, and first-call-refill variants: pre-fix reproduces the
256-cycle OOB walk exactly; post-fix reads stay within the 4-byte buffer
with a refill every 4 calls.

This PR fixes the -Rstack crash only; the -Rheap and -Rcode crashes are a
separate bug in Heap.h, sent as its own PR. With this change,
tests/libquantum (input 851 2) runs to completion under -Rstack, output
byte-identical to an uninstrumented build, across ~170 re-randomisation
epochs.

One note for whoever picks this up: because the bug pushed 252 of every
256 stack-pad bytes out of bounds, the intended stack-offset randomisation
was not actually happening as designed — worth knowing if you ever compare
against the paper's stack-related numbers, though I haven't measured that
effect.
