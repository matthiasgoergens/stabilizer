#!/bin/sh
set -eu
ulimit -c 0
status=0
LD_LIBRARY_PATH=../.. STABILIZER_CODE_MODE=retained STABILIZER_MAX_EPOCHS=1 timeout --kill-after=1s 5s "$1" >pic.out 2>&1 || status=$?
test "$status" -eq 134
grep --fixed-strings "TLS relocation $2 requires linker-relaxation-aware support" pic.out
