#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/tools/cputest"
CC=${CC:-cc}
CXX=${CXX:-c++}
CFLAGS="-O2 -DSNCPU_TEST=1 -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -I$ROOT/src/common/base -I$ROOT/src/snes/core -I$ROOT/src/snes/cpu"

$CC $CFLAGS -ffunction-sections -fdata-sections -c "$ROOT/src/snes/cpu/sncpu.c" -o "$OUT/sncpu.o"
$CC $CFLAGS -ffunction-sections -fdata-sections -c "$ROOT/src/snes/cpu/sncpu_c.c" -o "$OUT/sncpu_c.o"
$CXX $CFLAGS -ffunction-sections -fdata-sections -c "$OUT/cpu_test.cpp" -o "$OUT/cpu_test.o"
$CXX -Wl,--gc-sections "$OUT/cpu_test.o" "$OUT/sncpu.o" "$OUT/sncpu_c.o" -o "$OUT/cpu_test"
