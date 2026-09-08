# Running original Stabilizer in a period container — working notes

Started 2026-08-07. Task: get ccurtsinger/stabilizer (LLVM-3.1 era, 2013)
building and minimally running inside a period-correct podman container.
See /home/matthias/prog/stabilizers/stabilizer/RESURRECTION-BRIEF.md, sections "Running
the original against period software" and "House rules for this work".

## Host facts (checked live, 2026-08-07)

- podman 6.0.2, rootless, on this machine.
- Host kernel: `7.1.5-arch1-2.1` (`podman info` — very modern, this is the
  kernel/CPU-policy caveat the brief warns about; a container does NOT roll
  this back).
- Host CPU flags include `ibt` and `user_shstk` — i.e. the CPU supports
  Intel CET (both shadow stack and indirect-branch tracking). Whether CET is
  *enforced* on a given process depends on ELF markings (`GNU_PROPERTY_X86_FEATURE_1_IBT`)
  and glibc/kernel policy — old 2013 binaries carry no such markings, so they
  should run in legacy/compatible mode rather than being rejected outright.
  Worth re-checking empirically rather than assuming.
- `/proc/sys/vm/mmap_min_addr` = 65536 (default-ish, not unusually strict).
- `/proc/sys/kernel/randomize_va_space` = 2 (full ASLR, host default).
- 32 cores, 7.5T free disk under /home/matthias/prog. Not a resource constraint.
- Source cloned from local repo: `git clone /home/matthias/prog/stabilizers/stabilizer
  /home/matthias/prog/stabilizers/stabilizer-period/stabilizer-src` (NOT building inside
  the main repo checkout).

## Plan

1. Base image: try `ubuntu:12.04` first (docker hub), fall back to
   `debian/eol:wheezy` if unpullable. Point apt at old-releases/archive.
2. Era toolchain: gcc/g++ 4.6, python2.7, make, from distro packages.
3. LLVM 3.1 + Clang 3.1: try distro packages (llvm-3.1-dev, clang-3.1) from
   old-releases pool first; else build from release tarballs.
4. Build Stabilizer pass + runtime.
5. Smoke test HelloWorld -> Context -> libquantum (brief).
6. Diagnose any runtime failure: userspace/build vs kernel/CPU policy.

## Log

### Registry auth workaround

`podman pull docker.io/...` failed with "unable to retrieve auth token:
invalid username/password: unauthorized" for *anonymous* pulls, because
`~/.docker/config.json` (host file, not touched) carries a stale
`index.docker.io` credential that podman tries and fails before falling back
to anonymous. Fix used throughout: `--authfile
/home/matthias/prog/stabilizers/stabilizer-period/empty-authfile.json` (contents `{}`)
on every `podman pull`/`podman build`, which bypasses the stale credential
without editing the host docker config.

### Base image

- `docker.io/library/ubuntu:12.04` pulls fine (`podman pull --authfile
  empty-authfile.json`). `/etc/os-release` reports "12.04.5 LTS, Precise
  Pangolin", x86_64.
- As expected for an EOL release, `archive.ubuntu.com` 404s on every precise
  path. Fixed by rewriting `/etc/apt/sources.list` to
  `old-releases.ubuntu.com` (both the `archive.ubuntu.com` and
  `security.ubuntu.com` entries) — see `Containerfile`. After that,
  `apt-get update` succeeds.
- Default `gcc`/`gcc-4.6` in the image: `gcc (Ubuntu/Linaro 4.6.3-1ubuntu5)
  4.6.3` — matches the brief's "GCC 4.6.2" era closely enough (Ubuntu's own
  point release, not a bitwise match, but the same minor series).
- `python2.7` → `Python 2.7.3`.
- Built and tagged as `stabilizer-period:base` from `Containerfile` (log:
  `build-base.log`).

### LLVM 3.1 / Clang 3.1 toolchain — packages vs. source build

- `apt-cache search llvm` against the old-releases pool (Precise +
  precise-updates + precise-security) DOES have `llvm-3.1` / `llvm-3.1-dev`:
  candidate version `3.1-2ubuntu1~12.04.1` (`apt-policy-llvm31.log`).
