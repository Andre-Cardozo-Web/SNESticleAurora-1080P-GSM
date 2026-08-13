#!/usr/bin/env bash
# Bancada host-side para regressões pequenas do renderer SNES/PPU.
#
# Uso:  cd tools/pputest && ./build.sh && ./obj_test
set -e
cd "$(dirname "$0")"
ROOT=../..

"${CXX:-g++}" -O2 -ffunction-sections -fdata-sections \
    -Wl,--gc-sections \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 -DSNDBG_LOG=0 \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/common/render" \
    -I "$ROOT/src/common/debug" \
    -I "$ROOT/src/snes/ppu" \
    -I "$ROOT/src/snes/core" \
    -I "$ROOT/src/snes/cpu" \
    -I "$ROOT/src/snes" \
    -I "$ROOT/src" \
    obj_test.cpp "$ROOT/src/snes/ppu/snppuobj.cpp" -o obj_test

"${CXX:-g++}" -O2 -ffunction-sections -fdata-sections \
    -Wl,--gc-sections \
    -DCODE_PLATFORM=1 -DCODE_DEBUG=0 -DCODE_PROFILE=0 -DSNDBG_LOG=0 \
    -I "$ROOT/src/common/base" \
    -I "$ROOT/src/common/render" \
    -I "$ROOT/src/common/debug" \
    -I "$ROOT/src/snes/ppu" \
    -I "$ROOT/src/snes/core" \
    -I "$ROOT/src/snes/cpu" \
    -I "$ROOT/src/snes" \
    -I "$ROOT/src" \
    oam_test.cpp "$ROOT/src/snes/ppu/snppu.cpp" \
    "$ROOT/src/snes/core/sndma.cpp" -o oam_test

echo "OK -> ./obj_test && ./oam_test"
