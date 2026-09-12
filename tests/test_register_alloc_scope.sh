#!/bin/bash
# test_register_alloc_scope.sh — Regression: register allocation vs. live locals.
#
# Three independent bugs in the same subsystem, all the shape "a construct
# reused a register it did not own":
#
#  1. and/or short-circuit (and_expr/or_expr) took `left` as the result
#     register whenever dest was -1. The right-hand side is compiled straight
#     INTO that register, so `print(a or b)` — where `a` is a local — wrote b
#     over local a and destroyed it. Fixed: reuse `left` only when it is a
#     temporary, otherwise allocate a fresh one (the rule binary() already
#     documents: "NEVER reuse left as dest").
#
#  2. Assignment-as-expression ignored the caller's dest register. `var z =
#     (y = 7)`, `var b = (a += 5)`, `var v = (m[k] = 9)` and `var w = (c.x =
#     4)` each returned some OTHER register (the local, a bare dest nothing
#     had written, the index key, the receiver), so the caller read whatever
#     stale value happened to be there. Fixed: every assignment path copies
#     the stored value into the result register.
#
#  3. try_numeric_for() reserves its counter/limit/step registers by bumping
#     next_reg directly instead of calling alloc_reg(), so max_reg was never
#     raised for them. A function whose highest registers were a for-loop's
#     declared num_regs = 0 while writing R[0..2] — the VM then sized the
#     frame (and its stack-overflow check) too small. Fixed: raise max_reg
#     (and clear the type fields alloc_reg()/add_local() would have cleared).
#
# Usage: ./tests/test_register_alloc_scope.sh [path-to-zen-binary]

set -euo pipefail

ZEN="${1:-./build/cli/zen}"
PASS=0
FAIL=0
TOTAL=0

export ASAN_OPTIONS=detect_leaks=0

# Asserts stdout matches exactly (trailing newline ignored).
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

echo "--- and/or: the result register must never be a live local ---"

# The reported shape: `a` is nil, so `a or b` short-circuits to b — and the
# MOVE that brings b in landed on local a's own register. Before the fix this
# printed "5" for a.
run_exact "'or' with dest=-1 does not clobber the left local" \
    'def f() { var a = nil; var b = 5; print(a or b); print(a, b); } f();' \
    $'5\nnil 5'

run_exact "'and' with dest=-1 does not clobber the left local" \
    'def f() { var a = 5; var b = nil; print(a and b); print(a, b); } f();' \
    $'nil\n5 nil'

run_exact "'or' chain leaves every operand local intact" \
    'def f() { var a = nil; var b = false; var c = 7; print(a or b or c); print(a, b, c); } f();' \
    $'7\nnil false 7'

run_exact "'and'/'or' mixed inside a larger expression keeps locals" \
    'def f() { var a = nil; var b = 2; var c = 3; print((a or b) + (b and c)); print(a, b, c); } f();' \
    $'5\nnil 2 3'

run_exact "'or' into an explicit dest still works" \
    'def f() { var a = nil; var b = 5; var r = a or b; print(r, a, b); } f();' \
    '5 nil 5'

echo ""
echo "--- assignment is an expression: its value must reach dest ---"

run_exact "local simple assignment yields the assigned value" \
    'def f() { var y = 0; var z = (y = 7); print(z, y); } f();' \
    '7 7'

run_exact "local compound assignment yields the new value" \
    'def f() { var a = 1; var b = (a += 5); print(b, a); } f();' \
    '6 6'

run_exact "index assignment yields the assigned value (not the key)" \
    'def f() { var m = {"k": 1}; var v = (m["k"] = 9); print(v, m["k"]); } f();' \
    '9 9'

run_exact "index compound assignment yields the new value" \
    'def f() { var m = {"k": 1}; var v = (m["k"] += 4); print(v, m["k"]); } f();' \
    '5 5'

run_exact "field assignment yields the assigned value (not the receiver)" \
    'class C { var x; def init() { self.x = 0; return self; } } def f() { var c = C(); var w = (c.x = 4); print(w, c.x); } f();' \
    '4 4'

run_exact "field compound assignment yields the new value" \
    'class C { var x; def init() { self.x = 2; return self; } } def f() { var c = C(); var w = (c.x *= 3); print(w, c.x); } f();' \
    '6 6'