- **No `clang-3.1` package exists anywhere in that pool.** `apt-cache search
  clang` only turns up plain `clang` (tracks a different/newer LLVM per
  Ubuntu's alternatives system), `clang-3.3`, and `clang-3.4`
  (`apt-search-clang.log`, `apt-policy-clang31.log` — candidate: none).
  Ubuntu precise predates Debian/Ubuntu packaging clang per-LLVM-version
  starting at 3.3; 3.1/3.2-era clang was never packaged standalone for this
  release.
- Decision: build LLVM 3.1 **and** Clang 3.1 from the official release
  tarballs rather than mixing package LLVM libs with a newer clang (bitcode/
  IR compatibility across even minor LLVM versions in this era is not
  guaranteed, and Stabilizer's pass links directly against LLVM internals —
  safest to keep the whole toolchain at exactly 3.1).
- Verified 2026-08-07 that `releases.llvm.org/3.1/{llvm,clang}-3.1.src.tar.gz`
  are still served (HTTP 200, `curl --head`). Downloaded both to
  `src-tarballs/` (persistent, not /tmp):
  - `llvm-3.1.src.tar.gz` sha256
    `1ea05135197b5400c1f88d00ff280d775ce778f8f9ea042e25a1e1e734a4b9ab`
  - `clang-3.1.src.tar.gz` sha256
    `ff63e215dcd3e2838ffdea38502f8d35bab17e487f3c3799579961e452d5a786`
  Sizes match the `content-length` headers from the HEAD request.

### Heap-Layers / DieHard pinning (flagged by coordinator — addressed)

`common.mk` (lines 112-120) clones both `Heap-Layers` and `DieHard` from
their live GitHub HEADs with no pin, at build time. Both repos are still
active in 2026 with modern C++ that GCC 4.6 will not compile, so letting the
Makefile clone HEAD would silently break the build in a way unrelated to
Stabilizer itself — or worse, half-compile something misleading. Fixed by
pinning both to commits from the Stabilizer era and vendoring them into this
directory instead of letting `common.mk` clone live:

- **Heap-Layers**: cloned to `Heap-Layers-src/`, checked out
  `341fff3581be4327bc30ca81e90c6ba513942692` ("Removed print statement.",
  2013-02-07) — the newest commit before 2013-06-01 (Stabilizer's last
  substantive commit is 2013-05-20). Verified present at this commit:
  `heaplayers` (umbrella header, no extension) and `heaplayers.h`, both
  referenced by `runtime/Heap.h`'s `#include <heaplayers>`.
- **`<shuffleheap.h>`** (also included by `runtime/Heap.h`) is **not** in
  Heap-Layers at any point in its history (`git log --all --diff-filter=A --
  '**shuffle*'` in Heap-Layers-src returns nothing) — Stabilizer's runtime
  actually depends on it living in **DieHard**, confirmed by
  `runtime/Makefile`'s `INCLUDE_DIRS` listing `DieHard/src/include` and
  subdirs. (Stabilizer's own repo history shows it *used* to vendor a
  `runtime/shuffleheap/` directory directly — commit `b9a48f6` — but that was
  removed in favour of the external DieHard dependency by the time of the
  current `runtime/Heap.h`.)
- **DieHard**: cloned to `DieHard-src/`, checked out
  `65be5ec133078fe8a5e1fdbbb8c70284f08949b8` ("Removed SSH stuff.",
  2013-01-29) — newest commit before 2013-06-01. Verified
  `src/include/shuffleheap.h` and `src/include/libshuffle.cpp` present, plus
  the `math/`, `rng/`, `static/`, `util/` subdirectories that
  `runtime/Makefile`'s `INCLUDE_DIRS` expects.
- Plan: copy (not clone-at-build-time) `Heap-Layers-src` →
  `stabilizer-src/Heap-Layers` and `DieHard-src` → `stabilizer-src/DieHard`
  before running `make`, so `common.mk`'s auto-clone rule (which only fires
  if the directory is *absent*) never triggers network access at build time.
  Done: both copied in (with their `.git` intact for provenance).

### LLVM 3.1 / Clang 3.1 build — result

Built successfully via the `Containerfile` (`CC=gcc-4.6 CXX=g++-4.6
../llvm-3.1.src/configure --enable-optimized --disable-assertions
--prefix=/opt/llvm-3.1`, then `make --jobs=16` and `make install`), tagged
`stabilizer-period:llvm31`. Two build-log potholes, both fixed and now baked
into the `Containerfile`:

1. First attempt: `mv clang-3.1 llvm-3.1/tools/clang` failed — the release
   tarballs extract to `clang-3.1.src/` and `llvm-3.1.src/` (with the `.src`
   suffix), not `clang-3.1`/`llvm-3.1`. Fixed the directory names in the
   `RUN` line.
2. Second attempt: build succeeded (`clang`/`clang++` installed) but `make
   install` died in `tools/clang/docs` — `groff: not found` while generating
   the man-page `.ps` from `.pod` via `pod2man`/`groff`. Not related to
   Stabilizer at all, just Ubuntu 12.04's minimal image lacking `groff`.
   Added `groff` to the apt package list; full rebuild succeeded.

