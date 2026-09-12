#!/bin/bash
# test_branch_fusion.sh — Regression: fusing a comparison into the branch is
# only valid when nothing already jumps to that branch. `a() || b <= c`
# patches the short-circuit JMPIF to the if's JMPIFNOT; fusing moves that
# branch into a 2-word instruction, so the jump would land on the sBx word.
# Usage: ./tests/test_branch_fusion.sh [path-to-zen-binary]

set -euo pipefail

ZEN="${1:-./build/cli/zen}"
PASS=0
FAIL=0
TOTAL=0

export ASAN_OPTIONS=detect_leaks=0

run_test() {
    local name="$1" code="$2" expected="$3"
    TOTAL=$((TOTAL + 1))
    local actual
    actual=$("$ZEN" -e "$code" 2>&1) || true
    if [ "$actual" = "$expected" ]; then
        PASS=$((PASS + 1))
        printf "  [%3d] %-52s OK\n" "$TOTAL" "$name"
    else
        FAIL=$((FAIL + 1))
        printf "  [%3d] %-52s FAIL\n" "$TOTAL" "$name"
        printf "        expected: %s\n" "$expected"
        printf "        actual:   %s\n" "$actual"
    fi
}

CLS='class C { var v; def init(x) { self.v = x; } def f() { return false; } def t() { return true; } } var c = C(5);'

echo "--- Branch fusion with short-circuit operators ---"

run_test "|| then <= : right side must still be reached" \
    "$CLS if (c.f() || c.v <= 5) { print(\"ok\"); }" "ok"

run_test "&& then == " \
    "$CLS if (c.t() && c.v == 5) { print(\"ok\"); }" "ok"

run_test "|| then != " \
    "$CLS if (c.f() || c.v != 9) { print(\"ok\"); }" "ok"

run_test "comparison first, call second" \
    "$CLS if (c.v < 10 || c.f()) { print(\"ok\"); }" "ok"

run_test "|| in a while condition" \
    "$CLS var i = 0; while (c.f() || i < 3) { i = i + 1; } print(i);" "3"

run_test "elif after a short-circuit if" \
    "$CLS if (c.f()) { print(\"no\"); } elif (c.v == 5) { print(\"ok\"); } else { print(\"no\"); }" "ok"

# The shape that actually caught this: a method whose body ends in a
# comparison, called on the left of ||, with a field compare on the right.
# The simpler cases above all happened to still work — this one did not.
TIMER='class Timer { var restante; def init(d) { self.restante = d; } def update(dt) { self.restante = self.restante - dt; if (self.restante < 0) { self.restante = 0; } return self.restante <= 0; } }'
ARMA='class Arma { var cooldown; def init(t) { self.cooldown = Timer(t); } def disparar() { if (self.cooldown.update(0) || self.cooldown.restante <= 0) { print("BANG"); return true; } return false; } }'

run_test "method call returning a comparison, on the left of ||" \
    "$TIMER $ARMA var a = Arma(0.5); a.disparar(); a.cooldown.update(0.6); a.disparar();" "BANG"

# Plain comparisons must still fuse and still be correct.
run_test "plain if fuses and is correct" \
    'var i = 7; if (i < 10) { print("ok"); }' "ok"

run_test "plain elif chain" \
    'var i = 7; if (i < 5) { print("a"); } elif (i < 10) { print("ok"); } else { print("b"); }' "ok"

echo ""
echo "=== $PASS / $TOTAL PASSED ==="
if [ "$FAIL" -gt 0 ]; then
    echo "*** $FAIL TESTS FAILED ***"
    exit 1
fi
echo "ALL TESTS OK!"
