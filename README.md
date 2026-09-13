## Stabilizer: Statistically Sound Performance Evaluation
[Charlie Curtsinger](https://curtsinger.cs.grinnell.edu/) and [Emery D. Berger](https://www.emeryberger.com)
University of Massachusetts Amherst

### Abstract

Researchers and software developers require effective performance
evaluation. Researchers must evaluate optimizations or measure
overhead. Software developers use automatic performance regression tests to discover when changes improve or degrade performance.
The standard methodology is to compare execution times before and
after applying changes.

Unfortunately, modern architectural features make this approach
unsound. Statistically sound evaluation requires multiple samples
to test whether one can or cannot (with high confidence) reject the
null hypothesis that results are the same before and after. However,
caches and branch predictors make performance dependent on
machine-specific parameters and the exact layout of code, stack
frames, and heap objects. A single binary constitutes just one sample
from the space of program layouts, regardless of the number of runs.
Since compiler optimizations and code changes also alter layout, it
is currently impossible to distinguish the impact of an optimization
from that of its layout effects.

**Stabilizer** is a system that enables the use of
the powerful statistical techniques required for sound performance
evaluation on modern architectures. Stabilizer forces executions
to sample the space of memory configurations by repeatedly rerandomizing layouts of code, stack, and heap objects at runtime.
STABILIZER thus makes it possible to control for layout effects.
Re-randomization also ensures that layout effects follow a Gaussian
distribution, enabling the use of statistical tests like ANOVA. We
demonstrate Stabilizer's’s efficiency (< 7% median overhead) and
its effectiveness by evaluating the impact of LLVM’s optimizations
on the SPEC CPU2006 benchmark suite. We find that, while `-O2`
has a significant impact relative to `-O1`, the performance impact of
`-O3` over `-O2` optimizations is indistinguishable from random noise.

A full description of Stabilizer is available in the 
[technical paper](http://www.cs.umass.edu/~emery/pubs/stabilizer-asplos13.pdf), which appeared at
ASPLOS 2013.

See also this [nice blog post](https://fgiesen.wordpress.com/2017/09/02/papers-i-like-part-5/) about this research.

### Building Requirements

_NOTE: This project was originally built for LLVM 3.1. This repository has been updated to work with modern LLVM toolchains (tested with LLVM 21)._

Stabilizer runs on OSX and Linux, and supports x86, x86_64, and PowerPC.

Stabilizer requires LLVM and the Clang front-end. Stabilizer's build system assumes LLVM include files will be accessible through your default include path.

The legacy GCC + Dragonegg (Dragonegg/DragonEgg) frontend is no longer supported. For C/C++ inputs, `szc` uses clang. For Fortran inputs, `szc` uses LLVM Flang (`flang` / `flang-new`).

Stabilizer's compiler driver `szc` is written in Python 3. It uses the `argparse` module.

### Building Stabilizer
```
$ git clone git://github.com/ccurtsinger/stabilizer.git stabilizer
$ make
```

By default, Stabilizer is build with debug output enabled.  Run 
`make clean release` to build the release version with asserts and debug output 
disabled.

### Using Stabilizer
Stabilizer includes the `szc` compiler driver, which builds programs using the 
Stabilizer compiler transformations.  `szc` passes on common GCC flags, and is 
compatible with C, C++ and Fortran inputs.

To compile a program in `foo.c` with Stabilizer, run:
```
$ szc -Rcode -Rstack -Rheap foo.c -o foo
```

The `-R` flags enable randomizations, and may be used in any combination.
Stabilizer uses clang as its front-end. The legacy `-frontend=gcc`/Dragonegg path is no longer supported. For Fortran inputs (`-lang=fortran`), `szc` uses LLVM Flang (`flang` / `flang-new`).

The resulting executable is linked against with `libstabilizer.so` (or `.dylib` 
on OSX). Place this library somewhere in your system's dynamic library search
path or (preferably) add the Stabilizer base directory to your `LD_LIBRARY_PATH`
or `DYLD_LIBRARY_PATH` environment variable.

### SPEC CPU2006
The `szchi.cfg` and `szclo.cfg` config files can be installed in a SPEC CPU2006
config directory to build and run benchmarks with Stabilizer. The szchi config 
`-O2` for base and `-O3` for peak tuning, and szclo uses `-O0` and `-O1`.

The `run.py` and `process.py` scripts were used to drive experiments and
collect results. The run script accepts optimization levels, benchmarks to
enable (or disable with a "-" prefix), a number of runs, and build 
configurations in any order.  For example:

```
$ ./run.py 10 bzip2 code code.stack code.heap.stack
```
This will run the `bzip2` benchmark 10 times in each of three randomization
configurations. The `runspec` tool must be in your path, so `cd` to your SPEC
installation and `sourceh shrc` first.

```
$ ./run.py 10 -astar code link O2 O3
```
This will run every benchmark except `astar` 10 times with link randomization
at `-O2` and `-O3` optimization levels.

Be warned: there is no easy way to distinguish `O2` and `O0` results after the
fact: both are marked as "base" tuning.  Keep these results in separate 
directories.

The process script reads `.rsf` files from SPEC and provides some summary
statistics, or collects results in an easy-to-process format.

```
$ ./process.py $SPEC/result/*.rsf
```
This will print average runtimes for each benchmark in each configuration and
tuning level for the runs in your SPEC results directory.

Pass the `-trim` flag to remove the highest and lowest runtimes before computing 
the average.

The `-norm` flag tests the results for normality using the Shapiro-Wilk test.

The `-all` flag dumps all results to console, suitable for pasting into a
spreadsheet or CSV file.

## Experimental retained code generations (Linux x86_64)

Code randomisation sets `frame-pointer=all` on instrumented definitions,
including imported bitcode with conflicting frame-pointer attributes. Naked
functions are rejected under `-Rcode` because they lack a normal prologue.
This does not add frame pointers to native libraries or make arbitrary native
stacks unwindable.

The default runtime still assumes a single application stack when reclaiming
relocated code. It is not safe for general pthread workloads. The opt-in
`STABILIZER_CODE_MODE=retained` mode instead prepares all initial code copies
before deferred constructors/main and uses a normal maintenance thread to
publish later copies. Old copies are retained, including through shutdown,
so a worker blocked in native code can return to an earlier generation.
This is bounded experimental sampling, not a complete thread-safe replacement
for all Stabilizer modes or an unbounded reclamation implementation.

Rebuild with the current pass and use `-Rcode` only. Heap and stack
instrumentation are rejected, including reported mixed-module instrumentation.
Legacy unreported heap-wrapper calls are rejected before touching the shared
runtime heap. Module metadata cannot establish that every old object has been
rebuilt; compatible instrumentation throughout the executable is required.

Configuration (strict unsigned decimal values):

| Variable | Default | Meaning |
|---|---|---|
| `STABILIZER_MAX_EPOCHS` | `8` | Maximum completed generations, including the initial eager generation; range 1–1000000 |
| `STABILIZER_MAX_CODE_BYTES` | `67108864` | Logical copied body-byte budget; range 1–1073741824 |
| `STABILIZER_INTERVAL_MS` | `500` | Delay between publication passes; range 0–3600000; zero means explicitly requested epochs only |

The smaller generation/byte budget wins. The byte budget is **not an RSS
limit**: allocation chunks, bins, metadata and shuffle overhead add memory.
Once exhausted, sampling stops and the final layout stays active. Startup
prints an experimental-mode notice; exhaustion is exposed by the API, not
logged by the maintenance thread. Measurements must record/check that state
rather than silently treating the remainder as continuing layout sampling.

The C API in `runtime/Instrumentation.h` provides a completed-epoch counter,
an exhaustion flag, a published-location query and `stabilizer_request_epoch`.
Requests coalesce; an accepted request is not a promise of one unique epoch.
Shutdown cancels pending requests; callers waiting for progress need a deadline.
The counter advances after a full publication pass; individual function
destinations are updated sequentially, not as an atomic whole-program switch.
Location queries expose observations, not ownership or a reclamation API.

Initial header installation requires quiescence: native DSO constructors must
not start concurrent calls into instrumented executable text before runtime
initialisation. Instrumented registration after startup is rejected, as is
ordinary `fork()` in retained mode. Local-exec TLS offsets (`R_X86_64_TPOFF32`)
are preserved across code relocation and tested with distinct thread values
and stable per-thread addresses. The LLVM TLS-address intrinsic is outlined
into small fixed, non-inlined helpers; these are not randomised. Canonical
Clang TLS therefore uses native compiler/linker lowering, including tested
PIC global-dynamic and initial-exec access to TLS in a native shared library.
This adds a helper call and excludes the resolver itself from layout sampling;
its measurement cost has not been established. Direct TLS IR forms that still
emit TLSGD/TLSLD/GOTTPOFF/TLSDESC in copied bodies are rejected because retained
linker relocation labels may describe instructions that were already relaxed.
Do not infer support from a TLS type merely being PC-relative before linking.
Raw clone/fork system calls, instrumented
module unloading and general exception unwinding are not
supported by this prototype. Stop/join runs on normal main return and via
`atexit`; retained code is not freed there because later exit callbacks may
still enter it. No performance or statistical-normality claim follows from
passing the correctness regressions.