Verified working: `clang --version` → "clang version 3.1
(branches/release_31)", target `x86_64-unknown-linux-gnu`. `opt`, `llc`,
`llvm-link` all report LLVM 3.1, "Optimized build" (i.e. `--enable-optimized
--disable-assertions` took effect — release, not debug, build). Note: LLVM
3.1's own CPU auto-detection reports "Host CPU: i686" for this machine — its
2012-era cpuid table doesn't recognise a CPU this new, so it falls back to a
generic value. Irrelevant here since nothing in this build passes
`-march=native`, but worth remembering if performance numbers are ever taken
from this toolchain — it will never auto-tune for the host.

### Stabilizer pass + runtime build

`common.mk` defaults `CC=clang CXX=clang++` with no `-I`/`-L` flags anywhere
in `pass/Makefile`, `runtime/Makefile`, or `common.mk` — the original build
assumed LLVM was on the compiler's default search path (i.e. installed to
e.g. `/usr/local` via a prefix-less `make install`, or via a system
`llvm-3.1-dev` package under `/usr/include`). We used `--prefix=/opt/llvm-3.1`
instead, so the first build attempt failed immediately: `fatal error:
'llvm/Pass.h' file not found` in `pass/Stabilizer.cpp`, `'llvm/Module.h' file
not found` in `pass/LowerIntrinsics.cpp`. `runtime/` built fine in that same
attempt (it doesn't touch LLVM headers, only Heap-Layers/DieHard).

Fixed **without touching any Stabilizer source or Makefile**: added
`ENV CPATH=/opt/llvm-3.1/include` and `ENV LIBRARY_PATH=/opt/llvm-3.1/lib` to
the `Containerfile`. Both are honoured implicitly by gcc and clang for
header/library search, so this reproduces exactly what a default
`/usr/local`-prefixed LLVM install would have given the build in 2013,
without editing Stabilizer's own files. Confirmed this was the fix by
testing with `podman run --env CPATH=... --env LIBRARY_PATH=...` before
folding it into the `Containerfile` (appending `ENV` lines at the end is a
cheap layer, so this didn't require re-running the ~expensive LLVM compile).

Result: clean build inside `stabilizer-period:llvm31` with the source
bind-mounted at `/root/stabilizer` (`podman run --volume
.../stabilizer-src:/root/stabilizer:Z ...`) — **not** baked into the image,
so the source tree the container built stays exactly what's in
`stabilizer-src/` on the host, editable/inspectable outside the container.

    pass/Stabilizer.cpp, pass/LowerIntrinsics.cpp -> LLVMStabilizer.so (1.38 MB)
    runtime/{Debug,Function,Heap,Intrinsics,libstabilizer}.cpp -> libstabilizer.so (458 KB), libstabilizer.a (1.04 MB)

Both are Debug builds (`common.mk`'s default `all: debug` target; nothing in
the invocation asked for `release`) — matches how `szc`/`tests/*/Makefile`
invoke `make` by default, so left as-is rather than second-guessing the
upstream default.

### `szc` frontend: no dragonegg path available, use `-frontend clang`

`szc` (the compiler driver) defaults to `-frontend gcc`, which runs `gcc
-fplugin=dragonegg -fplugin-arg-dragonegg-emit-ir` to get LLVM IR out of GCC.
`platforms/Linux.x86_64.mk` sets `SZCFLAGS=` (empty) — i.e. on Linux/x86_64
the upstream default is the dragonegg path, not clang (clang is only forced
by default on Darwin and ppc, see `platforms/Darwin.x86_64.mk` and
`platforms/Linux.ppc.mk`, both `SZCFLAGS = -frontend=clang`).

The old-releases pool has `dragonegg` (`apt-policy-dragonegg.log`), but only
version `3.0-3` — built against LLVM **3.0**, not 3.1, and dragonegg's
plugin ABI is version-sensitive, so mixing it with our LLVM/clang 3.1 would
be its own period-mismatch risk. Simplest and most period-faithful fix:
override the frontend explicitly on the `make` command line, e.g. `make
SZCFLAGS=-frontend=clang test` — `szc` already supports this exact flag
(used verbatim by the Darwin/ppc platform files), so this is invoking an
existing, intended code path, not a workaround. No Stabilizer source changed.

### Smoke test 1: `tests/HelloWorld`

`make SZCFLAGS=-frontend=clang test` in `tests/HelloWorld`, inside
`stabilizer-period:llvm31` with `stabilizer-src` bind-mounted. **Built and
ran cleanly, first try**, full debug trace present (`hello-build-test.log`):

    [libstabilizer.cpp:51] Initializing Stabilizer
    [libstabilizer.cpp:60] Signal handlers installed
    [libstabilizer.cpp:67] Trapped all functions
    [libstabilizer.cpp:71] Set re-randomization timer
    [libstabilizer.cpp:77] Finished with program constructors
    [libstabilizer.cpp:81] Shutting down
    Hello Constructor!
    Hello World!

Every phase of Stabilizer's own startup sequence (trap signal handler
install, `SIGTRAP`-based lazy code trapping, `SIGALRM` re-randomization
timer, constructor call redirection) executed without crashing. HelloWorld
runs in well under 500ms though, so this alone only exercises the *lazy,
first-call* relocation path (each function's `Function::relocate()` fires
once via the `SIGTRAP` trap when first entered) — it does **not** by itself
prove the timer-driven re-randomization loop survives repeatedly. See next.

### Smoke test 2: `manual-tests/loop.cpp` — proving actual re-randomization

Wrote a small standalone program (`manual-tests/loop.cpp`, not part of the
vendored `tests/`, so the original tree stays untouched) that loops for
~3 seconds (300 × 10ms `usleep`, calling a deliberately non-trivial
`__attribute__((noinline))` function each iteration) — long enough to cross
Stabilizer's default 500ms re-randomization interval (`libstabilizer.cpp`:
`size_t interval = 500;`) multiple times.

