#!/bin/bash
# test_addi_subi_instance.sh — Regression: the ADDI/SUBI immediate-literal
# fold (`x + 1`/`x - 1` where the compiler can't statically prove `x` is
# numeric — e.g. an untyped parameter) never checked whether the left
# operand turned out to be a class instance at runtime. OP_ADDI/OP_SUBI's
# fallback assumed "not int, not string [ADDI only]" meant numeric and read
# the instance's object pointer through to_number(), which returns 0.0 for
# any object — so `x + 1` on an instance silently computed `0.0 + 1` instead
# of calling __add__/__radd__ (or falling back to __str__ concat like OP_ADD
# does), and `x - 1` likewise ignored __sub__/__rsub__.
# Usage: ./tests/test_addi_subi_instance.sh [path-to-zen-binary]

set -euo pipefail

ZEN="${1:-./build/cli/zen}"
PASS=0
FAIL=0
TOTAL=0

export ASAN_OPTIONS=detect_leaks=0

run_exact() {
    local name="$1"
    local code="$2"
    local expect="$3"
    TOTAL=$((TOTAL + 1))

    local actual status
    set +e
    actual=$("$ZEN" -e "$code" 2>&1)
    status=$?
    set -e

    if [ "$status" -ne 0 ]; then
        FAIL=$((FAIL + 1))
        printf "  [%3d] %-55s FAIL (exit %d)\n" "$TOTAL" "$name" "$status"
        printf "        output: %s\n" "$actual"
        return
    fi

    if [ "$actual" = "$expect" ]; then
        PASS=$((PASS + 1))
        printf "  [%3d] %-55s OK\n" "$TOTAL" "$name"
    else
        FAIL=$((FAIL + 1))
        printf "  [%3d] %-55s FAIL\n" "$TOTAL" "$name"
        printf "        expected: %s\n" "$expect"
        printf "        actual:   %s\n" "$actual"
    fi
}

echo "--- ADDI/SUBI fold on an instance operand (via an untyped parameter) ---"

run_exact "x + 1 on an instance calls __add__" \
    'class S { var v; def init(a){self.v=a;} def __add__(o){return S(self.v+o);} def __str__(){return "S"+str(self.v);} } def f(x){ return x + 1; } print(f(S(10)));' \
    "S11"

run_exact "x - 1 on an instance calls __sub__" \
    'class S { var v; def init(a){self.v=a;} def __sub__(o){return S(self.v-o);} def __str__(){return "S"+str(self.v);} } def f(x){ return x - 1; } print(f(S(10)));' \
    "S9"

run_exact "x + 1 on an instance with no __add__ falls back to __str__ concat" \
    'class Q { var v; def init(a){self.v=a;} def __str__(){return "Q"+str(self.v);} } def f(x){ return x + 1; } print(f(Q(10)));' \
    "Q101"

echo ""
echo "--- sanity: non-instance forms of the fold still work ---"

run_exact "int + 1 still uses the fast int path" \
    'def f(x){ return x + 1; } print(f(41));' \
    "42"

run_exact "string + 1 still concatenates (pre-existing fix, no regression)" \
    'def f(x){ return x + 1; } print(f("a"));' \
    "a1"

run_exact "float - 1 still computes numerically" \
    'def f(x){ return x - 1; } print(f(10.5));' \
    "9.5"

echo ""
echo "=== $PASS / $TOTAL PASSED ==="
if [ "$FAIL" -gt 0 ]; then
    echo "*** $FAIL TESTS FAILED ***"
    exit 1
else
    echo "ALL TESTS OK!"
    exit 0
fi
