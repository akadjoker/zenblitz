#!/bin/sh
set -eu

if [ "$#" -lt 3 ]; then
    echo "usage: $0 <zenblitz> <zencc> <program.bb>..." >&2
    exit 2
fi

vm=$1
zencc=$2
shift 2

workdir=${TMPDIR:-/tmp}/zen-native-compare.$$
trap 'rm -rf "$workdir"' EXIT INT TERM
mkdir -p "$workdir"

for source in "$@"; do
    name=$(basename "$source" .bb)
    generated="$workdir/$name.cpp"
    native="$workdir/$name.native"
    vm_out="$workdir/$name.vm.out"
    native_out="$workdir/$name.native.out"

    set +e
    timeout 30 "$vm" "$source" >"$vm_out" 2>"$workdir/$name.vm.err"
    vm_status=$?

    if ! timeout 30 "$zencc" --emit-c "$source" "$generated" >"$workdir/$name.zencc.out" 2>"$workdir/$name.zencc.err"; then
        echo "native generation failed for $source:" >&2
        cat "$workdir/$name.zencc.err" >&2
        exit 1
    fi

    cxx=${CXX:-c++}
    support_dir=$(CDPATH= cd -- "$(dirname "$zencc")/../codegen" && pwd)
    engine_dir=$(CDPATH= cd -- "$(dirname "$zencc")/../engine/include" && pwd)
    gpu_dir=$(CDPATH= cd -- "$(dirname "$zencc")/../extern/GPU/gpu/include" && pwd)
    runtime_third_party_dir=$(CDPATH= cd -- "$(dirname "$zencc")/../libzen/third_party" && pwd)
    build_dir=$(CDPATH= cd -- "$(dirname "$zencc")/../build" && pwd)
    sdl2_dir=$(CDPATH= cd -- "$(dirname "$zencc")/../extern/SDL" && pwd)
    sdl2_build="$build_dir/extern/SDL"
    cxxflags="${NATIVE_CXXFLAGS:-}"
    ldflags="${NATIVE_LDFLAGS:-}"
    # If SDL2 is available via pkg-config, use it; otherwise fall back to the vendored build
    if pkg-config --exists sdl2 2>/dev/null; then
        cxxflags="$cxxflags $(pkg-config --cflags sdl2)"
        ldflags="$ldflags $(pkg-config --libs sdl2)"
    else
        cxxflags="$cxxflags -I$sdl2_dir/include -I$sdl2_build/include"
        if [ -f "$sdl2_build/libSDL2.a" ]; then
            ldflags="$ldflags $sdl2_build/libSDL2.a -lm -lpthread -ldl"
        elif ls "$sdl2_build"/libSDL2-*.so.* >/dev/null 2>&1; then
            sdl2_shared=$(ls "$sdl2_build"/libSDL2-*.so.* | head -1)
            ldflags="$ldflags $sdl2_shared -Wl,-rpath,$sdl2_build -lm -lpthread -ldl"
        else
            ldflags="$ldflags -lSDL2"
        fi
    fi
    engine_ldflags="$build_dir/engine/libzenblitz_engine.a $build_dir/extern/GPU/gpu/libgpu_sdl.a $build_dir/extern/GPU/gpu/libgpu.a $build_dir/extern/GPU/gpu/libgpu_common.a"
    "$cxx" -std=c++11 -Wall -Wextra -Werror $cxxflags -I"$support_dir" -I"$engine_dir" -I"$gpu_dir" -I"$runtime_third_party_dir" "$generated" -o "$native" $engine_ldflags $ldflags
    timeout 30 "$native" >"$native_out" 2>"$workdir/$name.native.err"
    native_status=$?
    set -e

    if [ "$vm_status" -ne "$native_status" ]; then
        echo "exit code mismatch for $source: VM=$vm_status native=$native_status" >&2
        exit 1
    fi
    if ! cmp -s "$vm_out" "$native_out"; then
        echo "stdout mismatch for $source" >&2
        diff -u "$vm_out" "$native_out" >&2 || true
        exit 1
    fi
    echo "ok: $source"
done