Compiled directly with `szc -frontend clang -Rcode -Rheap -Rstack`
(`loop-build.log` — the `-v` trace shows the full pipeline: `clang -O0
-emit-llvm` → `llvm-link` → `opt -O2 -load=LLVMStabilizer.so
-stabilize-code -stabilize-stack -stabilize-heap -stabilize` → `llc
-relocation-model=pic` → `clang` final link against `libstabilizer`).

Run three times (`loop-run.log`, `loop-repeat-run.log`), **no crash, exit 0
every time, identical correct output** (`done, acc=-5898811395913406765` —
deterministic given the fixed PRNG-like recurrence, unaffected by layout, as
expected) despite each run relocating code/stack/heap to different random
addresses. Debug trace confirms the full re-randomization cycle firing
repeatedly, not just once:

    grep -c 'Re-randomization timer fired'          -> 6
    grep -c 'Re-randomization started after trap'   -> 6

Six full `SIGALRM` → "Placing traps" → next-call `SIGTRAP` → "Re-randomization
started after trap" → `Function::relocate()` cycles over one ~3s run (500ms ×
6 ≈ 3s), each one moving the hot function to a fresh randomized location and
resuming execution correctly. **This is the load-bearing result of the whole
exercise: Stabilizer's core claim — repeated, in-process, live code/stack/heap
relocation via self-modifying trap patching — works, unmodified, on a
2026 kernel (`7.1.5-arch1-2.1`) and a 2024-era CPU (13th Gen Intel Core
i9-13900K) that supports CET (IBT + shadow stack), inside a podman rootless
container with no special kernel flags, seccomp overrides, or CPU-feature
disabling.**

### Why the kernel/CPU caveat did NOT block this — checked, not assumed

The brief is explicit that a container only fixes userspace, and the runtime
touches exactly the policy surfaces (W^X, CET/IBT, ASLR, `mmap_min_addr`)
that a container can't roll back. Checked each one against the actual
built/run binary rather than assuming the pass was clean luck:

- **W^X / RWX mmap**: `runtime/Heap.h` maps the code heap with `CodeProt =
  PROT_READ | PROT_WRITE | PROT_EXEC` (simultaneously writable and
  executable) and the runtime later self-modifies code in place (`Trap.h`'s
  `0xCC` int3 patch, `Jump.h`'s hand-built `jmp`/`mov+ret` trampolines,
  `Function::copyTo`/`relocate` in `Function.cpp`). This is exactly what a
  strict W^X policy (SELinux, some hardened seccomp profiles) would deny.
  Six successful re-randomization cycles across three separate runs is
  direct evidence this host/container combination does not enforce W^X
  against `mmap(PROT_READ|WRITE|EXEC)` — confirmed behaviourally, not by
  reading a policy file.
- **CET (IBT/shadow stack)**: host CPU flags include `ibt` and `user_shstk`
  (`/proc/cpuinfo`, checked at the top of this doc) — the silicon supports
  both halves of CET. But `readelf -n` on the built `loop` binary shows
  **no `GNU_PROPERTY_X86_FEATURE_1_*` note at all** (only the standard
  `NT_GNU_ABI_TAG`/`NT_GNU_BUILD_ID`), and `objdump -d` finds **zero**
  `endbr64` instructions anywhere in the binary (`loop-binary-check.log`) —
  clang 3.1 predates CET code generation entirely. Per the Linux/glibc CET
  ABI, the kernel only *enables* CET enforcement for a process if the
  binary (and every loaded shared object) carries the marking; an unmarked
  binary runs in plain legacy mode regardless of what the CPU supports. So
  this isn't "CET happened to not trigger" — the binary structurally never
  asks for it, and both of `Jump.h`'s hand-crafted control-flow tricks (a
  raw `0xE9` relative jump, and a `sub rsp,8`/`mov`/`mov`/`retq` sequence
  that fabricates a return address for an indirect jump) executed fine,
  including the `retq`-based one, which is precisely the shape a shadow
  stack exists to catch — and did not, because shadow stack enforcement
  was never turned on for this process.
