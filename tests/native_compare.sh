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
    "$cxx" -std=c++11 -Wall -Wextra -Werror -I"$support_dir" "$generated" -o "$native"
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
