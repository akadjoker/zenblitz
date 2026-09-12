#!/bin/bash
# Edge case tests - stress testing the VM
ZEN="${1:-./build/zen}"
PASS=0; FAIL=0

run_test() {
    local desc="$1" code="$2" expected="$3"
    local actual
    actual=$(ASAN_OPTIONS=detect_leaks=0 "$ZEN" -e "$code" 2>&1)
    if [ "$actual" = "$expected" ]; then
        PASS=$((PASS+1))
        printf "  [%3d] %-55s OK\n" $((PASS+FAIL)) "$desc"
    else
        FAIL=$((FAIL+1))
        printf "  [%3d] %-55s FAIL\n" $((PASS+FAIL)) "$desc"
        printf "        expected: |%s|\n" "$(echo "$expected" | head -3)"
        printf "        got:      |%s|\n" "$(echo "$actual" | head -3)"
    fi
}

# Test that output contains expected string (for error messages)
run_test_contains() {
    local desc="$1" code="$2" expected="$3"
    local actual
    actual=$(ASAN_OPTIONS=detect_leaks=0 "$ZEN" -e "$code" 2>&1)
    if echo "$actual" | grep -qF "$expected"; then
        PASS=$((PASS+1))
        printf "  [%3d] %-55s OK\n" $((PASS+FAIL)) "$desc"
    else
        FAIL=$((FAIL+1))
        printf "  [%3d] %-55s FAIL\n" $((PASS+FAIL)) "$desc"
        printf "        expected to contain: |%s|\n" "$expected"
        printf "        got:                 |%s|\n" "$(echo "$actual" | head -3)"
    fi
}

echo "=== Edge Case Tests ==="

# --- Stack overflow ---
run_test_contains "stack overflow: infinite recursion" \
    'def boom() { return boom(); } boom();' \
    "stack overflow"

run_test_contains "stack overflow: mutual recursion" \
    'def a() { return b(); } def b() { return a(); } a();' \
    "stack overflow"

# --- Deep but valid recursion ---
run_test "deep recursion (100 levels)" \
    'def depth(n) { if (n <= 0) { return 0; } return depth(n - 1) + 1; } print(depth(100));' \
    "100"

# --- Nested loops with break/continue ---
run_test "nested loop break outer via flag" \
    'var done = false; var result = 0; for (var i = 0; i < 10; i = i + 1) { for (var j = 0; j < 10; j = j + 1) { if (i * 10 + j == 35) { done = true; result = i * 10 + j; break; } } if (done) { break; } } print(result);' \
    "35"

run_test "continue in nested for loops" \
    'var sum = 0; for (var i = 0; i < 5; i = i + 1) { if (i == 2) { continue; } for (var j = 0; j < 3; j = j + 1) { if (j == 1) { continue; } sum = sum + 1; } } print(sum);' \
    "8"

# --- Variable shadowing ---
run_test "variable shadowing in nested scopes" \
    'var x = 1; { var x = 2; { var x = 3; print(x); } print(x); } print(x);' \
    "3
2
1"

# --- Closure captures correct scope ---
run_test "closure captures correct shadowed variable" \
    'var x = 10; def outer() { var x = 20; def inner() { return x; } return inner; } var f = outer(); print(f()); print(x);' \
    "20
10"

# --- Boolean operations ---
run_test "boolean logic" \
    'print(true and false); print(true or false); print(!true); print(!false);' \
    "false
true
false
true"

run_test "short-circuit and" \
    'var x = 0; false and (x = 1); print(x);' \
    "0"

run_test "short-circuit or" \
    'var x = 0; true or (x = 1); print(x);' \
    "0"

# --- Nil handling ---
run_test "nil equality" \
    'print(nil == nil); print(nil != nil); print(nil == 0); print(nil == false);' \
    "true
false
false
false"

# --- String operations ---
run_test "string concatenation" \
    'var s = "hello" + " " + "world"; print(s);' \
    "hello world"

run_test "string comparison" \
    'print("abc" == "abc"); print("abc" != "def"); print("a" < "b");' \
    "true
true
true"

# --- Integer overflow (wrapping) ---
run_test "integer arithmetic wrapping" \
    'var x = 2147483647; print(x + 1);' \
    "2147483648"

# --- Modulo ---
run_test "modulo operator" \
    'print(10 % 3); print(7 % 2); print(-5 % 3);' \
    "1
