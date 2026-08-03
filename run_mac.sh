#!/usr/bin/env bash
# Native macOS counterpart to run.sh (which runs the Linux tests in Docker).
#
# ./run_mac.sh            build + run both tests (Debug)
# ./run_mac.sh Release    same, optimized
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

BUILD_TYPE="${1:-Debug}"
B="$HERE/build/mac-$BUILD_TYPE"

cmake -S "$HERE" -B "$B" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" >/dev/null
cmake --build "$B" -j >/dev/null

cd "$B"

echo "### build type: $BUILD_TYPE"
sw_vers | tr '\n' ' '; echo; uname -m
echo ""

echo "### thread-local sections in libfoo.dylib"
otool -l libfoo.dylib | grep -A4 '__thread' | grep -E 'sectname|segname|size'
echo ""

echo "### tlv symbols referenced by libfoo.dylib"
nm -mu libfoo.dylib | grep -i tlv || true
echo ""

echo "### dynamic_app  (libfoo.dylib linked as a dependency)"
otool -L dynamic_app | grep libfoo || true
./dynamic_app

echo ""
echo "### dlopen_app  (libfoo.dylib loaded via dlopen)"
otool -L dlopen_app | grep libfoo || echo "(libfoo not a link-time dependency, as expected)"
./dlopen_app
