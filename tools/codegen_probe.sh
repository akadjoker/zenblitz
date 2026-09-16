#!/usr/bin/env bash
# Build and run a probe pair for the native codegen and compare it with the VM.
#
#   tools/codegen_probe.sh <name>
#
# expects /tmp/<name>_vm.bb and /tmp/<name>_native.bb (the same script, with the
# output file it saves - if any - pointing at /tmp/<name>_{vm,native}.bmp), emits
# C++ with zencc, builds it against the engine, runs both with ZENBLITZ_HIDDEN=1
# and reports stdout and bitmap differences.
set -u
ZEN=$(cd "$(dirname "$0")/.." && pwd)
cd "$ZEN" || exit 1
n=$1
cxx=${CXX:-c++}
FLAGS="-std=c++11 -Wall -Wextra -Werror -I extern/SDL/include -I codegen -I engine/include -I extern/GPU/gpu/include -I libzen/third_party"
LIBS="build-codegen/engine/libzenblitz_engine.a build-codegen/extern/GPU/gpu/libgpu_sdl.a build-codegen/extern/GPU/gpu/libgpu.a build-codegen/extern/GPU/gpu/libgpu_common.a build-codegen/extern/SDL/libSDL2-2.0.so.0 -Wl,-rpath,$ZEN/build-codegen/extern/SDL -lm -lpthread -ldl"

rm -f "/tmp/${n}_vm.bmp" "/tmp/${n}_native.bmp" "/tmp/${n}_vm.out" "/tmp/${n}_native.out"
ZENBLITZ_HIDDEN=1 timeout 30 bin/zenblitz3d "/tmp/${n}_vm.bb" > "/tmp/${n}_vm.out" 2>&1

bin/zencc --emit-c "/tmp/${n}_native.bb" "/tmp/${n}_native.cpp" || exit 1
# shellcheck disable=SC2086
$cxx $FLAGS "/tmp/${n}_native.cpp" -o "/tmp/${n}.native" $LIBS || exit 1
ZENBLITZ_HIDDEN=1 timeout 30 "/tmp/${n}.native" > "/tmp/${n}_native.out" 2>&1

if diff <(grep -v ERROR "/tmp/${n}_vm.out") <(grep -v ERROR "/tmp/${n}_native.out") >/dev/null; then
    echo "$n stdout IGUAL"
else
    echo "$n stdout DIFERE"
    diff <(grep -v ERROR "/tmp/${n}_vm.out") <(grep -v ERROR "/tmp/${n}_native.out") | head -4
fi

if [ ! -f "/tmp/${n}_vm.bmp" ]; then
    echo "$n (sem bitmap)"
elif cmp -s "/tmp/${n}_vm.bmp" "/tmp/${n}_native.bmp"; then
    echo "$n bmp IGUAL"
else
    echo "$n bmp DIFERE"
fi
