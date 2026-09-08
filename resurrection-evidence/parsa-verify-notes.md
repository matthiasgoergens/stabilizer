# parsa/stabilizer LLVM 21 verification notes

Task: verify the README claim "This repository has been updated to work with
modern LLVM toolchains (tested with LLVM 21)."

All building and all execution of instrumented binaries happens inside
rootless podman containers. Nothing is installed on the host; no instrumented
binary is run on the host.

## Repo / commit under test

- Cloned `https://github.com/parsa/stabilizer` to
  `/home/matthias/prog/stabilizers/stabilizer-parsa-verify/stabilizer`.
- Commit tested: `2bffc191c97008cc8ae99efbd801d8d609ce55d8`
  ("leave more intrinsics for LLVM to lower itself"), authored 2026-02-14T15:25:16-06:00.
  This is the tip of `master`, matching the fork-survey note (26 commits ahead
  of upstream, last commit 2026-02-14).
- No git submodules.

## Build-time fetches

`common.mk` (unmodified from upstream, still present in parsa's tree) clones,
**unpinned**, at build time:
- `https://github.com/emeryberger/Heap-Layers.git` -> `$(ROOT)/Heap-Layers`
- `https://github.com/emeryberger/DieHard.git` -> `$(ROOT)/DieHard` (only its
  `src/include/*` subdirectories are used, per `runtime/Makefile`
  `INCLUDE_DIRS`)

`runtime/Heap.h` includes `<heaplayers>` and `<shuffleheap.h>` (from
Heap-Layers); `runtime/Util.h` includes `<randomnumbergenerator.h>` (from
DieHard's `src/include/util`). Both are fetched live from GitHub's current
HEAD by the Makefile itself — parsa's fork did not pin or vendor these, same
as upstream. Exact commit SHAs fetched are recorded below once the build
runs.

## Container

`Containerfile` in this directory: `ubuntu:24.04` + apt.llvm.org
`llvm-toolchain-noble-21` (`llvm.sh 21 all`), symlinks unversioned tool names
(`clang`, `clang++`, `opt`, `llc`, `llvm-link`, `llvm-as`, ...) to the `-21`
binaries, `CPATH=/usr/lib/llvm-21/include` so the LLVM C++ headers are found
without patching the Makefiles (per README: "assumes LLVM include files will
be accessible through your default include path").

## Test ladder — final results

1. **Pass builds against LLVM 21: YES.** `make` (top level) compiles
   `pass/Stabilizer.cpp` and `pass/LowerIntrinsics.cpp` cleanly against
   `/usr/lib/llvm-21/include` with only deprecation warnings
   (`PointerType::get` with pointee type, `InsertPosition` for instruction
   insertion) — no errors, no patches needed. Links `LLVMStabilizer.so` as an
   LLVM new-PM plugin (`llvmGetPassPluginInfo`), loaded via
   `opt --load-pass-plugin=`.
2. **Runtime builds: YES.** `runtime/*.cpp` compile and link cleanly into
   `libstabilizer.so`/`libstabilizer.a`, after the build-time `git clone` of
   Heap-Layers and DieHard (see below). One benign warning (`NO_INLINE`
   macro redefined between Heap-Layers and its own `spinlock.h`).
3. **tests/HelloWorld compiles under szc: YES**, for `-Rcode` alone,
   `-Rstack` alone, `-Rheap` alone, and all three combined (`-Rcode -Rstack
   -Rheap`, matching `tests/HelloWorld/Makefile`'s default). No patches
   needed.
4. **Instrumented HelloWorld runs correctly: YES**, in all four
   configurations (code / stack / heap / all three) — prints "Hello
   Constructor!" then "Hello World!" and exits 0 in every case. Also
   confirmed the pass is genuinely doing PIE-relative code allocation: the
   runtime logs a computed "Code allocation window" straddling the process's
   own PIE load address (e.g. `[0x564d5f804000, 0x564ddf804000)`), and the
   binary is left as an ordinary PIE executable by clang's Ubuntu default (no
   `-no-pie` needed anywhere in the default `szc` invocation).
5. **tests/libquantum >=30s stress run: REACHED, FAILS.** Built via the
   project's own `make build` (mirrors `tests/libquantum/Makefile`, i.e. the
   authentic two-phase compile-then-link szc invocation). Used a fixed `N=851
   x=2` (`./libquantum 851 2`) so the run is fully deterministic (confirmed:
   two runs of the uninstrumented reference produce byte-identical output;
   `shor.c` seeds its classical RNG in `spec_srand(26)` and only the
   coprime-base `x` is otherwise time-seeded via `srandom(time(0))`, which
   passing `x` explicitly on the command line bypasses). Uninstrumented
   reference build (`clang -O2`, same sources, same `-DSPEC_CPU
   -DSPEC_CPU_MACOSX`) takes ~29s wall and would have served as the
   correctness oracle — **but no instrumented configuration got anywhere
   close to finishing**, so no output comparison was possible. All three
   randomization modes crash on real (non-trivial) execution, each in a
   distinct subsystem — see "Crashes" below. **This is the load-bearing
   result of the whole verification: the README's "tested with LLVM 21"
   claim holds for build + a near-instantaneous smoke test, and does not
   hold once a program survives past ~1 second of wall-clock runtime.**
6. **PIE vs non-PIE: both work, for the trivial case.** Default `szc` output
   is PIE (Ubuntu clang default) and HelloWorld runs correctly under it
   (rung 4). `szc`'s driver does **not** expose a way to force `-no-pie`
   (its `-f` passthrough always emits `-f<value>`, and `-no-pie` isn't an
   `-f` flag) — worked around by manually relinking the same `.llc.o`
   intermediate with `clang ... -no-pie` bypassing `szc`'s final `codegen()`
   step. The manually-built non-PIE HelloWorld (`-Rcode`) also runs
   correctly, with the code-allocation window correctly starting near
   `(nil)` instead of near the PIE base. Not tested against libquantum,
   since the mode-crashes in rung 5 are unrelated to PIE (reproduced
   identically under default PIE builds for `-Rcode`, `-Rstack`, `-Rheap`
   independently).
7. **Crash characterization: three distinct, reproducible bugs**, one per
   randomization mode, all triggered by libquantum (`N=851 x=2`), none by
   HelloWorld (which exits before any 500ms timer even fires once). Full
   logs in `crash-*.log`. Summary:

   - **`-Rheap` (alone or combined): crashes before any re-randomization
     epoch**, ~0.2–0.3s in, on the *program's own first ordinary `realloc()`
     call* (not a re-randomization event at all). Signal SIGSEGV (caught by
     Stabilizer's own `onFault` handler, which then `abort()`s — process
     exit is SIGABRT/6). Faulting address `0xfffffffffffffff8` = `-8`,
     i.e. a read at `ptr - 8` where `ptr` is 0 — classic signature of a
     custom allocator's `getSize()` being called on a null/zero pointer
     without a null check. Backtrace:
     `stabilizer_realloc → HL::ANSIWrapper<ShuffleHeap<...>>::realloc →
     ...::free → HL::SizeHeap<...>::getSize` — i.e. the crash is *inside
     Stabilizer's own runtime* (`libstabilizer.so`), not in relocated user
     code. **This makes `-Rheap` essentially unusable for any program that
     calls `realloc()` in the ordinary course of execution** — the bug has
     nothing to do with the 2026 LLVM/kernel environment; it's a runtime
     logic bug in the ported heap-redirection path itself.
   - **`-Rcode` (alone or with `-Rstack`, heap not required): crashes on the
     2nd re-randomization epoch**, wall time ~1.29s (≈ 2 × the 500ms
     `ITIMER_REAL` interval plus startup). Same fault signature
     (`0xfffffffffffffff8`), but a different call path:
     `onTrap → FunctionLocation::sweep() → FunctionLocation::~FunctionLocation()
     → (code heap) ShuffleHeap::free → SizeHeap::getSize`. This *is* the
     mark/sweep-of-stale-relocated-code path `runtime-analysis.md` flagged
     as the mechanism most exercised by sustained re-randomization: the
     sweep phase, walking the frame-pointer chain to find and free
     locations no longer live, frees a null/stale `FunctionLocation`
     pointer. Reproduced identically with `-Rcode` alone and `-Rcode
     -Rstack` together — `-Rstack` is not required to trigger it. Never
     survives to a 3rd epoch in any run.
   - **`-Rstack` (alone): crashes on the 1st re-randomization epoch**, wall
     time ~0.5–0.6s. Log shows `[libstabilizer.cpp:242] Re-randomizing stack
     pads` immediately before the fault. This time the faulting address is a
     page-aligned address close to `libstabilizer.so`'s own mapping
     (`0x7fc46f62e000`, versus the library's load base
     `~0x7fc46f1f0000`-ish inferred from the symbol offsets in the same
     trace) rather than a small/null-derived offset — a different failure
     mode from the other two, not yet root-caused beyond the log location
     (`onTimer`'s stack-pad re-randomization step).
   - **Combined (`-Rcode -Rstack -Rheap`, i.e. the test suite's own
     default): crashes at ~0.31s**, matching the *earliest* of the three
     individual-mode failures (the heap bug, which doesn't even wait for a
     timer tick) — consistent with the three bugs being independent and the
     first one encountered along the execution path winning.

   None of these are build-, LLVM-21-, or LLVM-toolchain-version-related:
   the pass and runtime compile and link without incident, and the bugs
   reproduce deterministically with fixed inputs. They are logic bugs in the
   2026 rewrite of the runtime's heap/code/stack randomization paths,
   exposed by any program that runs long enough or allocates enough to reach
   a real re-randomization epoch or a real `realloc()` call — which
   `tests/HelloWorld` structurally never does (it returns before the first
   500ms tick and never calls `realloc`).
8. **Frame pointers / optimization levels used by the test configs:**
   `szc` defaults to `-O2` for the `opt` pipeline (`-passes=default<O2>,...`)
   and always invokes the final `llc` codegen step at
   `llc -O0 -relocation-model=pic --frame-pointer=all` (`szc`'s
   `codegen()`), i.e. the LLVM *optimization* pipeline runs at `-O2` but the
   *native code generation* backend pass is forced to `-O0` with frame
   pointers unconditionally kept (`--frame-pointer=all`), for every `-R`
   configuration, not just `-Rcode`/`-Rstack`. This matches
   `runtime-analysis.md`'s point that the mark/sweep frame-pointer walk is a
   structural requirement — parsa's 2026 driver makes that requirement
   explicit and unconditional (upstream's original `szc`, per that same
   note, relied on stack-pad instrumentation forcing frame pointers
   implicitly; this fork instead pins it directly at the `llc` invocation).

## Build-time fetches (recorded exact SHAs, this run)

- Heap-Layers: `9400e88e81cca2ef390f7a0c39d22180397a66e9`
  ("Merge pull request #73 from emeryberger/fix/freesllist-implicit-lifetime"),
  committer date 2026-07-09.
- DieHard: `4467f2b0bacf2e2a4d3a8eb7b89972a4d74ee290`
  ("Merge pull request #24 from emeryberger/fix-largeheap-phantom-insert"),
  committer date 2026-04-26.
- Both fetched live, unpinned, by `common.mk`'s `$(ROOT)/Heap-Layers` and
  `$(ROOT)/DieHard/src/include...` rules — parsa's fork did not change this
  from upstream. A rebuild on a different day will fetch different commits.
  Given the crashes found are inside Stabilizer's own runtime code
  (`FunctionLocation`, `stabilizer_realloc`, `CodeWindow`/`onTimer`) rather
  than inside Heap-Layers/DieHard internals themselves (the faulting frames
  bottom out in `HL::SizeHeap::getSize`/`HL::SegHeap`/`HL::StrictSegHeap`,
  which are being called *correctly-shaped* but with bad pointers from
  Stabilizer's own bookkeeping), Heap-Layers/DieHard version drift is an
  unlikely explanation for what was found, but wasn't independently ruled
  out by pinning to a specific older commit and re-testing.

## Commands log

All commands run via `podman exec` against a long-lived container
(`szc-verify`, from image `stabilizer-llvm21`) with
`/home/matthias/prog/stabilizers/stabilizer-parsa-verify/stabilizer` bind-mounted at
`/work/stabilizer`. Key steps, in order:

```
podman build --authfile empty-auth.json --tag stabilizer-llvm21 --file Containerfile .
podman run --detach --name szc-verify --volume .../stabilizer:/work/stabilizer:Z stabilizer-llvm21 sleep infinity

# rungs 1-2
podman exec --workdir /work/stabilizer szc-verify make
git -C stabilizer/Heap-Layers log -1   # recorded above
git -C stabilizer/DieHard log -1       # recorded above

# rung 3-4: HelloWorld, each mode + all three
podman exec --workdir /work/stabilizer szc-verify python3 szc -v -Rcode -Rstack -Rheap tests/HelloWorld/hello.cpp -o /tmp/hello-all
podman exec --workdir /work/stabilizer szc-verify python3 szc -v -Rcode   tests/HelloWorld/hello.cpp -o /tmp/hello-code
podman exec --workdir /work/stabilizer szc-verify python3 szc -v -Rstack  tests/HelloWorld/hello.cpp -o /tmp/hello-stack
podman exec --workdir /work/stabilizer szc-verify python3 szc -v -Rheap   tests/HelloWorld/hello.cpp -o /tmp/hello-heap
podman exec --workdir /work/stabilizer szc-verify env LD_LIBRARY_PATH=/work/stabilizer /tmp/hello-<mode>

# rung 5: libquantum, via the project's own make (mirrors tests/libquantum/Makefile)
podman exec --workdir /work/stabilizer/tests/libquantum szc-verify make build                              # all 3, default
podman exec --workdir /work/stabilizer/tests/libquantum szc-verify make clean
podman exec --workdir /work/stabilizer/tests/libquantum szc-verify make build CC="../../szc -Rheap"         # heap only
podman exec --workdir /work/stabilizer/tests/libquantum szc-verify make clean
podman exec --workdir /work/stabilizer/tests/libquantum szc-verify make build CC="../../szc -Rcode -Rstack" # code+stack
podman exec --workdir /work/stabilizer/tests/libquantum szc-verify make clean
podman exec --workdir /work/stabilizer/tests/libquantum szc-verify make build CC="../../szc -Rcode"         # code only
podman exec --workdir /work/stabilizer/tests/libquantum szc-verify make clean
podman exec --workdir /work/stabilizer/tests/libquantum szc-verify make build CC="../../szc -Rstack"        # stack only
podman exec --workdir /work/stabilizer/tests/libquantum szc-verify sh -c 'clang -O2 ...*.c -lm -o /tmp/libquantum-ref'  # oracle
podman exec --workdir /work/stabilizer/tests/libquantum szc-verify env LD_LIBRARY_PATH=/work/stabilizer /usr/bin/time -v <binary> 851 2

# rung 6: manual non-PIE relink, bypassing szc's codegen() (driver has no -no-pie switch)
podman exec --workdir /work/stabilizer szc-verify python3 szc -v -Rcode tests/HelloWorld/hello.cpp -o /tmp/hello-pie-test   # keeps .llc.o
podman exec --workdir /work/stabilizer szc-verify clang /tmp/hello-pie-test.llc.o -no-pie -Wl,--emit-relocs -o /tmp/hello-nopie-test -L/work/stabilizer -lstdc++ -lstabilizer
```

Full raw logs: `build-image.log`, `build-01-make.log`, `build-02-hello-all.log`,
`stackonly-851-x2.log`, `ref-851-x2-run{1,2}.txt`.

