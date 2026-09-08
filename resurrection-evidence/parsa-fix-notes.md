# Fixing the `-Rheap` realloc crash in parsa/stabilizer (LLVM 21 port)

Working directory for this task. Fresh clone, separate from
`stabilizer-parsa-verify` (read-only reference) and `stabilizer` (the
original 2013 codebase, also read-only reference here). All builds and all
execution of instrumented binaries happen inside rootless podman
(`stabilizer-parsa-fix` image, container `szc-fix`). Nothing is installed or
run on the host.

Task: fix the `-Rheap` crash characterised in
`~/prog/stabilizer-parsa-verify/NOTES.md` — libquantum crashes ~0.2s in, on
the program's own first *large* `realloc()`, well before any re-randomisation
timer fires. This is the first of three characterised crashes (see
`~/prog/stabilizer/SCOPING.md` §3); `-Rcode` and `-Rstack` are separate,
unrelated bugs and are explicitly out of scope here.

## Setup

- Cloned `https://github.com/parsa/stabilizer` to
  `~/prog/stabilizer-parsa-fix/stabilizer`. HEAD landed exactly on
  `2bffc191c97008cc8ae99efbd801d8d609ce55d8`, the same commit the
  verification run tested (tip of master at the time).
- Copied `Containerfile` from `stabilizer-parsa-verify` unmodified (Ubuntu
  24.04 + apt.llvm.org LLVM 21). Built image `stabilizer-parsa-fix`.
- Container `szc-fix`, long-lived, with two bind mounts: the repo at
  `/work/stabilizer` and `~/prog/stabilizer-parsa-fix/scripts` at
  `/work/scripts` (for gdb command files — the repo mount alone isn't enough
  once we need to hand gdb a script that isn't part of the tracked source).
- `make` (top level, pass + runtime) built clean. Build-time-fetched deps
  landed on the same commits the verify run recorded (fetched same day):
  Heap-Layers `9400e88e`, DieHard `4467f2b0`.

## Step 1: reproduce

`make build CC="../../szc -Rheap"` in `tests/libquantum`, then
`LD_LIBRARY_PATH=/work/stabilizer ./libquantum 851 2` — crashed identically
to the verify run, same fault address, same backtrace (`crash-heap-only.log`
in this directory): SIGSEGV at `0xfffffffffffffff8` (-8) inside
`HL::SizeHeap<...>::getSize`, reached via `stabilizer_realloc ->
ANSIWrapper::realloc -> ANSIWrapper::free -> ShuffleHeap::free ->
StrictSegHeap::free -> SegHeap::getSize -> SizeHeap::getSize`. Confirmed:
this is not verify-environment-specific.

## Step 2: diagnose

### Coordinator correction (addressed)

An adversarial review flagged that `stabilizer_realloc`/`stabilizer_free` in
`runtime/libstabilizer.cpp` are byte-identical between the parsa port and the
2013 original (confirmed independently: both are one-line dispatches to
`getDataHeap()->realloc(p, sz)` / a `getSize(p) == 0` foreign-pointer check
before `getDataHeap()->free(p)`). So the bug is not in
`libstabilizer.cpp`. The review reprioritised: (1) Heap-Layers/DieHard version
skew, since the verify build fetched unpinned 2026 HEAD of both while the
period build used 2013-pinned commits; (2) the port's actual heap-adjacent
rewrites — `runtime/Heap.h`'s heap-type composition changed, confirmed by
its own comment ("Modern `ShuffleHeap` takes (ChunkSize, MaxSize,
SuperHeap)... we use a shuffled KingsleyHeap..."); (3) a foreign
pre-Stabilizer-heap pointer hitting `ShuffleHeap::free`.

Read `runtime/Heap.h` in both trees to settle this before touching gdb:

- **2013 original** (`~/prog/stabilizer/runtime/Heap.h`, read-only
  reference):
  `typedef ANSIWrapper<KingsleyHeap<ShuffleHeap<DataShuffle, DataSource>, DataSource>> DataHeapType;`
  — `ShuffleHeap` is Kingsley's *per-size-class* sub-heap (Kingsley
  instantiates one instance per bin); big objects go straight to
  `DataSource`, never touching `ShuffleHeap` at all. The era
  `DieHard/src/include/shuffleheap.h` (`~/prog/stabilizer-period/DieHard-src`,
  pinned 2013-01-29) matches: `ShuffleHeap<NObjects, [Size], SuperHeap>`
  handles exactly *one* fixed size, discovered lazily via a probe
  malloc/getSize/free on first use, with a single flat `_buffer`.
