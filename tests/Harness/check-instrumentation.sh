#!/bin/sh
# Check the final binaries, so a native compiler override cannot pass CI.
set -eu
if [ "$#" -eq 0 ]; then
    echo "ERROR: no binaries supplied for instrumentation checking" >&2
    exit 1
fi
for binary in "$@"; do
    symbols=$("${NM:-llvm-nm}" "$binary")
    printf '%s\n' "$symbols" | awk '
        $NF == "stabilizer_main" && $(NF-1) == "T" { entry = 1 }
        $NF == "stabilizer_register_function" && $(NF-1) == "U" { code = 1 }
        END { exit !(entry && code) }
    ' || {
        echo "ERROR: missing Stabilizer code instrumentation in $binary" >&2
        exit 1
    }
done
