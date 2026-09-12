#!/bin/sh
# Run from the repository root. A later successful child must not hide failure.
set -eu
for target in build test clean; do
    make --no-print-directory --file=common.mk ROOT=. \
        RECURSIVE_TARGETS="$target" DIRS=tests/Harness/pass "$target"
    if make --no-print-directory --file=common.mk ROOT=. \
        RECURSIVE_TARGETS="$target" \
        DIRS="tests/Harness/fail tests/Harness/pass" "$target"; then
        echo "ERROR: $target hid a failing child" >&2
        exit 1
    fi
done
