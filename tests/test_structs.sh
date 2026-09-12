#!/bin/bash
# test_structs.sh — Tests for script-defined structs (zero args, partial args, full args, field access, mutation)
# Usage: ./tests/test_structs.sh [path-to-zen-binary]

set -euo pipefail

ZEN="${1:-./build/cli/zen}"
PASS=0
FAIL=0
TOTAL=0

export ASAN_OPTIONS=detect_leaks=0

run_test() {
    local name="$1"
    local code="$2"
    local expected="$3"
    TOTAL=$((TOTAL + 1))

    local actual
    actual=$("$ZEN" -e "$code" 2>&1) || true

    if [ "$actual" = "$expected" ]; then
        PASS=$((PASS + 1))
        printf "  [%3d] %-55s OK\n" "$TOTAL" "$name"
    else
        FAIL=$((FAIL + 1))
        printf "  [%3d] %-55s FAIL\n" "$TOTAL" "$name"
        printf "        code:     %s\n" "$code"
        printf "        expected: %s\n" "$expected"
        printf "        actual:   %s\n" "$actual"
    fi
}

echo "=== Struct Tests ==="
echo ""

# =========================================================
# Basic struct definition and construction
# =========================================================
echo "--- Basic construction ---"

run_test "struct zero args (all nil)" \
    'struct Vec3 { x, y, z } var v = Vec3(); print(v.x); print(v.y); print(v.z);' \
    "nil
nil
nil"

run_test "struct full args" \
    'struct Vec3 { x, y, z } var v = Vec3(1, 2, 3); print(v.x); print(v.y); print(v.z);' \
    "1
2
3"

run_test "struct partial args (rest nil)" \
    'struct Vec3 { x, y, z } var v = Vec3(10, 20); print(v.x); print(v.y); print(v.z);' \
    "10
20
nil"

run_test "struct single field" \
    'struct Wrapper { val } var w = Wrapper(42); print(w.val);' \
    "42"

run_test "struct single field zero args" \
    'struct Wrapper { val } var w = Wrapper(); print(w.val);' \
    "nil"

# =========================================================
# Field access and mutation
# =========================================================
echo ""
echo "--- Field access and mutation ---"

run_test "struct set field" \
    'struct Point { x, y } var p = Point(0, 0); p.x = 99; print(p.x);' \
    "99"

run_test "struct set nil field" \
    'struct Point { x, y } var p = Point(); p.x = 7; p.y = 8; print(p.x); print(p.y);' \
    "7
8"

run_test "struct compound assign +=" \
    'struct Counter { n } var c = Counter(10); c.n += 5; print(c.n);' \
    "15"

run_test "struct compound assign -=" \
    'struct Counter { n } var c = Counter(10); c.n -= 3; print(c.n);' \
    "7"

run_test "struct compound assign *=" \
    'struct Counter { n } var c = Counter(4); c.n *= 3; print(c.n);' \
    "12"

run_test "struct field with float" \
    'struct F { val } var f = F(3.14); print(f.val);' \
    "3.14"

run_test "struct field with string" \
    'struct S { name } var s = S("hello"); print(s.name);' \
    "hello"

run_test "struct field with bool" \
    'struct B { flag } var b = B(true); print(b.flag);' \
    "true"

# =========================================================
# Multiple instances
# =========================================================
echo ""
echo "--- Multiple instances ---"

run_test "two instances independent" \
    'struct Point { x, y } var a = Point(1, 2); var b = Point(3, 4); print(a.x); print(b.x);' \
    "1
3"

run_test "mutate one, other unchanged" \
    'struct Point { x, y } var a = Point(1, 2); var b = Point(1, 2); a.x = 99; print(a.x); print(b.x);' \
    "99
1"

# =========================================================
# Struct in expressions
# =========================================================
echo ""
echo "--- Struct in expressions ---"

run_test "struct field in arithmetic" \
    'struct V { x } var v = V(10); print(v.x + 5);' \
    "15"

run_test "struct field in comparison" \
    'struct V { x } var v = V(10); print(v.x > 5);' \
    "true"

run_test "struct field as function arg" \
    'struct V { x } def double(n) { return n * 2; } var v = V(7); print(double(v.x));' \
    "14"

# =========================================================
# Nested structs (struct as field value)
# =========================================================
echo ""
echo "--- Nested structs ---"

run_test "struct inside struct" \
    'struct Inner { val } struct Outer { inner } var i = Inner(42); var o = Outer(i); print(o.inner.val);' \
    "42"

run_test "nested struct mutation" \
    'struct Inner { val } struct Outer { inner } var i = Inner(1); var o = Outer(i); o.inner.val = 99; print(o.inner.val);' \
    "99"

# =========================================================
# Struct with many fields
# =========================================================
echo ""
echo "--- Many fields ---"

run_test "struct 5 fields all set" \
    'struct Big { a, b, c, d, e } var x = Big(1,2,3,4,5); print(x.a); print(x.c); print(x.e);' \
    "1
3
5"

run_test "struct 5 fields partial" \
    'struct Big { a, b, c, d, e } var x = Big(10, 20); print(x.b); print(x.c); print(x.e);' \
    "20
nil
nil"

# =========================================================
# Struct in loops
# =========================================================
echo ""
echo "--- Struct in loops ---"

run_test "struct created in loop" \
    'struct Point { x, y } var sum = 0; for (var i = 0; i < 3; i = i + 1) { var p = Point(i, i*2); sum = sum + p.y; } print(sum);' \
    "6"

# =========================================================
# Summary
# =========================================================
echo ""
echo "=== $PASS / $TOTAL PASSED ==="
if [ "$FAIL" -gt 0 ]; then
    echo "*** $FAIL TESTS FAILED ***"
    exit 1
else
    echo "ALL TESTS OK!"
    exit 0
fi