- **Modern DieHard's `shuffleheap.h`** (fetched live by `common.mk`, no pin —
  confirmed same commit the verify run saw, `4467f2b0`) is a different
  design: `ShuffleHeap<ChunkSize, MaxSize, SuperHeap>` handles *all* size
  classes up to `MaxSize` in one instance, with a `_buffer[binIndex]`/
  `_is_filled[binIndex]` array, and calls `SuperHeap::get_size_class`/
  `get_class_size` — i.e. it now expects `SuperHeap` to be Kingsley-shaped,
  not a plain source heap. **This makes the 2013 composition
  (`ShuffleHeap` as Kingsley's per-class arg) impossible to express against
  modern DieHard** — confirmed by reading `KingsleyHeap`'s still-current
  `PerClassHeap`/`BigHeap` template contract in
  `Heap-Layers/heaps/general/kingsleyheap.h` (unchanged shape, but the
  `PerClassHeap` argument modern `ShuffleHeap` requires isn't a plain heap
  any more). The port's `Heap.h` responded by inverting the composition:
  `ANSIWrapper<ShuffleHeap<4096, DataShuffle, KingsleyHeap<DataSource,
  DataSource>>>` — `ShuffleHeap` now wraps the *whole* `KingsleyHeap`
  (both the per-class and the big-object heap), matching modern
  `shuffleheap.h`'s API. This is a genuine, forced adaptation, not an
  accidental version-skew regression — the old API doesn't exist any more to
  regress *to*.

So both halves of the coordinator's reprioritisation are partially right:
version skew forced a real design change (not just a different pinned SHA to
try), and the change lives in the port's own `Heap.h`, not
`libstabilizer.cpp`. What's still needed is *evidence* of exactly how the new
composition crashes, not just that it changed.

### gdb, live process, in-container

`gdb --batch --command=/work/scripts/gdb-heap-crash.gdb ./libquantum` against
`tests/libquantum`'s `-Rheap`-only build (`851 2`), breaking on
`stabilizer_realloc` and stepping through calls (`gdb-heap-crash-02.log`,
full script in `scripts/gdb-heap-crash.gdb`):

- Calls 1-4 to `stabilizer_realloc`: sizes 32, 64, 128, 256 — all succeed
  (these stay within `DataShuffle = 256`, the `ShuffleHeap` `MaxSize`).
- The crashing call: `stabilizer_realloc(ptr=0x7f9b69a00010, sz=1024)`.
  `ANSIWrapper::realloc` frame shows `objSize = 512` (the *old* block's real
  size, read via `getSize`, which works fine — `ShuffleHeap` doesn't define
  `getSize`, so it's inherited straight through from `KingsleyHeap`,
  size-class-agnostic) and `buf = 0x7f9b67a00010` (the new 1024-byte block,
  allocated fine — `ShuffleHeap::malloc`'s own `sz > MaxSize` fast path
  routes straight to `KingsleyHeap::malloc`, bypassing the shuffle buffer
  entirely for both the 512- and 1024-byte requests, since both exceed
  `MaxSize = 256`).
- `ANSIWrapper::realloc` then calls `free(ptr=0x7f9b69a00010)` — a live,
  valid pointer, confirmed non-null in the `ANSIWrapper::free` frame.
- Inside `ShuffleHeap::free`, the locals at the crash are `reqSz = 512`,
  `binIndex = 6`, `j = 4` — i.e. `ShuffleHeap::free` **did not** take any
  `MaxSize` bypass (it has none) and instead computed a Kingsley bin index
  for the real 512-byte size and picked a shuffle-buffer slot for it. At the
  point of the fault, the frame's own `ptr` parameter reads `0x0` — i.e. the
  `swap(_buffer[binIndex](j), ptr)` line overwrote the local `ptr` with
  whatever was in `_buffer[6][4]`, and then called
  `SuperHeap::free(ptr)` (`KingsleyHeap::free`, frame `StrictSegHeap::free`,
  `this=ptr=0x0` — matches) with that swapped-in value. `StrictSegHeap::free`
  calls `getSize(0x0)` internally to pick a bin to free into, which crashes
  reading the size header at `ptr - 8 = -8` — **exactly the observed fault
  address**.
- Why is `_buffer[6][4]` null? `_is_filled[binIndex]` and `_buffer[binIndex]`
  are only populated by `ShuffleHeap::malloc`'s `fill()`, and only for bins
  reached through malloc's *non*-bypass path (`sz <= MaxSize`). Since every
  object bigger than `DataShuffle = 256` bytes — bin 6 (512 bytes) and up —
  always takes `malloc`'s bypass, `fill()` never runs for those bins, so
  `_buffer[binIndex]` stays at its static-storage zero-initialised state
  (`getDataHeap()`'s heap object is a function-local static, i.e. zero-init
  before any constructor runs; `Array<N,T>::_item[N]`
  (`DieHard/src/include/array.h`) has no user constructor, so it's never
  touched beyond that zero-init) — **all-null**, for every bin above
  `MaxSize`. `ShuffleHeap::free` has no check for this and blindly swaps a
  live pointer for one of these null slots.

**Root cause, stated with a falsifiable check**: `ShuffleHeap::free()` (in
the live-fetched, modern `DieHard/src/include/shuffleheap.h`) has no
`MaxSize` bypass symmetric to the one `ShuffleHeap::malloc()` has — every
`free()` of an object whose real size exceeds `MaxSize` (`DataShuffle = 256`
in Stabilizer's config) computes a Kingsley bin index for it and swaps it
into that bin's shuffle buffer, which for bins above `MaxSize` was never
filled (since `malloc()` bypasses `fill()` for those sizes) and so holds a
null pointer; the null gets freed instead of the real one, and the crash
happens inside `getSize(NULL)`. Confidence: high — gdb's own locals
(`reqSz = 512`, `binIndex = 6`, and the frame's `ptr` parameter reading
`0x0` at the crash, versus the live `0x7f9b69a00010` one frame up) show the
mechanism directly, not by inference from source reading alone. The
observation that would falsify this: if the fix below (routing `free()` of
objects `> MaxSize` around `ShuffleHeap` entirely) does *not* make the
crash disappear, the diagnosis is wrong. That check is the real test and is
recorded in Step 4, not asserted here in advance.

**Pinned-dependency diagnostic (coordinator-requested, run for completeness)**:
built the *unmodified* port (`git worktree add scratch-pin-test
2bffc191c9 --detach`) against the exact 2013-era Heap-Layers/DieHard commits
the working period run used (`341fff3581be4327bc30ca81e90c6ba513942692` /
`65be5ec133078fe8a5e1fdbbb8c70284f08949b8`, copied in place of the
build-time clone). Result: **does not compile**
(`build-04-pinned-scratch.log`) —
`./Heap.h:29:39: error: template argument for template type parameter must
be a type` against `../DieHard/src/include/shuffleheap.h:21`. The port's
`Heap.h` passes `ShuffleHeap<4096, DataShuffle, KingsleyHeap<...>>` — three
arguments, second one a `size_t` constant — but era `shuffleheap.h`'s
(non-`FIXED_SIZE`) `ShuffleHeap` takes exactly `<NObjects, class SuperHeap>`,
two arguments, second one a *type*. This settles the coordinator's version-skew
hypothesis precisely: it's not that old dependencies happen to avoid the bug
at runtime — the port's `Heap.h` is unconditionally written against the
modern `shuffleheap.h` API and is a hard compile-time mismatch against the
old one. "Just pin the dependency" is not an available fix without *also*
reverting `Heap.h`'s composition (which brings back the C++17
dynamic-exception-specification errors era Heap-Layers throws under a modern
default `-std`, `exceptionheap.h:39`, among what a full port-back would
likely surface more of). Confirms the "Fix considered and rejected" reasoning
below empirically rather than by inspection alone. Worktree removed after
this check (`git worktree remove scratch-pin-test --force`) — it was scratch
only, no commits made there.

**Not the root cause** (ruled out): a foreign pre-Stabilizer-heap pointer
(prime suspect (c) from the original brief) — the crashing pointer
(`0x7f9b69a00010`) was demonstrably allocated by this same `DataHeapType`
instance (its `getSize` returned a valid Kingsley class size, 512, which a
foreign/glibc pointer would not do consistently — `SizeHeap::getSize` reads
a magic-numbered header at `ptr-8`; a foreign pointer reading that header
would produce either a crash *there* rather than after a successful 512
read, or a wrong-but-plausible size, not the clean value seen). Also not
Heap-Layers/DieHard *SHA* skew in the narrow sense of "an old commit would
build the port unmodified and not crash" — the 2013 API this composition
depends on (`ShuffleHeap<MaxSize, SuperHeap>`, per-class instantiation via
Kingsley) no longer exists at any live DieHard commit recent enough to
compile under LLVM 21/C++20 tooling, so "just pin an older SHA" is not
available as a fix path without also reintroducing GCC-4.6-era C++ that
won't compile against modern headers; see "Fix considered and rejected"
below.

## Step 3: fix

`Heap-Layers` and `DieHard` are both `.gitignore`d and fetched fresh by
`common.mk` on every build with no pin (confirmed: `.gitignore` lists both
by name; `common.mk`'s clone rules `rm -rf` and re-clone whenever the
directory is absent). Any fix inside `shuffleheap.h` itself would be
invisible to a fresh clone and silently lost — not a viable fix location.
The fix has to live in Stabilizer's own tracked source, i.e. `runtime/Heap.h`.

**Fix considered and rejected**: pin `DieHard`/`Heap-Layers` to the 2013 era
commits (`~/prog/stabilizer-period/NOTES.md`'s
`341fff3581be4327bc30ca81e90c6ba513942692` /
`65be5ec133078fe8a5e1fdbbb8c70284f08949b8`) and revert `Heap.h`'s
composition to the original `KingsleyHeap<ShuffleHeap<DataShuffle,
DataSource>, DataSource>`. Rejected: era Heap-Layers is pre-C++11 in a lot of
its headers and does not compile clean under a modern `-std=` default (this
is the exact build-time trap `runtime-analysis.md` flagged); it would need
either forcing an old `-std=gnu++98`-ish flag project-wide (risking breaking
the *rest* of the port, including the LLVM-21-facing pass, which is
unrelated to this bug) or patching era Heap-Layers itself (which, being
`.gitignore`d/fetched, has the same "invisible fix" problem as patching
modern `shuffleheap.h`). Much larger surface than the actual defect
warrants.

**Fix applied**: a small guard heap layer in `runtime/Heap.h`, inserted
between `ANSIWrapper` and `ShuffleHeap` for `DataHeapType` only (not
`CodeHeapType` — the code heap's own crash is a separate, already-tracked
bug in `FunctionLocation::sweep()`, out of scope here, and touching
`CodeHeapType` risks changing that unrelated failure's behaviour for no
benefit). `ShuffleFreeGuard<MaxSize, ShuffledHeap, UnshuffledHeap>` mirrors
`ShuffleHeap::malloc`'s own `sz > MaxSize` bypass for `free()`: if the
pointer's real size (read via the size-class-agnostic, inherited `getSize`)
exceeds `MaxSize`, route the free straight to `UnshuffledHeap::free` (the
concrete `KingsleyHeap<DataSource, DataSource>` that `ShuffleHeap` itself
wraps, reached by an explicit upcast — valid because `ShuffledHeap` publicly
inherits exactly that type), skipping `ShuffleHeap::free` and its shuffle
bookkeeping entirely; otherwise behave exactly as before
(`ShuffledHeap::free`). This restores, for `free()`, exactly the size-based
routing the 2013 design got for free from Kingsley itself dispatching
`ShuffleHeap` only to matching-size objects — without needing the retired
per-class-instantiation API.

Diff (see commit for the authoritative version): `runtime/Heap.h`, adds the
`ShuffleFreeGuard` template and changes `DataHeapType`'s typedef to route
through it; `CodeHeapType` untouched.

## Step 4: verify

All runs via the long-lived `szc-fix` container, `tests/libquantum`, fixed
`851 2` input (deterministic; see verify-notes for why).

- **`-Rheap` alone, correctness, four independent runs** (`heap-fixed-run-01.log`,
  `heap-fixed-run-repeat-{1,2,3}.log`): all exit 0, ~77s wall clock each
  (`/usr/bin/time -v`), i.e. well over the 60s bar and roughly 150
  re-randomisation-timer epochs per run (500ms interval — the log shows
  "Re-randomizing stack pads" firing throughout even though only `-Rheap`
  was requested; the runtime evidently re-arms the stack-pad timer
  unconditionally regardless of which `-R` flags were compiled in, unrelated
  to this fix). stdout captured separately from the `[libstabilizer.cpp:...]`
  trace (`heap-fixed-stdout-only.log`) and `diff`'d byte-for-byte against
  the uninstrumented oracle (`clang -O2` build of the same sources,
  `oracle-851-x2.log`) — **`diff` exit 0 on all four runs**. Oracle and all
  four instrumented runs produce identical output: `N = 851, 52 qubits
  required` / `Random seed: 2` / `Measured 985026 (0.939394)...` / `Possible
  period is 66.` / `Unable to determine factors, try again.`
- **`-Rcode` alone** (`verify-code-only.log`): unaffected — crashes at
  ~1.29s wall, `FunctionLocation::sweep -> ShuffleHeap<...CodeSource...>::free
  -> SizeHeap::getSize`, signal 6. Matches the verify run's characterisation
  exactly (2nd re-randomisation epoch). `CodeHeapType` wasn't touched by the
  fix, so this is expected and confirms no regression.
- **`-Rstack` alone** (`verify-stack-only.log`): unaffected — crashes at
  ~0.61s wall, right after "Re-randomizing stack pads", signal 6. Matches
  the verify run's characterisation (1st epoch).
- **`-Rheap -Rcode` combined** (`verify-heap-code.log`) — the key
  regression check the task called for: no longer dies on the first
  `realloc` (that path is fixed). Survives two full re-randomisation
  timer firings ("Re-randomization timer fired" x2, "Placing traps" x2)
  and dies at ~1.61s in exactly the same `FunctionLocation::sweep ->
  CodeSource ShuffleHeap::free -> SizeHeap::getSize` crash as `-Rcode`
  alone — i.e. it now survives up to the known, already-tracked `-Rcode`
  epoch-2 bug, precisely as predicted, and dies there rather than at the
  old heap bug. No new failure mode.
- **All three combined (`-Rheap -Rcode -Rstack`, the test suite's own
  default)** (`verify-all-modes.log`): previously crashed at ~0.31s (the
  earliest of the three independent bugs, the heap one). Now survives past
  that and dies at ~1.72s with the identical `FunctionLocation::sweep`
  signature as the `-Rheap -Rcode` case above — the code bug is now the
  earliest-firing of the two remaining bugs in this combination on this
  input. No new failure mode; nothing beyond the already-tracked bugs.

**Conclusion**: the `-Rheap` crash is fixed — verified against a working
oracle over multiple full-length runs, not just "doesn't crash". The other
two characterised bugs (`-Rcode`, `-Rstack`) are confirmed unperturbed by
this change, including in combination with the now-fixed heap path.

## Artefacts in this directory

- `Containerfile`, `empty-auth.json` — build environment (copied from
  `stabilizer-parsa-verify`, unmodified).
- `stabilizer/` — the working clone; see its own git log for the fix commit.
- `scripts/gdb-heap-crash.gdb` — the gdb batch script used for Step 2's
  diagnosis (bind-mounted at `/work/scripts` in the container).
- `build-*.log`, `crash-heap-only.log`, `gdb-heap-crash-*.log`,
  `heap-fixed-*.log`, `oracle-851-x2.log`, `verify-*.log` — raw logs backing
  every claim above, in the order referenced.
- `scripts/gdb-stack-crash.gdb` — gdb batch script for the `-Rstack`
  diagnosis (Step 2 of the second section below).
- `nm-stack-only.log`, `gdb-stack-crash-01.log`,
  `nm-libstabilizer-sizesort.log`, `readelf-sections-libstabilizer.log`,
  `readelf-sections-period-libstabilizer.log`, `stack-fixed-*.log`,
  `verify-*-poststackfix.log`, `heap-recheck-stdout.log` — raw logs backing
  the `-Rstack` fix section below.

---

# Fixing the `-Rstack` first-epoch crash

Second task, same working tree, built on top of `f9ed534` (the `-Rheap`
fix). Task: diagnose and, if tractable, fix the `-Rstack` crash — libquantum
crashes ~0.5-0.6s in, on the *first* re-randomisation timer tick, in
`onTimer`'s "Re-randomizing stack pads" step. Coordinator supplied three
working hypotheses to test: (a) the pad `GlobalVariable` lands in a
read-only mapping (rodata/relro) under modern LLVM/PIE; (b) the pass changed
how pads are registered/allocated; (c) an ordering issue (pads registered
before the runtime's pad set exists). All three treated as hypotheses to
falsify, not assumptions.

## Step 1: source comparison before touching gdb

Compared the pad-registration and pad-randomisation code in the port against
the 2013 original (`~/prog/stabilizer/`, read-only reference) before
reproducing, to narrow the search:

- `runtime/libstabilizer.cpp`'s `stabilizer_register_stack_pad` (inserts
  into `std::set<uint8_t*> stack_pads`) and `onTimer`'s stack-pad
  re-randomisation loop (`for (... stack_pads ...) **iter =
  getRandomByte();`) are **byte-identical** between the port and the
  original, modulo container-type spelling (`std::set` vs bare `set` from a
  `using namespace std`) and the port's removal of a pre-existing harmless
  `live_functions.empty();` no-op (already flagged as dead code in
  `runtime-analysis.md`, unrelated here). Rules out hypothesis (b) at the
  runtime level.
- `pass/Stabilizer.cpp`'s pad `GlobalVariable` construction (`new
  GlobalVariable(m, stackPadType, /*isConstant=*/false,
  GlobalValue::InternalLinkage, getInt(m,8,0,false), name+".stack_pad")`)
  and the `stabilize_stack && !stabilize_code` registration-call path (the
  one exercised by `-Rstack` alone) are also unchanged between port and
  original. `isConstant=false` means LLVM has no license to place this in
  `.rodata`; expectation going in was `.bss`/`.data`, not read-only —
  weakens hypothesis (a) before even reproducing, but "expectation" isn't
  evidence; checked properly below. Rules out hypothesis (c) at the source
  level too: registration order (`registerStackPad` calls emitted in the
  same module constructor, same relative position) is unchanged.

## Step 2: reproduce and gdb

`make build CC="../../szc -Rstack"` in `tests/libquantum`, run under gdb
with `handle SIGALRM nostop noprint pass` (so gdb doesn't stop on the
harmless 500ms timer signal itself, only on the actual fault) —
`scripts/gdb-stack-crash.gdb`, log `gdb-stack-crash-01.log`. Crashed on the
very first timer tick, matching the verify run's characterisation.

**First surprise**: `nm` on the built binary shows every `*.stack_pad`
symbol as lowercase `b` — i.e. **already in `.bss`, writable** — directly
contradicting hypothesis (a) before gdb even ran (`nm-stack-only.log`).

**Second, bigger surprise**: the actual fault is not a write through a pad
pointer at all. gdb's backtrace at the SIGSEGV:

```
#0  getRandomByte () at ./Util.h:51        <- crash here: uint8_t r = _rands[_randCount];
        _randCount = 76 'L'
        _rands = "\3503\342\201"           (4-byte buffer)
#1  onTimer (...) at libstabilizer.cpp:245  <- **iter = getRandomByte();
        pad = 0x55d1d46c217c <quantum_real.17.stack_pad> ""
```

The crash happens *inside* `getRandomByte()`, reading its own internal state
array, **before** the pad pointer is ever dereferenced for the write. The
pad `GlobalVariable` (`quantum_real.17.stack_pad`) is never touched at the
point of the fault. All three of the coordinator's hypotheses are about the
pad's memory — none of them can be right, because the pad isn't involved
yet. This redirected the whole investigation.

`runtime/Util.h:37-54`:

```cpp
static inline uint8_t getRandomByte() {
    static RandomNumberGenerator _rng;
    static uint8_t _randCount = 0;

    static union {
        uint8_t _rands[sizeof(int)];
        int _bigRand;
    };

    if(_randCount == sizeof(int)) {
        _bigRand = _rng.next();
        _randCount = sizeof(int);      // <- resets to 4, not 0
    }

    uint8_t r = _rands[_randCount];
    _randCount++;
    return r;
}
```

**Root cause, part 1 (the actual bug)**: after refilling `_bigRand` via
`_rng.next()`, the refill branch sets `_randCount = sizeof(int)` (4) instead
of `0`. `_rands` is a 4-byte array (`uint8_t[sizeof(int)]`, aliased to the
`int` via the union), valid indices 0-3. Once `_randCount` reaches 4 the
first time, it is **never reset** — every subsequent call reads
`_rands[_randCount]` with `_randCount` marching upward forever (4, 5, 6,
...), i.e. reading further and further past the 4-byte buffer's end, one
byte per call. Only when `_randCount` (a `uint8_t`, wraps at 256) happens to
land back on exactly 4 does a real refill fire again — once every 256 calls
— otherwise every call reads adjacent memory as if it were a random byte.
This is a walking out-of-bounds read with no bound at all, not a one-off
off-by-one.

**Confirmed identical in the 2013 original** (`~/prog/stabilizer/runtime/Util.h`,
read-only reference) — byte-for-byte the same function, same bug. **Not a
port regression.** This bug has existed since 2013.

**Root cause, part 2 (why it crashes now and not then)**: whether a walking
OOB read like this ever reaches an unmapped page — and how many calls it
takes — depends entirely on how much writable memory happens to be mapped
contiguously after the buggy static buffer. Checked directly:

- `getRandomByte()` is `static inline` with **internal linkage** (C-style
  `static`, not just `inline`) — every translation unit that includes
  `Util.h` gets its own independent copy of the function and its static
  locals. `nm -S --size-sort libstabilizer.so` shows exactly two copies of
  `_rands`/`_randCount`/`_rng`, at `0x2d0c8`-ish and `0x42dfb0`-ish
  (`nm-libstabilizer-sizesort.log`). The crash backtrace's `getRandomByte`
  frame resolves to the second copy, at `0x42dfb4`.
- `readelf --sections libstabilizer.so`: `.bss` spans `0x2d050` +
  `0x400f68` bytes = ends at `0x42dfb8` exactly.
  `0x42dfb4 (this copy's _rands base) + 4 (its size) = 0x42dfb8` — **this
  copy of `_rands` occupies the literal last 4 bytes of the entire `.bss`
  section.** Confirmed by arithmetic, not approximation.
- Why is `.bss` 4.06 MiB (`0x400f68`) in the port build? `nm -S
  --size-sort`'s largest two entries are `getDataHeap()::buf` and
  `getCodeHeap()::buf`, each `0x2006f8` (~2.1 MiB) —  the function-local
  static `DataHeapType`/`CodeHeapType` singletons. This traces to modern
  `ShuffleHeap`'s `_buffer[NumBins]` member (`shuffleheap.h`, the same file
  implicated in the `-Rheap` fix above): `NumBins = ChunkSize/8 = 512`, and
  each bin unconditionally holds an `Array<ChunkSize/8, void*>` — 512
  pointers Ă— 8 bytes = 4096 bytes **per bin, regardless of that bin's
  actual object size** — Ă— 512 bins = 2 MiB, Ă— two heaps (data + code) ≈
  4.2 MiB, matching the measured `.bss` size closely. (The 2013 original's
  `ShuffleHeap` is instantiated once *per Kingsley size class* with a
  buffer sized proportionally to that class's own object size, not a flat
  512-pointer array replicated unscaled across all 512 bins — structurally
  much smaller. Not fixed here — see "not in scope" below.)
- Cross-checked directly against the period build: `readelf --sections` on
  `~/prog/stabilizer-period/stabilizer-src/libstabilizer.so`
  (read-only, inspected via a one-off `podman run --rm` mounting that
  directory `:ro` — no execution, no modification) shows `.bss` there is
  only `0x1f0f8` (~127 KiB) — **over 32x smaller**
  (`readelf-sections-period-libstabilizer.log` vs
  `readelf-sections-libstabilizer.log`). A 127 KiB `.bss` gives a walking
  OOB read from a buried static variable tens or hundreds of thousands of
  bytes of padding to cross before it could ever reach an unmapped page —
  never happens inside the coordinator-reported 173-epoch period run. A
  4.06 MiB `.bss` with this *specific* TU's copy landing in the last 4
  bytes gives it exactly 0 bytes of padding — the very first
  re-randomisation epoch's stack-pad loop (~100+ pads in libquantum, one
  `getRandomByte()` call each) comfortably exceeds the ~76 calls needed to
  walk off the end.

