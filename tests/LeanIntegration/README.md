# Lean integration regression

This Linux x86_64 correctness check regenerates `Variants.c` with released
Lean 4.33.1 and links its matching native shared runtime. It exercises the
loop-study workload from Matthias Goergens's faster-lean investigations;
`Variants.lean` is an unchanged copy of that workload. The generated C is
compiled both natively and with Stabilizer's code instrumentation.

Run after building Stabilizer, with LLVM 21 on PATH:

```sh
uv run --no-project --with setuptools==84.0.0 tests/LeanIntegration/check.py --lean-root /path/to/lean-4.33.1-linux --output tests/LeanIntegration/build/results
```

The output directory must be new. `--llvm-bin` can select an explicit LLVM
binary directory. The runner checks the Lean version and embedded commit,
records commands, outputs and input hashes, and leaves generated C/binaries
for inspection. CI verifies the release archive using `lean.sha256` before
extraction. The digest is the GitHub release asset's SHA256 for
[Lean 4.33.1 Linux](https://github.com/leanprover/lean4/releases/tag/v4.33.1).

The driver preserves Lean's generated initialisation and finalisation. It
wraps the real `lean_run_main` call, checks `pthread_self()` inside each
callback, and calls the application twice. Instrumented cases require a
completed relocation epoch and a changed callback destination between calls.
Both `LEAN_MAIN_USE_THREAD=0` and `1` are exercised; `LEAN_NUM_THREADS=1`
alone does not suppress the main worker thread.

Ten variants × native/instrumented × initial/worker thread give 40 fresh
processes. Two further processes check automatic epochs. All runs check two
checksum pairs against an independent integer reference implementation.
A one-generation negative control must reject an epoch request rather than
mistaking retained startup for continuing sampling. Each process has an
external timeout; epoch waiting also has an internal monotonic deadline.

This is a separate CI step, not part of dependency-light `make test`, because
the pinned Lean release archive is approximately 570 MB. CI does not skip it.
The ordinary C pthread regressions remain in `make test`. Printed timings
are ignored: this does not establish overhead, useful measurement accuracy,
unbounded sampling, general TLS/unwind support or instrumented Lean-runtime
compatibility. Compiler and runtime versions must not be mixed when updating
the pin; update the digest and embedded-commit check together.