run_exact "assignment as a call argument passes the assigned value" \
    'def id(v) { return v; } def f() { var a = 0; print(id(a = 3), a); } f();' \
    '3 3'

run_exact "global assignment as an expression still works" \
    'var y = 0; var z = (y = 7); print(z, y);' \
    '7 7'

echo ""
echo "--- numeric for: the declared register window must cover the loop ---"

# A function whose ONLY high registers are the for-loop's counter/limit/step
# used to report num_regs = 0 while writing R[0], R[1] and R[2].
run_exact "numeric for alone still produces correct values" \
    'def f() { for (var i = 0; i < 3; i = i + 1) { print(i); } } f();' \
    $'0\n1\n2'

run_exact "three nested numeric fors (9 registers) run correctly" \
    'def f() { for (var i = 0; i < 2; i = i + 1) { for (var j = 0; j < 2; j = j + 1) { for (var k = 0; k < 2; k = k + 1) { print(i, j, k); } } } } f();' \
    $'0 0 0\n0 0 1\n0 1 0\n0 1 1\n1 0 0\n1 0 1\n1 1 0\n1 1 1'

run_exact "numeric for inside a fiber (frame sized from num_regs)" \
    'def w() { for (var i = 0; i < 3; i = i + 1) { yield i; } return 9; } var fb = spawn w; var k = 0; while (k < 4) { print(resume(fb)); k = k + 1; }' \
    $'0\n1\n2\n9'

run_exact "numeric for does not disturb surrounding locals" \
    'def f() { var outer = 1; for (var i = 0; i < 2; i = i + 1) { var body = i * 10; print(body); } var after = 99; print(outer, after); } f();' \
    $'0\n10\n1 99'

echo ""
echo "--- free_reg must ask who owns a register, not count locals ---"

# `var (a, b, c) = f()` allocates the call's three results as temps and then
# declares the locals ABOVE them, so a local's register index exceeds
# local_count. free_reg()'s old `reg >= local_count` test then freed a live
# local, and the next `var` was allocated right on top of it.
run_exact "a later var does not land on a destructured local" \
    'def m() { return 1, 2, 3; } def f() { var (x, y, z) = m(); print(z); var n = 42; print(z, n); } f();' \
    $'3\n3 42'

run_exact "a second destructure reads the right result slots" \
    'def m() { return 1, 2, 3; } def f() { var (x, y, z) = m(); print(z); var (p, _, q) = m(); print(p, q); } f();' \
    $'3\n1 3'

run_exact "destructured locals survive an intervening print" \
    'def m() { return 1, 2, 3; } def f() { var a = 9; var (x, y, z) = m(); print(x, y, z, a); var (p, _, q) = m(); print(p, q, a, z); } f();' \
    $'1 2 3 9\n1 3 9 3'

run_exact "destructured local survives being used in an expression" \
    'def m() { return 1, 2, 3; } def f() { var (x, y, z) = m(); var s = z + 1; var t = z * 2; print(x, y, z, s, t); } f();' \
    '1 2 3 4 6'

echo ""
echo "--- sanity: neighbouring constructs still allocate correctly ---"

run_exact "ternary with dest=-1 keeps its operand locals" \
    'def f() { var c = true; var a = 1; var b = 2; print(c ? a : b); print(c, a, b); } f();' \
    $'1\ntrue 1 2'

run_exact "comprehension keeps surrounding locals and its result" \
    'def f() { var p = 1; var n = 10; var src = [1,2,3]; print([x*n for x in src][1]); print(p, n, src[0]); } f();' \
    $'20\n1 10 1'

run_exact "call arguments stay consecutive with nested calls" \
    'def add3(a,b,c) { return a*100+b*10+c; } def id(x) { return x; } def f() { var p=1; var q=2; var r=3; print(add3(id(p), id(q), id(r))); print(p,q,r); } f();' \
    $'123\n1 2 3'

echo ""
echo "=== $PASS / $TOTAL PASSED ==="
if [ "$FAIL" -gt 0 ]; then
    echo "*** $FAIL TESTS FAILED ***"
    exit 1
else
    echo "ALL TESTS OK!"
    exit 0
fi
