#!/bin/bash
# Exhaustive fiber tests
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

echo "=== Fiber Tests ==="

# Basic spawn and resume
run_test "basic fiber spawn and resume" \
    'def gen() { yield 1; yield 2; yield 3; } var f = spawn gen; var a = resume(f); var b = resume(f); var c = resume(f); print(a); print(b); print(c);' \
    "1
2
3"

# Fiber yields nil after completion
run_test "fiber yields nil after done" \
    'def gen() { yield 42; } var f = spawn gen; var a = resume(f); var b = resume(f); print(a); print(b);' \
    "42
nil"

# Fiber counter using closure to pass args
run_test "fiber counter via closure" \
    'def make_counter(max) { def counter() { var i = 0; while (i < max) { yield i; i = i + 1; } } return counter; } var f = spawn make_counter(5); var v = resume(f); while (v != nil) { print(v); v = resume(f); }' \
    "0
1
2
3
4"

# Bidirectional communication (send value to fiber)
run_test "send value to fiber" \
    'def echo() { var x = yield 0; yield x * 2; } var f = spawn echo; var a = resume(f); var b = resume(f, 10); print(a); print(b);' \
    "0
20"

# Multiple sends
run_test "multiple sends to fiber" \
    'def adder() { var sum = 0; loop { var v = yield sum; sum = sum + v; } } var f = spawn adder; resume(f); var a = resume(f, 5); var b = resume(f, 3); var c = resume(f, 7); print(a); print(b); print(c);' \
    "5
8
15"

# Fibonacci generator
run_test "fibonacci fiber" \
    'def fib() { var a = 0; var b = 1; loop { yield a; var t = a; a = b; b = t + b; } } var f = spawn fib; var r0 = resume(f); var r1 = resume(f); var r2 = resume(f); var r3 = resume(f); var r4 = resume(f); var r5 = resume(f); var r6 = resume(f); print(r0); print(r1); print(r2); print(r3); print(r4); print(r5); print(r6);' \
    "0
1
1
2
3
5
8"

# Multiple fibers running concurrently
run_test "multiple concurrent fibers" \
    'def count(start, step) { def gen() { var n = start; loop { yield n; n = n + step; } } return gen; } var a = spawn count(0, 1); var b = spawn count(100, 10); var a0 = resume(a); var b0 = resume(b); var a1 = resume(a); var b1 = resume(b); var a2 = resume(a); var b2 = resume(b); print(a0); print(b0); print(a1); print(b1); print(a2); print(b2);' \
    "0
100
1
110
2
120"

# Fiber with closure capturing upvalue
run_test "fiber with closure capturing upvalue" \
    'def make() { var x = 0; def gen() { loop { x = x + 1; yield x; } } return gen; } var g = make(); var f = spawn g; var a = resume(f); var b = resume(f); var c = resume(f); print(a); print(b); print(c);' \
    "1
2
3"

# Fiber range generator
run_test "fiber range via closure" \
    'def make_range(start, stop, step) { def gen() { var i = start; while (i < stop) { yield i; i = i + step; } } return gen; } var f = spawn make_range(2, 10, 3); var a = resume(f); var b = resume(f); var c = resume(f); var d = resume(f); print(a); print(b); print(c); print(d);' \
    "2
5
8
nil"

# Fiber with conditional yield
run_test "fiber with conditional yield" \
    'def make_evens(max) { def gen() { var i = 0; while (i < max) { if (i % 2 == 0) { yield i; } i = i + 1; } } return gen; } var f = spawn make_evens(10); var a = resume(f); var b = resume(f); var c = resume(f); var d = resume(f); var e = resume(f); print(a); print(b); print(c); print(d); print(e);' \
    "0
2
4
6
8"

# Fiber that yields strings
run_test "fiber yields strings" \
    'def greet() { yield "hello"; yield "world"; } var f = spawn greet; var a = resume(f); var b = resume(f); print(a); print(b);' \
    "hello
world"

# Nested function calls inside fiber
run_test "fiber calls functions" \
    'def double(x) { return x * 2; } def gen() { yield double(3); yield double(7); } var f = spawn gen; var a = resume(f); var b = resume(f); print(a); print(b);' \
    "6
14"

# Fiber with loop and break
run_test "fiber with loop and break" \
    'def gen() { var i = 0; loop { if (i >= 3) { break; } yield i; i = i + 1; } yield 99; } var f = spawn gen; var a = resume(f); var b = resume(f); var c = resume(f); var d = resume(f); print(a); print(b); print(c); print(d);' \
    "0
1
2
99"

# Spawn with anonymous function
run_test "spawn anonymous function" \
    'var f = spawn def() { yield 10; yield 20; }; var a = resume(f); var b = resume(f); print(a); print(b);' \
    "10
20"

# Fiber state after completion
run_test "resume completed fiber multiple times" \
    'def gen() { yield 1; } var f = spawn gen; var a = resume(f); var b = resume(f); var c = resume(f); var d = resume(f); print(a); print(b); print(c); print(d);' \
    "1
nil
nil
nil"

echo ""
echo "=== $PASS / $((PASS+FAIL)) PASSED ==="
if [ $FAIL -gt 0 ]; then echo "FAILURES: $FAIL"; exit 1; fi
echo "ALL FIBER TESTS OK!"
