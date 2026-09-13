#!/bin/sh
set -eu
ulimit -c 0
status=0
LD_LIBRARY_PATH=../.. timeout --kill-after=1s 5s ./alignment-unaligned \
    > alignment-unaligned.out 2>&1 || status=$?
cat alignment-unaligned.out
test "$status" -ne 0
test "$status" -ne 124
test "$status" -ne 137
grep --fixed-strings 'is not aligned to 8 bytes' alignment-unaligned.out
if grep --fixed-strings 'aligned entry passed' alignment-unaligned.out; then
    echo 'ERROR: unaligned fixture reached application code'
    exit 1
fi
