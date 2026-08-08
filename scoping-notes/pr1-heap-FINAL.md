Title: Restore ShuffleHeap's free-side large-allocation bypass (fixes -Rheap and -Rcode crashes)

I've been exercising the LLVM 21 port on tests/libquantum, and hit
crashes under sustained re-randomisation in the heap and code modes.
Thanks for the port — the CodeWindow near-mapping for PIE held up fine
across these runs; this is a separate issue in the heap composition.

Both crashes are one bug. ShuffleHeap::malloc() bypasses its own shuffle
buffer for requests larger than MaxSize, calling the superheap directly,
but ShuffleHeap::free() has no matching bypass: it always computes a
Kingsley bin index for the pointer and swaps it into that bin's shuffle
buffer — including bins malloc() never filled, because every allocation
that size took the bypass. Freeing an object larger than MaxSize then
pulls a null out of a never-filled slot and hands it to the superheap,
which faults reading its size header. This is exposed by the port's
necessary whole-heap ShuffleHeap composition against the modern
Heap-Layers/DieHard API, not a fault in DieHard itself.

- Data heap (DataShuffle=256): libquantum's first large realloc frees a
  512-byte old block; gdb at the fault showed reqSz=512, binIndex=6, the
  pointer swapped to 0x0 before the superheap free. Crashes early, before
  the re-randomisation timer first fires.
- Code heap (CodeShuffle=256): essentially every non-trivial
  FunctionLocation free, since a relocated function plus its adjacent
  relocation table is comfortably over 256 bytes; gdb showed reqSz=4096,
  binIndex=9, same swap-to-null. Crashes on the second re-randomisation
  epoch.

The fix is a small ShuffleFreeGuard layer that restores malloc()'s
MaxSize bypass on the free path, routing anything over MaxSize straight
to the unshuffled heap underneath ShuffleHeap. Applied to both the data
and code heap types. Three commits: the data-heap guard, the same guard
on the code heap, and a defence-in-depth null check (unreachable through
ANSIWrapper, which already filters null, but it keeps the layer safe if
composed without the wrapper).

This PR fixes the -Rheap and -Rcode crashes; the -Rstack crash is a
separate, independent bug in a different file, which I've sent as its own
PR. With this change, tests/libquantum (input 851 2) runs to completion
under -Rheap, -Rcode, and all modes combined, output byte-identical to an
uninstrumented build. I also ran an LD_PRELOAD-style property-test and
fuzzing pass over the exact heap composition (hundreds of thousands of
randomised malloc/free/realloc sequences straddling the 256 boundary, no
crashes). Happy to share the harness and logs if useful.