**Stated with the falsifying check**: if this is right, the crash is
independent of the *pad* memory entirely and should reproduce identically
under `-Rstack` even if the pad `GlobalVariable`s were e.g. renamed,
reordered, or given different linkage — because the fault happens before
any pad is ever touched. That's exactly what the backtrace already shows
directly (frame #1 is mid-loop, `pad` is a valid, resolved symbol, and the
fault is one frame *inside* `getRandomByte`, never reaching the `**iter =
...` store). Confidence: high — this is direct evidence (crash-site
disassembly plus exact `.bss`-boundary arithmetic), not inference from
source reading alone.

## Step 3: fix

Smallest fix that matches the actual cause: `runtime/Util.h`, change the
refill branch's reset target from `sizeof(int)` to `0`:

```cpp
    if(_randCount == sizeof(int)) {
        _bigRand = _rng.next();
        _randCount = 0;
    }
```

This is a one-token fix to the actual logic bug (present in both port and
original), in Stabilizer's own tracked source, matching the surrounding
code exactly. It does not touch the `.bss`-bloat contributing factor (see
below).

**Deliberately not fixed here**: the oversized `ShuffleHeap::_buffer[NumBins]`
layout that inflated `.bss` 32x and is what turned this particular
pre-existing bug into a reliable crash instead of a latent one. That's a
port/Heap.h-composition concern shared with the `-Rheap` fix's territory,
but changing it is a bigger, riskier surface (it touches both heap
singletons' memory layout) than this task's actual defect warrants, and the
one-line `Util.h` fix removes the *actual* bug outright — after the fix,
`getRandomByte()` never reads out of bounds at all, so the `.bss` size stops
mattering for this failure mode regardless of its own merits. Flagging it
here per the "flag rather than doing surgery beyond the probe's scope"
instruction: if a future task wants to right-size `ShuffleHeap`'s buffers to
match the original's per-class scaling, this note plus the `-Rheap` fix's
NOTES.md section are the starting context.

## Step 4: verify

- **`-Rstack` alone, correctness, two independent full runs**
  (`stack-fixed-run-02-full.log`, `stack-fixed-run-repeat-2.log`): both exit
  0, ~85s wall each — well past the coordinator-reported 173-epoch period
  benchmark (500ms interval Ă— 85s ≈ 170 epochs). A first attempt under a 90s
  `timeout` (`stack-fixed-run-01.log`) ran the full 90s with the timer
  firing throughout and no crash (`exit 124` = killed by `timeout`, not a
  fault) before being re-run without a timeout to confirm actual
  completion. stdout captured separately
  (`stack-fixed-stdout-only.log`) and `diff`'d byte-for-byte against the
  same oracle used for the `-Rheap` fix (`oracle-851-x2.log`) — **`diff`
  exit 0 on both runs**.
- **`-Rheap` alone, re-checked with both fixes in the tree**
  (`heap-recheck-stdout.log`): still exit 0, still byte-identical to the
  oracle — confirms the `Util.h` fix doesn't interact with the earlier
  `Heap.h` fix (expected: `ShuffleHeap`'s own shuffling uses DieHard's
  `RandomNumberGenerator` directly, not `getRandomByte()`, so the two fixes
  touch disjoint code paths).
- **`-Rcode` alone, re-checked with both fixes in the tree**
  (`verify-code-only-poststackfix.log`): unaffected — crashes at ~1.60s
  with the identical signature as before both fixes
  (`FunctionLocation::sweep -> CodeSource ShuffleHeap::free ->
  SizeHeap::getSize`, signal 6, 2nd epoch). This is the known, already-
  tracked, out-of-scope bug; confirms the `Util.h` fix doesn't perturb it
  even though `getRandomByte()` is also called from `Function.cpp:83`
  (`*_stackPad = getRandomByte();`, the per-function pad draw on first
  relocation) — that call site is exercised by every `-Rcode` run and was
  not previously suspected of contributing to the epoch-2 crash; confirmed
  here that it doesn't (same crash, same timing region, no new behaviour).
- **All three modes combined (`-Rcode -Rstack -Rheap`, the test suite's own
  default), re-checked with both fixes in the tree**
  (`verify-all-modes-poststackfix.log`): survives past both now-fixed bugs
  and dies at ~1.59s with the same `FunctionLocation::sweep` signature as
  `-Rcode` alone — i.e. with two of the three original bugs fixed, the
  remaining `-Rcode` bug is now the only failure mode in every combination
  tested. No new failure mode anywhere.

**Conclusion**: the `-Rstack` first-epoch crash is fixed, verified against
the same working oracle used for the `-Rheap` fix, over multiple full-length
runs exceeding the period benchmark's epoch count. `-Rcode` (the one
remaining characterised bug, out of scope for both tasks so far) is
confirmed unperturbed, including with all three modes combined.

