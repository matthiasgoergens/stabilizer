This native allocator unit test runs under UBSan as part of `make test` and
CI. Its fake mapping source returns null deterministically; it does not
exhaust memory or reserve executable mappings. It covers failed initial
allocation, retry, an old chunk's tail after failed refill, size headers,
single-class free-list reuse, alignment, overflow and Kingsley fallback.

`-DLEGACY_SOURCE` selects the previous dependency layers as a diagnostic
negative control. They fail the first allocation with null-pointer arithmetic.
The ordinary CI run selects the checked layers; it does not depend on a
particular sanitizer diagnostic from the old dependencies.

This is code-source coverage, not an all-allocator OOM guarantee. The data
heap and shuffle-reservoir failure handling are separate concerns. Normal
legacy and retained execution remain covered by the other runtime tests.
