#!/bin/sh
set -eu
# Start from a built fixture, so a stale source/Makefile cannot make this check
# pass for the wrong reason. A hypothetical header edit must schedule a compile.
make --question entry-publication
make --dry-run --what-if=../../runtime/FunctionHeader.h entry-publication \
    > dependency-check.out
grep --fixed-strings ' -c entry-publication.cpp ' dependency-check.out