---

# Fixing the `-Rcode` second-epoch crash

Third task, same working tree, built on top of `f9ed534`+`29afeef` (the
`-Rheap` and `-Rstack` fixes). Task: diagnose and, if tractable, fix the
last characterised bug — libquantum crashes ~1.29s in (the 2nd
re-randomisation epoch), inside `FunctionLocation::sweep()`, freeing a
stale/null code-heap pointer, fault address `-8` (null-derived), same shape
as the `-Rheap` crash. Coordinator supplied three ordered hypotheses: (a)
the same modern-`ShuffleHeap` malloc/free bypass asymmetry as the `-Rheap`
bug, but on the code heap (my `ShuffleFreeGuard` was deliberately
`DataHeapType`-only when I built it, since the task at the time was scoped
to `-Rheap`); (b) `CodeWindow`/`TextRelocations` interaction with location
bookkeeping; (c) the frame-pointer-walk mark phase missing a live location
under PIE, so `sweep()` frees in-use code. Told to test (a) first as cheap
and likely, with gdb evidence before any fix.

## Step 1: reproduce, and a real gdb/runtime conflict

`make build CC="../../szc -Rcode"` in `tests/libquantum`, run under gdb.
First attempt (`scripts/gdb-code-crash.gdb`, `handle SIGTRAP nostop noprint
pass` set *before* `run`) killed the process **immediately, before any
output at all** (`gdb-code-crash-01.log`, "Program terminated with signal
SIGTRAP. The program no longer exists."). Diagnosed rather than
worked around blindly: `-Rcode` installs `int3` (0xCC) traps at every
instrumented function's entry as part of normal operation, and the dynamic
linker itself also relies on an internal SIGTRAP (the initial post-`execve`
stop, and `r_brk` shared-library-load notifications) that gdb must consume
*itself* to finish `run`'s startup sequence. Setting `pass` for *all*
SIGTRAPs before `run` forwards those linker-internal traps to a process
that has no handler for them yet (the runtime's own `sigaction` isn't
installed until partway through `main()`), so the kernel's default
disposition (terminate) fires instantly — nothing to do with the runtime's
own code-relocation logic.

Fixed by deferring the `handle ... pass` change until *after* the process
has moved past its own startup traps: break on a symbol first, let gdb use
its normal (stop, don't pass) SIGTRAP handling to get there safely, `handle
SIGTRAP nostop noprint pass` only then, `continue`.

**Second attempt hit a different, genuine conflict**: breaking on
`stabilizer_main` (`gdb-code-crash-02.log`) reached the breakpoint fine, but
the *following* `continue` (after enabling pass-through) crashed almost
immediately with `Function::relocate()` reading a garbage `this` pointer
(`0x7501ff83d47d8b00`) from a garbage `Function*` inside `onTrap`. Cause:
`stabilizer_main` is itself one of the pass's instrumented, trap-armed
functions (it's local to the program like every other function — "Trapped
all functions" traps it too), so gdb's own software breakpoint (also a
`0xCC` write) landed on the *same byte* as the runtime's own already-placed
trap. gdb's breakpoint bookkeeping saves "the original byte" to restore on
`continue` — but what it saved was the *runtime's* `0xCC`, not the true
original instruction, corrupting the `FunctionHeader`/trap protocol at that
address. This is the same class of interference the period run's
`strace -f` finding already flagged (`~/prog/stabilizer-period/NOTES.md`)
between `ptrace`-based tools and this runtime's self-modifying-code
mechanism — confirmed here to extend to gdb breakpoints specifically, not
just signal delivery.

**Fixed** by breaking on a symbol that is *not* subject to the pass's
instrumentation — any plain runtime-library function, never patched with a
trap (`gdb-code-crash-03.log` used `stabilizer_register_function`, which
turned out not to exist as a distinct symbol at that name so the breakpoint
silently didn't set — gdb instead stopped at the first real `int3` trap
under its own default, safe SIGTRAP handling, which worked just as well:
reached `stabilizer_main` normally, *then* `handle SIGTRAP nostop noprint
pass`, then `continue`). This time the program ran two full
re-randomisation epochs ("Placing traps" x2) and crashed cleanly on the
real bug, with clean symbolication throughout.

## Step 2: gdb evidence for hypothesis (a)

`gdb-code-crash-03.log`, backtrace at the SIGSEGV:

```
#0  SizeHeap<...>::getSize (ptr=0x0)                     <- crash: ptr-8 read
#1  SegHeap<...CodeSource...>::getSize (ptr=0x0)
#2  StrictSegHeap<...CodeSource...>::free (ptr=0x0)        objectSize = 0
#3  ShuffleHeap<4096,256,KingsleyHeap<CodeSource,CodeSource>>::free (ptr=0x0)
        reqSz = 4096
        binIndex = 9
        j = 0
#4  ANSIWrapper<ShuffleHeap<...>>::free (ptr=0x56400ee11010)   <- live, valid pointer
#5  FunctionLocation::~FunctionLocation (this=0x7f1205e001f0)
#6  FunctionLocation::sweep ()
#7  onTrap (...)
#9  quantum_decohere ()
```

Exact match to the `-Rheap` bug's mechanism: frame #4 (`ANSIWrapper::free`)
is called with the FunctionLocation's real, live code pointer
(`0x56400ee11010`); by frame #3 (`ShuffleHeap::free`), its own `reqSz` local
is `4096` — the object's *real* Kingsley class size — computed correctly by
the first `SuperHeap::getSize(ptr)` call in `ShuffleHeap::free` (using the
still-valid `ptr`), then `binIndex=9`, then the `swap(_buffer[9](j), ptr)`
line overwrites the local `ptr` with whatever was in that never-filled
slot — `0x0` — and `SuperHeap::free(0x0)` (`StrictSegHeap::free`, frame #2)
is what's on the stack at the crash, reading `getSize(0x0)` internally and
faulting at `ptr - 8`.

**Why bin 9 (4096 bytes) was never filled**: `CodeShuffle` (`MaxSize` for
the code heap) is `256`, same as `DataShuffle`. `FunctionLocation`'s
allocation size is `_code.size()` (+ `_table.size()` if the relocation
table is adjacent) — a whole function's relocated machine code. For any
real function (not a one-line stub), that is routinely well over 256 bytes;
`quantum_decohere` (the function whose location is being freed in this
trace, frame #9) is a real, non-trivial function. So essentially *every*
`FunctionLocation` free in a real program takes `ShuffleHeap::malloc`'s
`sz > MaxSize` bypass on allocation and then hits the exact same missing
bypass in `free()` that `-Rheap`'s fix addressed — this isn't an edge case
for the code heap, it's closer to the common case, which matches the
crash's own determinism (always the 2nd epoch, never later, never earlier,
in every run at both the coordinator's report and here).

**Hypotheses (b) and (c) not investigated further**: (a) reproduced the
exact, deterministic mechanism with direct evidence on the first attempt,
per the "test (a) first" instruction, and the fix below removes the failure
entirely (checked in Step 3) — no residual symptom that would motivate
chasing (b) or (c) as well.

## Step 3: fix

Same fix as `-Rheap`, applied to the other heap. `ShuffleFreeGuard` was
already generic over `MaxSize`/`ShuffledHeap`/`UnshuffledHeap`, so this is
purely a `CodeHeapType` typedef change plus a comment, no new logic:

```cpp
typedef ANSIWrapper<ShuffleFreeGuard<CodeShuffle,
    ShuffleHeap<4096, CodeShuffle, KingsleyHeap<CodeSource, CodeSource> >,
    KingsleyHeap<CodeSource, CodeSource> > > CodeHeapType;
```

`DataHeapType` unchanged. Commit `19137a3`.

## Step 4: verify

- **`-Rcode` alone**: first a 90s-timeout smoke check
  (`code-fixed-run-01.log`) — ran the full 90s (~80s under `/usr/bin/time`,
  since the timeout wrapper adds negligible overhead) to **actual
  completion**, exit 0, not killed by the timeout this time. Re-run capturing
  stdout only (`code-fixed-stdout-only.log`), `diff`'d against the same
  oracle used throughout (`oracle-851-x2.log`) — **exit 0, byte-identical**.
- **All three modes combined (`-Rcode -Rstack -Rheap`, the test suite's own
  default and the task's actual success criterion)**: built fresh
  (`build-17-all-modes-final.log`), then run **three independent times**
  (`all-modes-final-run-01.log` under `/usr/bin/time -v`, plus two more
  stdout-only repeats `all-modes-final-stdout-{1,2}.log`) — **all three
  exit 0**, ~82-83s wall each (~165-170 re-randomisation epochs at the
  500ms interval, matching the coordinator's 173-epoch period-container
  benchmark), and **all three `diff` byte-identical** to the oracle. This is
  the exact success criterion given: "libquantum 851 2, all modes, ~170
  epochs, byte-identical output" — met.

**Conclusion**: all three characterised `parsa/stabilizer` LLVM-21-port
crashes are now fixed. `libquantum` runs to completion, correctly, under
every randomisation mode individually and combined, sustained over the full
period-container-equivalent epoch count.

## Summary across all three tasks in this file

Three commits, same working tree, on top of the same base commit
(`2bffc191c9`):

1. `f9ed534` — `-Rheap` realloc crash (`runtime/Heap.h`,
   `ShuffleFreeGuard` on `DataHeapType`).
2. `29afeef` — `-Rstack` first-epoch crash (`runtime/Util.h`,
   `getRandomByte()` cursor reset).
3. `19137a3` — `-Rcode` second-epoch crash (`runtime/Heap.h`,
   `ShuffleFreeGuard` on `CodeHeapType`).

Two of the three root causes (heap, code) are the same defect in two
places: a real gap in the port's necessary adaptation to modern DieHard's
`ShuffleHeap` API (`malloc()`'s size-based shuffle-bypass has no matching
bypass in `free()`), hit by the data heap on any `realloc`/`free` over 256
bytes and by the code heap on almost every `FunctionLocation` free (function
code is routinely over 256 bytes). The third (stack) is a bug inherited
unchanged from the 2013 original, merely promoted from latent to
reliably-crashing by an unrelated `.bss` size increase caused by the same
modern-`ShuffleHeap` adaptation's oversized per-bin buffers. All three fixes
are small (one new ~20-line generic template used twice, one one-token
change), stay in Stabilizer's own tracked source, and were each verified
against a working, byte-identical oracle over multiple full-length runs
before being called done.

`libquantum 851 2` now runs to completion under `-Rheap`, `-Rstack`,
`-Rcode`, and all three combined, individually and together, matching the
uninstrumented reference output exactly, over runs of ~170
re-randomisation epochs — the same sustained-execution bar the original
2013 Stabilizer passes in the period container. The `parsa/stabilizer`
LLVM 21 port is, as far as `libquantum` under this test harness can show,
no longer just "builds" but "usable for sustained, randomised runs."
