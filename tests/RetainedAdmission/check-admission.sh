#!/bin/sh
set -eu

# Defaults isolate the controls from a developer's runtime configuration.
export LD_LIBRARY_PATH=.:../..
export STABILIZER_CODE_MODE=retained
export STABILIZER_MAX_EPOCHS=1
export STABILIZER_MAX_CODE_BYTES=67108864
export STABILIZER_INTERVAL_MS=0

reject() {
    name=$1
    diagnostic=$2
    shift 2
    status=0
    timeout --kill-after=1s 10s env "$@" >"$name.out" 2>&1 || status=$?
    if [ "$status" -ne 78 ]; then
        printf '%s: expected exit 78, got %s\n' "$name" "$status" >&2
        cat "$name.out" >&2
        exit 1
    fi
    if ! grep -F -x -- "$diagnostic" "$name.out"; then
        printf '%s: missing rejection diagnostic\n' "$name" >&2
        cat "$name.out" >&2
        exit 1
    fi
}

# Verify the real pass generated both the metadata and instrumentation. The
# native legacy wrapper below separately tests missing historical metadata.
grep -F 'call void @stabilizer_register_module(i32 1, i32 2)' heap.ll
grep -E 'call .*@stabilizer_malloc\(' heap.ll
grep -F 'call void @stabilizer_register_module(i32 1, i32 4)' stack.ll
grep -F 'call void @stabilizer_register_stack_pad(' stack.ll

timeout --kill-after=1s 10s ./admission-plain >plain.out 2>&1
grep -F -x 'retained admission control passed' plain.out

reject heap 'Stabilizer retained mode requires code-only instrumentation and current module metadata' ./admission-heap
reject stack 'Stabilizer retained mode requires code-only instrumentation and current module metadata' ./admission-stack
reject legacy 'Stabilizer retained mode: heap instrumentation is not supported' ./admission-legacy
reject late 'Stabilizer retained mode: registration after startup is not supported' ./admission-late
reject fork 'Stabilizer retained mode: fork is not supported' ./admission-fork
reject wrong-mode 'Stabilizer: STABILIZER_CODE_MODE must be legacy or retained' STABILIZER_CODE_MODE=wrong ./admission-plain
reject zero-epochs 'Stabilizer retained mode: numeric option out of range' STABILIZER_MAX_EPOCHS=0 ./admission-plain
reject overflow 'Stabilizer retained mode: invalid numeric option' STABILIZER_MAX_CODE_BYTES=184467440737095516160 ./admission-plain
reject empty 'Stabilizer retained mode: empty numeric option' STABILIZER_INTERVAL_MS= ./admission-plain
reject negative 'Stabilizer retained mode: invalid numeric option' STABILIZER_INTERVAL_MS=-1 ./admission-plain
reject byte-budget 'Stabilizer retained mode: initial layout exceeds code-byte budget' STABILIZER_MAX_CODE_BYTES=1 ./admission-plain
printf '%s\n' 'Retained admission checks passed'
