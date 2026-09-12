#!/bin/bash
# Exhaustive closure/upvalue tests
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
        printf "        expected: %s\n" "$(echo "$expected" | head -3)"
        printf "        got:      %s\n" "$(echo "$actual" | head -3)"
    fi
}

echo "=== Closure / Upvalue Tests ==="

# Basic closure capturing local
run_test "closure captures enclosing local" \
    'def make() { var x = 42; def get() { return x; } return get; } var g = make(); print(g());' \
    "42"

# Closure modifies captured variable
run_test "closure modifies upvalue" \
    'def make() { var x = 0; def inc() { x = x + 1; return x; } return inc; } var f = make(); print(f()); print(f()); print(f());' \
    "1
2
3"

# Two closures share same upvalue (using wrapper pattern since arrays not yet available)
run_test "two closures share upvalue via caller" \
    'def make() { var x = 0; def inc() { x = x + 1; } def get() { return x; } inc(); inc(); inc(); return get; } var g = make(); print(g());' \
    "3"

run_test "shared upvalue via wrapper" \
    'def make() { var x = 0; def inc() { x = x + 1; return x; } return inc; } var a = make(); var b = make(); print(a()); print(a()); print(b()); print(b());' \
    "1
2
1
2"

# Closure captures parameter
run_test "closure captures parameter" \
    'def make(val) { def get() { return val; } return get; } var g = make(99); print(g());' \
    "99"

# Nested closures (closure inside closure)
run_test "nested closure" \
    'def outer() { var x = 1; def middle() { var y = 2; def inner() { return x + y; } return inner; } return middle; } var m = outer(); var i = m(); print(i());' \
    "3"

# Closure in loop - each iteration captures different value
run_test "closure captures loop variable (shared)" \
    'def make() { var fns = nil; var last = nil; for (var i = 0; i < 3; i = i + 1) { def f() { return i; } last = f; } return last; } var fn = make(); print(fn());' \
    "3"

# Counter with start value
run_test "counter closure with start" \
    'def counter(start) { var n = start; def next() { n = n + 1; return n; } return next; } var c = counter(10); print(c()); print(c()); print(c());' \
    "11
12
13"

# Closure captures multiple locals
run_test "closure captures multiple locals" \
    'def make(a, b) { def sum() { return a + b; } return sum; } var s = make(3, 7); print(s());' \
    "10"

# Deeply nested upvalue chain
run_test "deeply nested upvalue (3 levels)" \
    'def a() { var x = 100; def b() { def c() { return x; } return c; } return b; } var b = a(); var c = b(); print(c());' \
    "100"

# Closure modifies deeply nested upvalue
run_test "modify deeply nested upvalue" \
    'def a() { var x = 0; def b() { def c() { x = x + 10; return x; } return c; } return b; } var b = a(); var c = b(); print(c()); print(c());' \
    "10
20"

# Closure survives after enclosing function returns
run_test "closure outlives enclosing function" \
    'def make() { var secret = 42; def reveal() { return secret; } return reveal; } var r = make(); print(r()); print(r());' \
    "42
42"

# Multiple independent closures from same factory
run_test "independent closures from factory" \
    'def make(n) { def get() { return n; } return get; } var a = make(1); var b = make(2); var c = make(3); print(a()); print(b()); print(c());' \
    "1
2
3"

# Closure with mutation and multiple instances
run_test "independent counters" \
    'def make() { var n = 0; def inc() { n = n + 1; return n; } return inc; } var a = make(); var b = make(); print(a()); print(a()); print(b()); print(a());' \
    "1
2
1
3"

# Anonymous function as closure
run_test "anonymous closure" \
    'def make(x) { return def() { return x * 2; }; } var f = make(5); print(f());' \
    "10"

# Closure in while loop
run_test "closure defined in while loop" \
    'var last = nil; var i = 0; while (i < 5) { def f() { return i; } last = f; i = i + 1; } print(last());' \
    "5"

# Recursive function (not really closure but related)  
run_test "recursive function" \
    'def fact(n) { if (n <= 1) { return 1; } return n * fact(n - 1); } print(fact(5)); print(fact(10));' \
    "120
3628800"

# Mutual recursion via globals
run_test "mutual recursion" \
    'def is_even(n) { if (n == 0) { return true; } return is_odd(n - 1); } def is_odd(n) { if (n == 0) { return false; } return is_even(n - 1); } print(is_even(10)); print(is_odd(7));' \
    "true
true"

# Closure as callback (higher-order)
run_test "higher-order function with closure" \
    'def apply(f, x) { return f(x); } def make_adder(n) { def add(x) { return x + n; } return add; } var add10 = make_adder(10); print(apply(add10, 5)); print(apply(add10, 20));' \
    "15
30"

# Fibonacci with closure (memoization-like)
run_test "fibonacci closure" \
    'def fib_gen() { var a = 0; var b = 1; def next() { var t = a; a = b; b = t + b; return t; } return next; } var f = fib_gen(); print(f()); print(f()); print(f()); print(f()); print(f()); print(f()); print(f());' \
    "0
1
1
2
3
5
8"

echo ""
echo "=== $PASS / $((PASS+FAIL)) PASSED ==="
if [ $FAIL -gt 0 ]; then echo "FAILURES: $FAIL"; exit 1; fi
echo "ALL CLOSURE TESTS OK!"
