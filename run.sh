#!/usr/bin/env bash
# ./run.sh            build + run both tests (Debug)
# ./run.sh Release    same, optimized
# ./run.sh shell      interactive container (pmap / gdb available)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

docker build -q -t tls-test "$HERE" >/dev/null

if [[ "${1:-}" == shell ]]; then
    exec docker run --rm -it -v "$HERE:/workspace" tls-test bash
fi

BUILD_TYPE="${1:-Debug}"

docker run --rm -v "$HERE:/workspace" -e BUILD_TYPE="$BUILD_TYPE" tls-test bash -c '
set -e
B=/workspace/build/$BUILD_TYPE
cmake -S /workspace -B $B -DCMAKE_BUILD_TYPE=$BUILD_TYPE >/dev/null
cmake --build $B -j >/dev/null
cd $B

U=$(echo $BUILD_TYPE | tr a-z A-Z)
echo "### build type: $BUILD_TYPE"
grep "^CMAKE_CXX_FLAGS_$U:" CMakeCache.txt
echo ""

echo "### readelf -d dynamic_app"
readelf -d dynamic_app
echo ""
echo "### readelf -d dlopen_app"
readelf -d dlopen_app
echo ""
echo "### readelf -lW libfoo.so"
readelf -lW libfoo.so
echo ""

echo; echo "### dynamic_app"; ./dynamic_app
echo; echo "### dlopen_app";  ./dlopen_app
'