- **ASLR / PIE / `MAP_32BIT`**: `file ./loop` reports a plain "ELF 64-bit
  LSB **executable**" (not "pie executable" / "shared object") —
  clang 3.1 does not default to `-pie`, matching 2012 toolchain norms. The
  main binary therefore loads at its fixed link-time address regardless of
  the host's `randomize_va_space=2`; only mmap'd regions (heap, stack
  overflow area, shared libs) get ASLR'd, which is what Stabilizer's own
  code/data heaps use `MAP_32BIT` for in the first place (to keep
  `X86Jump32`'s 32-bit relative displacement in range) — and that worked
  too, with no allocation failures across any of the runs.
- **`mmap_min_addr`**: 65536 on this host, an ordinary default, never a
  factor since Stabilizer's heaps are placed by the kernel (`MAP_32BIT`,
  not a fixed low address), not user-chosen near-NULL addresses.

Net: on this specific host (Arch Linux, kernel 7.1.5, 13th Gen Intel,
rootless podman), **none of the kernel/CPU policy risks flagged in the brief
turned out to be live blockers** — not because the risks were wrong to flag,
but because 2012-vintage binaries never opt into the modern protections that
would have caught them (no CET markings, no PIE, no W^X-hardened profile
active on this container/host combination). This is itself the empirical
answer to "is a container sufficient" for *this* host: yes, here. It is not
a general proof — a host with `MemoryDenyWriteExecute`-style enforcement (a
systemd unit setting, common in hardened server configs), a hardened SELinux
policy, or a distro kernel with CET enforced by default even for unmarked
binaries would very plausibly reject the RWX mmap or the trap-driven control
flow. That the risk is real in principle but this machine doesn't happen to
enforce it is the actual, precise finding — worth stating explicitly rather
than either overselling ("Stabilizer runs fine on modern Linux") or
underselling ("kernel/CPU policy makes this unrunnable").

### A real (and unrelated) interaction found: `strace` breaks it

Running the same `loop` binary under `strace -f` **does** crash it — SIGILL,
core dumped, immediately after "Finished with program constructors" (i.e. on
entry to the very first `SIGTRAP`-trapped call) — `loop-strace-full-run.log`.
Running the identical binary directly (no `strace`) is reliable (three
clean runs, above). This is very likely the well-known conflict between
`ptrace`-based tracers and `int3`(`0xCC`)-based self-modifying trampolines:
under `ptrace`, a tracee's own `SIGTRAP` becomes a ptrace-stop that the
tracer must explicitly forward, and the interaction with a runtime that is
*itself* installing and removing `0xCC` breakpoints as its core mechanism is
exactly the kind of thing expected to be fragile under a second, independent
ptrace-based tool. **Not a kernel/CPU hardening finding** — this class of
conflict predates all the CET/W^X concerns above and would very likely have
affected the original 2013 Stabilizer under `strace`/`gdb` too. Noted so a
future session doesn't waste time trying to `strace` its way to more
confidence and gets confused by the crash.

### `tests/Context` — out of scope for Linux, confirmed Darwin-only

The brief flagged this as "presumably a runtime context-switch test" worth
trying. Checked rather than assumed: `tests/Context/Makefile` assembles
`stub.asm` with `yasm -f macho64` (Mach-O 64-bit — the **macOS** object
format, not ELF), and `stub.asm` itself uses leading-underscore symbol names
(`_stub`, `_doStuff`, `_saveState`) — the classic macOS C symbol-mangling
convention, not Linux's. This test cannot run against a Linux target without
non-trivial porting (object format, symbol naming, `MAP_32BIT`/registers use
already assume a specific ABI) that upstream never did — it's a Darwin-only
test that happens to sit in the shared `tests/` directory. Consistent with
the brief's own steer ("Darwin and ppc support: almost certainly drop rather
than port"). Skipped rather than spending time porting a test for a platform
already out of scope; not a failure of the Linux/x86_64 port.

### `tests/libquantum` — real SPEC CPU2006 benchmark, ran clean

