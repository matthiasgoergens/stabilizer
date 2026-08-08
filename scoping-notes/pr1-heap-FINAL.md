Title: Restore ShuffleHeap's free-side large-allocation bypass (fixes -Rheap and -Rcode crashes)

I've been exercising the LLVM 21 port on tests/libquantum and hit crashes
in the heap and code modes. Thanks for the port.

Both crashes are the same bug. ShuffleHeap::malloc() bypasses its shuffle
buffer for requests over MaxSize (calling the superheap directly), but
ShuffleHeap::free() has no matching bypass: it always swaps the pointer
into the size-class's shuffle buffer — a buffer malloc() never filled for
that class, since every such allocation took the bypass. Freeing an
over-MaxSize object then pulls a null out of the empty slot and faults in
the superheap. The data heap hits it while freeing the 512-byte old block
during libquantum's first large realloc (-Rheap); the code heap hits it on
essentially every FunctionLocation free, which is well over 256 bytes
(-Rcode). It is a missing free-side bypass in the port's modern
ShuffleHeap composition.

The fix is a small ShuffleFreeGuard layer that restores malloc()'s bypass
on free, routing over-MaxSize objects to the unshuffled heap. Three
commits: the guard on the data heap, the same on the code heap, and a
null check (unreachable through ANSIWrapper, which already filters null —
defence-in-depth). This PR fixes -Rheap and -Rcode; -Rstack is an
unrelated bug in another file, sent separately. With this PR and the
separate stack PR, tests/libquantum (851 2) runs to completion under
-Rheap, -Rcode, and all modes combined, output byte-identical to an
uninstrumented build. Happy to share the test harness and logs.
