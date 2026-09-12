#!/bin/bash
# test_stradd_instance.sh — Regression: `t += u` on a function-local always
# compiled to OP_STRADD (an in-place-append optimization aimed at strings),
# with no check for whether the operands were actually instances. OP_STRADD's
# fallback branch assumed "not a string means numeric" and read an instance's
# object pointer as a double — e.g. `t += u` on two class instances printed
# garbage like `9.63782e-310` instead of computing __add__/__str__. Fixed by
# giving OP_STRADD the same is_instance branch (operator-overload +
# __str__-coercion fallback) that OP_ADD already had.
# Usage: ./tests/test_stradd_instance.sh [path-to-zen-binary]

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

echo "--- OP_STRADD (t += u) on instances ---"

run_exact "instance += instance uses __add__" \
    'class A { var v; def init(a){self.v=a;} def __add__(o){return A(self.v+o.v);} def __str__(){return "A"+str(self.v);} } def g(){ var t=A(10); var u=A(5); t+=u; print(t); } g();' \
    "A15"

run_exact "instance += instance, no __add__, falls back to __str__ concat" \
    'class B { var v; def init(a){self.v=a;} def __str__(){return "B"+str(self.v);} } def g(){ var t=B(1); t += " ok"; print(t); } g();' \
    "B1 ok"

run_exact "instance += string, no __add__ or __str__, default_to_string fallback" \
    'class C { var v; def init(a){self.v=a;} } def g(){ var t=C(1); t += "x"; print(t); } g();' \
    "<object>x"

echo ""
echo "--- __str__ fallback must not clobber the RHS operand's register ---"

# A first attempt at GC-rooting the two __str__ coercion results parked them
# into the operand registers (R[C] for OP_ADD/OP_ADD_OBJ, R[B] for OP_STRADD),
# on the assumption those are always compiler temps. They aren't: binary()'s
# dest<0 path returns a bare local/parameter's OWN register for `a + b`, so
# writing there destroyed the user's `b`. The fix now pauses the GC across
# the coercion pair instead of touching any register the compiler didn't
# already intend for the instruction.
run_exact "OP_ADD: b survives after a + b with __str__ fallback" \
    'class P { var n; def init(a){self.n=a;} def __str__(){return "P"+self.n;} } def go(a,b){ var s = a + b; return b.n; } print(go(P(1),P(2)));' \
    "2"

run_exact "OP_ADD: parameter b survives after string + b" \
    'class P { var n; def init(a){self.n=a;} def __str__(){return "P"+self.n;} } def go(a,b){ "x" + b; return b.n; } print(go(P(1),P(2)));' \
    "2"

run_exact "OP_STRADD: b survives after a += b with __add__" \
    'class P { var n; def init(a){self.n=a;} def __add__(o){return P(self.n+o.n);} def __str__(){return "P"+self.n;} } def go(a,b){ a += b; return b.n; } print(go(P(1),P(2)));' \
    "2"

echo ""
echo "--- __str__ fallback must survive a GC triggered mid-coercion ---"

# The second __str__ call allocates heavily; before the fix the first
# coercion's result (an unrooted C++ local) got swept and reused, printing
# garbage like `131025MARK_2` instead of `MARK_1MARK_2`.
run_exact "OP_ADD: first __str__ result survives a GC in the second" \
    'class C { var v; def init(a){self.v=a;} def __str__(){ if (self.v==2) { var j=[]; var i=0; while(i<200000){ j.push("pad_"+str(i)); i+=1; } } return "MARK_"+str(self.v); } } def g(){ var t=C(1); var u=C(2); var r = t + u; print(r); } g();' \
    "MARK_1MARK_2"

run_exact "OP_STRADD: first __str__ result survives a GC in the second" \
    'class C { var v; def init(a){self.v=a;} def __str__(){ if (self.v==2) { var j=[]; var i=0; while(i<200000){ j.push("pad_"+str(i)); i+=1; } } return "MARK_"+str(self.v); } } def g(){ var t=C(1); var u=C(2); t += u; print(t); } g();' \
    "MARK_1MARK_2"

echo ""
echo "--- sanity: non-instance forms of += still work ---"

run_exact "string += string still works" \
    'def g(){ var s="hi"; s += " there"; print(s); } g();' \
    "hi there"

run_exact "number += number still works" \
    'def g(){ var n=3; n += 4; print(n); } g();' \
    "7"

run_exact "instance -= instance already worked (no regression)" \
    'class A { var v; def init(a){self.v=a;} def __sub__(o){return A(self.v-o.v);} def __str__(){return "A"+str(self.v);} } def g(){ var t=A(10); var u=A(3); t-=u; print(t); } g();' \
    "A7"

echo ""
echo "=== $PASS / $TOTAL PASSED ==="
if [ "$FAIL" -gt 0 ]; then
    echo "*** $FAIL TESTS FAILED ***"
    exit 1
else
    echo "ALL TESTS OK!"
    exit 0
fi
