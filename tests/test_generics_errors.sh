#!/bin/bash
# test_generics_errors.sh — negative tests for the reified-generics ABI.
# Each case must FAIL (compile or runtime error) with an expected message
# substring. These can't be snapshot .zen tests: they assert on error text,
# and tests/*.zen is a must-succeed suite.
# Usage: ./tests/test_generics_errors.sh [path-to-zen-binary]

set -uo pipefail

ZEN="${1:-./bin/zen}"
PASS=0
FAIL=0
TOTAL=0

export ASAN_OPTIONS=detect_leaks=0

if [ ! -x "$ZEN" ]; then
    echo "zen interpreter not found or not executable: $ZEN" >&2
    exit 2
fi

# run_fail <name> <code> <expected-substring>
run_fail() {
    local name="$1" code="$2" expected="$3"
    TOTAL=$((TOTAL + 1))
    local actual status
    actual=$("$ZEN" -e "$code" 2>&1)
    status=$?
    if [ "$status" -eq 0 ]; then
        FAIL=$((FAIL + 1))
        printf "  [%3d] %-56s FAIL\n" "$TOTAL" "$name"
        printf "        expected an error, but it exited 0\n"
        printf "        output:   %s\n" "$actual"
        return
    fi
    case "$actual" in
        *"$expected"*)
            PASS=$((PASS + 1))
            printf "  [%3d] %-56s OK\n" "$TOTAL" "$name"
            ;;
        *)
            FAIL=$((FAIL + 1))
            printf "  [%3d] %-56s FAIL\n" "$TOTAL" "$name"
            printf "        expected substring: %s\n" "$expected"
            printf "        actual:   %s\n" "$actual"
            ;;
    esac
}

CLASSES='class Transform {} class Sprite {}'

echo "--- Reified generics: error cases ---"

# A generic function called WITHOUT <...> must be rejected, not silently bind
# the first value argument into the type-parameter register.
run_fail "generic function called without <...> (no value params)" \
    "def create<T>() { return T(); } create();" \
    "is generic and must be called with"

# The case that used to slip through: a generic function WITH value params
# whose value arity happens to match the call's argument count.
run_fail "generic function called without <...> (with value params)" \
    "def make<T>(x) { return x; } make(5);" \
    "is generic and must be called with"

# Same gap on the method side, reached through plain OP_INVOKE.
run_fail "generic method called without <...>" \
    "class Box { def get<T>(x) { return x; } } var b = Box(); b.get(5);" \
    "is generic and must be called with"

# <...> on a function the compiler can see is not generic: unambiguous
# generic-call punctuation, so it must not quietly degrade to a comparison.
run_fail "<...> on a non-generic function" \
    "$CLASSES def plain(x) { return x; } plain<Transform>(5);" \
    "not generic"

# def f<T>(x) called as f<A, B>(...) — the old sugar only checked the TOTAL
# argument count, so this used to compile silently.
run_fail "wrong number of type arguments" \
    "$CLASSES def one<T>(x) { return x; } one<Transform, Sprite>(5);" \
    "expects 1 type argument but got 2"

# A type argument that resolves to a non-class value.
run_fail "type argument is not a type" \
    "def create<T>() { return T; } var v = 5; create<v>();" \
    "is not a type"

# Generic arity satisfied but value arity short — normal arity checking still
# applies to the value parameters.
run_fail "value arity still enforced" \
    "$CLASSES def cw<T>(a, b, c) { return a; } cw<Transform>(1, 2);" \
    "expects 3 args but got 2"

# Wrong type-argument count on a generic method.
run_fail "wrong type-argument count on a method" \
    "$CLASSES class Box { def get<T>() { return T; } } var b = Box(); b.get<Transform, Sprite>();" \
    "expects 1 type argument but got 2"

# Generic constructors are unsupported: a generic init<T> reached through plain
# ClassName(x) must be rejected rather than binding x into T.
run_fail "generic init via plain construction" \
    "class Foo { var t; def init<T>(x) { self.t = T; return self; } } Foo(42);" \
    "generic constructors are not supported"

# Foo<T>(x) construction syntax is not supported either. A class name is never
# a generic callee, so '<' stays a comparison and the expression fails to
# parse — rejected, never silently miscompiled into a construction.
run_fail "generic construction syntax Foo<T>(x)" \
    "$CLASSES class Foo { def init<T>() { return self; } } Foo<Transform>();" \
    "Expected expression"

# super().method(x) on a generic parent method: no super().method<T>(...)
# syntax exists, so this must reject rather than silently miscall.
run_fail "generic parent method via super()" \
    "class Base { def greet<T>(m) { return m; } } class Child : Base { def greet(m) { return super.greet(m); } } Child().greet(\"hi\");" \
    "generic"

# A generic dunder reached through plain infix syntax, where no <...> opt-in is
# possible at the call site at all.
run_fail "generic dunder via infix operator" \
    "class Vec { var v; def init(v) { self.v = v; return self; } def __add__<T>(o) { return o; } } var a = Vec(1); var b = Vec(2); a + b;" \
    "generic"

# A generic function cannot be a fiber body or a process — neither has a
# channel through which the type arguments could ever arrive.
run_fail "generic function as a fiber body" \
    "def g<T>() { return T; } var f = spawn g();" \
    "generic"

echo ""
echo "=== $PASS / $TOTAL PASSED ==="
if [ "$FAIL" -gt 0 ]; then
    echo "*** $FAIL TESTS FAILED ***"
    exit 1
fi
echo "ALL TESTS OK!"