`make SZCFLAGS=-frontend=clang test` (`-DSPEC_CPU -DSPEC_CPU_MACOSX` in
`CFLAGS` despite the Linux target — an upstream artefact, harmless: those
macros only gate a couple of `#ifdef` blocks in SPEC's harness code, not the
Linux/Darwin runtime split). Compiled all 16 source files through the full
`szc` pipeline with `-Rcode -Rheap -Rstack`, linked, and ran `libquantum 128`
(Shor's algorithm factoring 128) to completion (`libquantum-build-test.log`):

    N = 128, 37 qubits required
    Random seed: 125
    Measured 1024 (0.062500), fractional approximation is 1/16.
    Possible period is 16.
    128 = 32 * 4

Correct result, clean exit, two harmless `-Wformat` warnings (`%i` vs `%li`
in `gates.c`, pre-existing in the 2013 source, unrelated to Stabilizer). This
is a genuine, non-trivial SPEC benchmark exercised end-to-end through the
period pass+runtime, not just a hello-world.

## Summary / status

**Built and ran, in full.** LLVM 3.1 + Clang 3.1 built from the official
release tarballs (no distro `clang-3.1` package exists), Stabilizer's LLVM
pass and runtime built clean against them, and three programs of increasing
weight (`HelloWorld`, a purpose-written re-randomization stress test, and
the `libquantum` SPEC CPU2006 benchmark) all ran correctly under full
`-Rcode -Rheap -Rstack` randomization inside a podman rootless container
with an Ubuntu 12.04 / LLVM 3.1 / GCC 4.6.3 / Python 2.7.3 userspace, on top
of an unmodified 2026 Arch Linux host (kernel 7.1.5, 13th Gen Intel Core
i9-13900K with CET support).

**Key finding**: the kernel/CPU-policy blockers the brief flagged as the
real risk (W^X, CET/IBT shadow stack, ASLR/PIE, `mmap_min_addr`) did **not**
materialise on this host — checked empirically (RWX mmap succeeds, six
re-randomization cycles complete, `readelf`/`objdump` confirm the binary
carries no CET markings so the CPU's CET support is simply inert for it, the
binary is non-PIE so it isn't subject to load-address ASLR). This is a
host-specific empirical result, not a general proof that no modern kernel
would block it — a host with `MemoryDenyWriteExecute=yes`-style hardening or
a CET-enforce-by-default kernel policy remains a plausible blocker
elsewhere, untested here. On *this* machine, a period container turned out
to be sufficient; no VM-with-period-kernel escalation was needed.

**Secondary finding, unrelated to kernel/CPU policy**: `strace -f` reliably
crashes the runtime (SIGILL) via what looks like a `ptrace`-vs-`int3`
self-modifying-trampoline conflict — a debugging-tool limitation, not a
hardening one, and plausibly present in 2013 too.

**Not attempted / explicitly out of scope**: `tests/Context` (Mach-O/Darwin
object format and symbol convention, confirmed by reading `stub.asm` and its
Makefile — not a Linux test), `tests/bzip2` and `tests/perlbench` (not run;
`libquantum` alone was enough to confirm a real SPEC-CPU2006-scale benchmark
works, per the brief's "one real benchmark ... briefly"), the `-Rlink`
(link-only, per-build) config and `run.py`'s full factorial design (belongs
to task 4's baseline measurement, not this task).

## Artefacts in this directory

- `Containerfile` — full period-container build (Ubuntu 12.04 base,
  old-releases sources, era toolchain, LLVM 3.1 + Clang 3.1 from source).
- `stabilizer-src/` — the working clone (Heap-Layers and DieHard vendored in
  at pinned period commits; do not let anything re-clone their HEADs).
- `Heap-Layers-src/`, `DieHard-src/` — the pinned upstream checkouts these
  were copied from (kept for provenance/re-vendoring if `stabilizer-src` is
  ever reset).
- `src-tarballs/` — the LLVM 3.1 / Clang 3.1 release tarballs (sha256 above).
- `manual-tests/loop.cpp` — the re-randomization stress test written for
  this exercise.
- `empty-authfile.json` — workaround for a stale `docker.io` credential in
  the host's `~/.docker/config.json` breaking anonymous `podman pull`; pass
  `--authfile` pointing at this on every `podman pull`/`build`.
- `*.log` — raw build/run logs referenced throughout this file.
- `libquantum-851-results/` — the follow-up experiment below (binaries,
  stdout/stderr/exit/timing per mode, diffs against the oracle).

## Follow-up: `libquantum 851 2` per-mode re-randomization epochs (2026-08-07)

An adversarial review of the write-up above caught a real gap: the
`libquantum 128` run used for the initial smoke test (`libquantum-build-test.log`)
contains **zero** "Re-randomization" lines — it finished before the first
500ms timer tick fired even once. So up to this point, Stabilizer's core
claim (repeated in-process re-randomization) had only been demonstrated on
the trivial hand-written `manual-tests/loop.cpp`, never on a real SPEC
benchmark. This section fixes that: `libquantum 851 2` (the heavier
"parsa verification" input, reference runtime ~29s, expected on the order of
tens of `SIGALRM` epochs), run under each of `-Rcode`, `-Rstack`, `-Rheap`
separately and then all three combined, each diffed against an
uninstrumented reference build for correctness.

### Pinned dependency commits — explicit, for downstream reference

These are the exact commits vendored into `stabilizer-src/Heap-Layers` and
`stabilizer-src/DieHard` (see "Heap-Layers / DieHard pinning" above for the
reasoning; recorded here again, explicitly, since a downstream diagnostic
needs them without hunting through prose):

    Heap-Layers: 341fff3581be4327bc30ca81e90c6ba513942692
                 "Removed print statement." — 2013-02-07 09:01:51 -0500
                 (github.com/emeryberger/Heap-Layers)

    DieHard:     65be5ec133078fe8a5e1fdbbb8c70284f08949b8
                 "Removed SSH stuff." — 2013-01-29 13:16:49 -0500
                 (github.com/emeryberger/DieHard)

Re-verified live against the actual vendored trees on 2026-08-07 (`git
rev-parse HEAD` inside `stabilizer-src/Heap-Layers` and `stabilizer-src/DieHard`
— both match exactly what's above; not just copied from earlier prose).

### Correctness oracle

Built directly with the container's `clang` (LLVM 3.1), **not** through
`szc`, no Stabilizer pass, no `libstabilizer` link — isolates "did
Stabilizer's transform/runtime change program behaviour" as the only
variable under test:

    clang -O2 -DSPEC_CPU -DSPEC_CPU_MACOSX -o libquantum-oracle \
      classic.c complex.c decoherence.c expn.c gates.c matrix.c measure.c \
      oaddn.c objcode.c omuln.c qec.c qft.c qureg.c shor.c specrand.c version.c \
      -lm

(`-lm` needed — `gates.c`/`complex.c` etc. use `sin`/`cos`/`sqrt`/sim.,
confirmed by grep before assuming.) Clean build, same two pre-existing
`-Wformat` warnings seen throughout (`%i` vs `unsigned long` in `gates.c`,
upstream 2013 issue, unrelated to Stabilizer).

Ran `./libquantum-oracle 851 2`: **exit 0, 36.1s wall time** (`time`,
`oracle-run-time.log`). Output (`libquantum-oracle.stdout`), used as the
correctness oracle for every mode below:

    N = 851, 52 qubits required
    Random seed: 2
    Measured 985026 (0.939394), fractional approximation is 31/33.
    Odd denominator, trying to expand by 2.
    Possible period is 66.
    Unable to determine factors, try again.

("Unable to determine factors" is Shor's algorithm's own legitimate
probabilistic outcome for this seed/input, not a bug — `spec_srand(26)` is a
fixed seed so this is deterministic and reproducible, which is exactly why
diffing stdout across builds is a valid correctness check here.)

### Per-mode builds and runs

Each mode: `make clean` in `tests/libquantum`, then `make CC="../../szc
-frontend=clang -R<mode(s)>" CXX="../../szc -frontend=clang -R<mode(s)>"
build` (overriding the Makefile's hardcoded `CC = $(ROOT)/szc $(SZCFLAGS)
-Rcode -Rheap -Rstack` on the command line — command-line variables win over
a plain `=` assignment in the Makefile, and `CXX = $(CC)` being
recursively-expanded means it picks up the override too; verified this
produces the intended single-mode builds by inspecting each `-v` build log).
Ran each resulting binary as `LD_LIBRARY_PATH=stabilizer-src ./libquantum-<mode>
851 2`, `timeout 300`, stdout/stderr captured to **separate** files (the
`DEBUG()` macro in `runtime/Debug.h` writes to `stderr`, program output goes
to `stdout` — this is what makes clean diffing possible without stripping
debug noise out of the oracle comparison by hand).

| mode | exit | wall time | timer fires | epochs completed | diff vs oracle |
|---|---|---|---|---|---|
| `-Rcode` alone | 0 | 1m29.805s (independent rerun; first run untimed) | 173 | 173 (`Re-randomization started after trap`) | **identical** |
| `-Rstack` alone | 0 | 1m26.615s | 173 | 173 (`Re-randomizing stack pads` branch, see below) | **identical** |
| `-Rheap` alone | 0 | 1m24.818s | 169 | 169 (same stack-pad-branch no-op, see below) | **identical** |
| `-Rcode -Rheap -Rstack` | 0 | 1m27.028s | 173 | 173 (`Re-randomization started after trap`) | **identical** |

(`-Rcode`'s wall time is from a second, independent rerun of the saved
binary — used to get a properly-timed number without re-running all four
modes — which also re-confirms determinism: same `exit=0`, output still
byte-identical to the oracle on a run that, by construction, relocated
functions to a *different* set of random addresses than the first run.)

**No crash in any of the four modes.** All four `diff oracle.stdout
mode.stdout` are empty (`diff-code.txt`, `diff-stack.txt`, `diff-heap.txt`,
`diff-all.txt` — all zero-length, `diff` exit 0). Every mode reproduces the
oracle's exact text, including the "Unable to determine factors" line,
despite each one relocating/re-randomizing a real, non-trivial, heavily
memory-allocating SPEC benchmark roughly 170 times over its run — this is
the epoch-bearing evidence the coordinator asked for, on a real benchmark
rather than the toy loop.

**Important nuance, worth being precise about rather than reporting a flat
"170 epochs" for every mode**: the runtime's `onTimer()` handler
(`runtime/libstabilizer.cpp`) branches on `functions.size() == 0`. Only
`-Rcode` (via the `stabilize-code`/`stabilize` opt passes) calls
`stabilizer_register_function`, populating that set. So:

- **`-Rcode` alone and the combined run**: `functions.size() > 0`, so every
  timer fire takes the "Placing traps" branch, and the *next* call into a
  live function raises the `SIGTRAP` that prints "Re-randomization started
  after trap" and actually calls `Function::relocate()` — a genuine
  code-relocation epoch. Timer-fire count and completed-epoch count match
  exactly (173/173), i.e. every timer tick successfully drove a real
  relocation before the program exited.
- **`-Rstack` alone and `-Rheap` alone**: `functions.size() == 0` (no
  `Function` objects registered at all, since only the code pass registers
  them), so `onTimer()` takes the *other* branch every time —
  "Re-randomizing stack pads" (`libstabilizer.cpp:177`) — confirmed via
  `grep 'Re-randomizing stack pads'` matching the full timer-fire count in
  both stderr logs, and `Re-randomization started after trap` at 0 in both.
  For `-Rstack` this branch is doing real work (iterating `stack_pads` and
  writing a fresh random byte into each — that set is non-empty when stack
  stabilization is active). For `-Rheap` alone, `stack_pads` is empty too
  (no stack pads registered without `-Rstack`), so this branch is a
  near-no-op each time it fires — **which is expected, not a bug**: heap
  randomization in this design isn't epoch-gated at all.  Layout
  randomization for the heap comes from the `ShuffleHeap` allocator itself
  (`runtime/Heap.h`'s `KingsleyHeap<ShuffleHeap<DataShuffle, DataSource>,
  ...>`, confirmed by reading `Heap.cpp`/`Heap.h`) — it shuffles allocation
  placement on *every* `malloc`, continuously, not on a timer. So "epochs
  completed" for `-Rheap` alone is honestly better read as "169 timer ticks
  survived cleanly during sustained heavy allocation" rather than "169 heap
  re-randomization events" — there's no such discrete event in the source to
  count. The correctness evidence for heap randomization itself is the
  clean diff against the oracle under continuous shuffled allocation for a
  ~85s run of a real workload, not the timer-fire count.

This means the flat table above slightly overstates uniformity: `-Rcode`
and the combined run have 173/173 *bona fide relocation* epochs each;
`-Rstack` has 173 *bona fide stack-pad* epochs; `-Rheap` has 169 *timer
ticks with no corresponding discrete event to count*, and its real evidence
is allocator-level, not epoch-level. All four are still unambiguous passes
— none crashed, none produced wrong output — but "epochs" doesn't mean
quite the same thing in all four rows and shouldn't be quoted as a single
undifferentiated number without this caveat.

Coordinator's ~58-epoch estimate (29s reference ÷ 0.5s) was in the right
order of magnitude but low: the instrumented builds here ran ~85-87s wall
time (vs. the oracle's 36.1s), roughly 2.4× the oracle, consistent with
per-epoch overhead (sweeping stale function locations, re-arming traps,
relocation memcpy's) on top of a benchmark this size — a real, if
unsurprising, overhead data point for whatever eventually measures
Stabilizer's runtime cost properly (task 4's `BASELINE.md`, not this task).

### Files

`libquantum-851-results/`: `libquantum-oracle{,.stdout,.stderr,.exit}`,
`libquantum-{code,stack,heap,all}{,.stdout,.stderr,.exit,.time}`,
`build-{code,stack,heap,all}.log`, `run-{code,stack,heap,all}.log`,
`diff-{code,stack,heap,all}.txt` (all empty — clean diffs), `oracle-build.log`,
`oracle-run-time.log`.

### Updated status

The "built and ran" claim from the first pass now has a second, load-bearing
leg: not just a hello-world and a synthetic loop, but a real SPEC CPU2006
benchmark, run to completion, output-identical to an uninstrumented build,
under all three individual randomization dimensions and the combined mode,
each surviving on the order of 170 in-process re-randomization epochs (or,
for `-Rheap`, 169 timer ticks over sustained real allocation) without a
single crash or output divergence. Nothing in this follow-up changes the
kernel/CPU-policy discussion above — same host, same binaries' lack of CET
markings, same non-PIE executables — it just replaces the previously-missing
"survives an actual re-randomization epoch on real work" evidence with a
direct, repeated, four-mode measurement.