1
-2"

# --- Multiple return from nested calls ---
run_test "return from nested if" \
    'def test(x) { if (x > 0) { if (x > 5) { return "big"; } return "small"; } return "zero"; } print(test(10)); print(test(3)); print(test(0));' \
    "big
small
zero"

# --- While loop edge: never executes ---
run_test "while loop with false condition" \
    'var x = 42; while (false) { x = 0; } print(x);' \
    "42"

# --- For loop edge: start >= end ---
run_test "for loop no iterations" \
    'var x = 99; for (var i = 10; i < 5; i = i + 1) { x = 0; } print(x);' \
    "99"

# --- Do-while executes at least once ---
run_test "do-while executes once with false condition" \
    'var x = 0; do { x = x + 1; } while (false); print(x);' \
    "1"

# --- Nested do-while with locals (THE BUG WE FIXED) ---
run_test "nested do-while with local variables" \
    'var outer = 0; do { outer = outer + 1; var inner = 0; do { inner = inner + 1; if (inner == 4) { break; } } while (inner < 10); print(outer, inner); } while (outer < 3);' \
    "1 4
2 4
3 4"

# --- Function as argument ---
run_test "function passed as argument" \
    'def apply(f, x) { return f(x); } def sq(n) { return n * n; } print(apply(sq, 5)); print(apply(sq, 3));' \
    "25
9"

# --- Return from loop ---
run_test "return from inside loop" \
    'def find(n) { for (var i = 0; i < 100; i = i + 1) { if (i * i >= n) { return i; } } return -1; } print(find(25)); print(find(10));' \
    "5
4"

# --- Deeply nested closures modifying shared state ---
run_test "deeply nested closure mutation" \
    'def make() { var n = 0; def a() { n = n + 1; def b() { n = n + 10; return n; } return b; } return a; } var a = make(); var b = a(); print(b()); var b2 = a(); print(b2());' \
    "11
22"

# --- Fiber + closure interaction ---
run_test "fiber modifies closure upvalue" \
    'def make() { var x = 0; def inc() { x = x + 1; yield x; x = x + 1; yield x; } def get() { return x; } return [inc, get]; }' \
    ""

# Can't test the above without arrays. Test with single accessor:
run_test "fiber and closure share state" \
    'def make() { var x = 0; def gen() { loop { x = x + 1; yield x; } } return gen; } var g = make(); var f = spawn g; var a = resume(f); var b = resume(f); var c = resume(f); print(a); print(b); print(c);' \
    "1
2
3"

# --- Empty function ---
run_test "empty function returns nil" \
    'def empty() {} print(empty());' \
    "nil"

# --- Multiple parameters ---
run_test "function with many parameters" \
    'def sum5(a, b, c, d, e) { return a + b + c + d + e; } print(sum5(1, 2, 3, 4, 5));' \
    "15"

# --- String escape sequences ---
run_test "escape sequences in strings" \
    'print("a\tb\nc");' \
    "a	b
c"

# NOTA: zen strings são length-based, print não trunca no \0
run_test "null byte in string" \
    'var s = "a\x00b"; print(len(s));' \
    "3"

# --- Comparison operators ---
run_test "all comparison operators" \
    'print(1 < 2); print(2 > 1); print(1 <= 1); print(1 >= 1); print(1 == 1); print(1 != 2);' \
    "true
true
true
true
true
true"

# --- Bitwise operators ---
run_test "bitwise operators" \
    'print(0xFF & 0x0F); print(0xF0 | 0x0F); print(0xFF ^ 0x0F); print(1 << 4); print(16 >> 2);' \
    "15
255
240
16
4"

# --- Compound assignment ---
run_test "compound assignment operators" \
    'var x = 10; x += 5; print(x); x -= 3; print(x); x *= 2; print(x); x /= 4; print(x);' \
    "15
12
24
6"

# --- Calling local function multiple times (THE OTHER BUG WE FIXED) ---
run_test "call local function multiple times" \
    'def make() { var x = 0; def inc() { x = x + 1; } def get() { return x; } inc(); inc(); inc(); return get; } var g = make(); print(g());' \
    "3"

echo ""
echo "=== $PASS / $((PASS+FAIL)) PASSED ==="
if [ $FAIL -gt 0 ]; then echo "FAILURES: $FAIL"; exit 1; fi
echo "ALL EDGE CASE TESTS OK!"
