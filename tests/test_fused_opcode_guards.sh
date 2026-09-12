#!/bin/bash
# test_fused_opcode_guards.sh — Regression: fused super-instructions used to
# call as_instance()/as_array()/as_set() on an unchecked receiver. A nil there
# was a segfault, not an error. Each case below was a SIGSEGV before the guards.
# Usage: ./tests/test_fused_opcode_guards.sh [path-to-zen-binary]

set -euo pipefail

ZEN="${1:-./build/cli/zen}"
PASS=0
FAIL=0
TOTAL=0

export ASAN_OPTIONS=detect_leaks=0

# Asserts the VM reports a runtime error containing $2 instead of crashing.
# A segfault (exit 139) fails here; the point is the diagnostic, not the exit code.
run_guard_test() {
    local name="$1"
    local code="$2"
    local expect_substr="$3"
    TOTAL=$((TOTAL + 1))

    local actual status
    set +e
    actual=$("$ZEN" -e "$code" 2>&1)
    status=$?
    set -e

    if [ "$status" -ge 128 ]; then
        FAIL=$((FAIL + 1))
        printf "  [%3d] %-55s FAIL (crashed, signal %d)\n" "$TOTAL" "$name" "$((status - 128))"
        return
    fi

    case "$actual" in
        *"$expect_substr"*)
            PASS=$((PASS + 1))
            printf "  [%3d] %-55s OK\n" "$TOTAL" "$name"
            ;;
        *)
            FAIL=$((FAIL + 1))
            printf "  [%3d] %-55s FAIL\n" "$TOTAL" "$name"
            printf "        expected substring: %s\n" "$expect_substr"
            printf "        actual:             %s\n" "$actual"
            ;;
    esac
}

echo "--- Fused opcode receiver guards ---"

# The peephole fuses GETFIELD_IDX + MUL only when the field load feeds the
# multiply, so the receiver has to go nil *inside* the loop: the compiler
# cannot see it, and the fused handler used to dereference it blindly.
run_guard_test "GETFIELD_MUL with nil receiver" \
    'struct Vec3 { x, y, z } var v1 = Vec3(1.0, 2.0, 3.0); var v2 = Vec3(4.0, 5.0, 6.0); var acc = 0.0; var i = 0; while (i < 3) { acc = acc + v1.x * v2.x; if (i == 1) { v2 = nil; } i = i + 1; } print(acc);' \
    "GETFIELD_MUL expected instance/struct"

run_guard_test "GETFIELD_SUB with nil receiver" \
    'struct V { x, y } var a = V(10.0, 2.0); var b = V(3.0, 1.0); var acc = 0.0; var i = 0; while (i < 3) { acc = acc + a.x - b.x; if (i == 1) { b = nil; } i = i + 1; } print(acc);' \
    "GETFIELD_SUB expected instance/struct"

# Sanity: the guarded fast paths still compute the right answer.
run_guard_test "GETFIELD_MUL still correct when receiver is valid" \
    'struct Vec3 { x, y, z } var v1 = Vec3(2.0, 0.0, 0.0); var v2 = Vec3(3.0, 0.0, 0.0); print(v1.x * v2.x);' \
    "6"

run_guard_test "GETFIELD_SUB still correct when receiver is valid" \
    'struct V { x, y } var a = V(10.0, 0.0); var b = V(4.0, 0.0); print(a.x - b.x);' \
    "6"

echo ""
echo "=== $PASS / $TOTAL PASSED ==="
if [ "$FAIL" -gt 0 ]; then
    echo "*** $FAIL TESTS FAILED ***"
    exit 1
else
    echo "ALL TESTS OK!"
    exit 0
fi
