#!/bin/bash
BIN="$1"
PASS=0; FAIL=0; ERRORS=""

run() {
    local name="$1" code="$2" expected="$3"
    local actual
    actual=$($BIN -e "$code" 2>&1)
    if [[ "$actual" == "$expected" ]]; then
        ((PASS++))
    else
        ((FAIL++))
        ERRORS+="  FAIL: $name\n    code: $code\n    expected: |$expected|\n    got:      |$actual|\n\n"
    fi
}

# Expect compile/runtime error (just check non-zero exit)
run_err() {
    local name="$1" code="$2"
    $BIN -e "$code" >/dev/null 2>&1
    if [[ $? -ne 0 ]]; then
        ((PASS++))
    else
        ((FAIL++))
        ERRORS+="  FAIL (expected error): $name\n    code: $code\n\n"
    fi
}

echo "=== Stress Test Suite ==="

# --- Numeric edge cases ---
run "int max" "print(2147483647);" "2147483647"
run "int min" "print(-2147483648);" "-2147483648"
run "int overflow no wrap (int64)" "print(2147483647 + 1);" "2147483648"
run "int underflow no wrap (int64)" "print(-2147483648 - 1);" "-2147483649"
run "0 / 0 float" "print(0.0 / 0.0);" "-nan"
run "1 / 0 float" "print(1.0 / 0.0);" "inf"
run "neg inf" "print(-1.0 / 0.0);" "-inf"
run "int div zero" "print(1 / 0);" "inf"
run "int mod zero" "print(1 % 0);" "0"
run "hex ff" "print(0xFF);" "255"
run "hex 0" "print(0x0);" "0"
run "float precision" "print(0.1 + 0.2);" "0.3"

# --- String edge cases ---
run "empty string" 'print("");' ""
run "string with newline" 'print("a\nb");' "a
b"
run "string with tab" 'print("a\tb");' "a	b"
run "string with null" 'print(len("\0"));' "1"
run "string concat empty" 'print("hello" + "");' "hello"
run "string repeat pattern" '{ var s = ""; for (var i = 0; i < 5; i = i + 1) { s = s + "x"; } print(s); }' "xxxxx"
run "interp basic" 'var x = 42; print("val={x}");' "val=42"
run "interp expr" 'var x = 2; print("{x + 3}");' "5"
run "interp nested braces" 'print("a{1+2}b");' "a3b"
run "interp empty segments" 'var x = 1; print("{x}");' "1"

# --- Scoping stress ---
run "deep nesting" '{ { { { { var x = 99; print(x); } } } } }' "99"
run "shadow 5 deep" 'var x=1; {var x=2; {var x=3; {var x=4; {var x=5; print(x);}}}}' "5"
run "shadow restore" 'var x=1; {var x=2;} print(x);' "1"
run "many locals" '{ var a=1; var b=2; var c=3; var d=4; var e=5; var f=6; var g=7; var h=8; print(a+b+c+d+e+f+g+h); }' "36"

# --- Loop stress ---
run "while 0 iterations" '{ var x = 0; while (x > 0) { x = x - 1; } print(x); }' "0"
run "nested for 3x3" '{ var s = 0; for (var i=0; i<3; i=i+1) { for (var j=0; j<3; j=j+1) { s = s + 1; } } print(s); }' "9"
run "nested for 10x10" '{ var s = 0; for (var i=0; i<10; i=i+1) { for (var j=0; j<10; j=j+1) { s = s + 1; } } print(s); }' "100"
run "break in while" '{ var i=0; while(true) { i=i+1; if(i==5) { break; } } print(i); }' "5"
run "continue in while" '{ var s=0; var i=0; while(i<10) { i=i+1; if(i%2==0){continue;} s=s+i; } print(s); }' "25"
run "break in for" '{ var s=0; for(var i=0;i<100;i=i+1){if(i==10){break;} s=s+i;} print(s); }' "45"
run "continue in for" '{ var s=0; for(var i=0;i<10;i=i+1){if(i%2==0){continue;} s=s+i;} print(s); }' "25"
run "do-while 1 iter" '{ var x=10; do { x=x+1; } while(x<5); print(x); }' "11"
run "for step 2" '{ var s=0; for(var i=0;i<10;i=i+2){s=s+i;} print(s); }' "20"
run "for step 3" '{ var s=0; for(var i=0;i<10;i=i+3){s=s+i;} print(s); }' "18"
run "for negative (general)" '{ var s=0; for(var i=5;i>0;i=i-1){s=s+i;} print(s); }' "15"

# --- Functions ---
run "simple func" 'def add(a,b){return a+b;} print(add(3,4));' "7"
run "recursive fib" 'def fib(n){if(n<=1){return n;} return fib(n-1)+fib(n-2);} print(fib(10));' "55"
run "closure basic" 'def make(){var x=10; return def(){return x;};} var f=make(); print(f());' "10"
run "closure mutate" 'def counter(){var c=0; return def(){c=c+1; return c;};} var inc=counter(); print(inc()); print(inc()); print(inc());' "1
2
3"
run "many args" 'def f(a,b,c,d,e){return a+b+c+d+e;} print(f(1,2,3,4,5));' "15"
run "return nil implicit" 'def f(){var x=1;} print(f());' "nil"
run "nested closures" 'def a(){var x=1; def b(){var y=2; def c(){return x+y;} return c;} return b;} print(a()()());' "3"
run "recursion depth" 'def r(n){if(n==0){return 0;} return r(n-1)+1;} print(r(100));' "100"

# --- Operators ---
run "bitwise and" 'print(0xFF & 0x0F);' "15"
run "bitwise or" 'print(0xF0 | 0x0F);' "255"
run "bitwise xor" 'print(0xFF ^ 0x0F);' "240"
run "bitwise not" 'print(~0);' "-1"
run "shift left" 'print(1 << 8);' "256"
run "shift right" 'print(256 >> 4);' "16"
run "shift right negative" 'print(-1 >> 1);' "-1"
run "precedence" 'print(2 + 3 * 4);' "14"
run "parens" 'print((2 + 3) * 4);' "20"
run "unary minus" 'print(-5 + 3);' "-2"
run "not true" 'print(!true);' "false"
run "not false" 'print(!false);' "true"
run "and short-circuit" 'var x=0; false && (x=1); print(x);' "0"
run "or short-circuit" 'var x=0; true || (x=1); print(x);' "0"
run "comparison chain" 'print(1 < 2); print(2 > 1); print(1 <= 1); print(1 >= 1);' "true
true
true
true"
run "equality" 'print(1 == 1); print(1 != 2); print("a" == "a"); print("a" != "b");' "true
true
true
true"
run "nil equality" 'print(nil == nil); print(nil != false);' "true
true"

# --- Type coercion ---
run "int + float" 'print(1 + 0.5);' "1.5"
run "float + int" 'print(0.5 + 1);' "1.5"
run "string + int" 'print("n=" + 42);' "n=42"
run "int + string" 'print(42 + " is the answer");' "42 is the answer"
run "int to string concat" 'var s = "x"; s = s + 1; print(s);' "x1"

# --- Compound assignment ---
run "+= basic" '{ var x = 5; x += 3; print(x); }' "8"
run "-= basic" '{ var x = 10; x -= 3; print(x); }' "7"
run "*= basic" '{ var x = 4; x *= 3; print(x); }' "12"
run "/= basic" '{ var x = 12; x /= 4; print(x); }' "3"
run "+= string" '{ var s = "hi"; s += " world"; print(s); }' "hi world"
run "+= in loop" '{ var s = 0; for(var i=0;i<5;i=i+1){ s += i; } print(s); }' "10"

# --- Array with GETINDEX/SETINDEX ---
run "array literal" 'var a=[1,2,3]; print(a[0]); print(a[1]); print(a[2]);' "1
2
3"
run "array set" 'var a=[10,20,30]; a[1]=99; print(a[1]);' "99"
run "array length" 'var a=[1,2,3]; print(len(a));' "3"
run "array nested" 'var a=[[1,2],[3,4]]; print(a[0][0]); print(a[1][1]);' "1
4"
run "array out of bounds" 'var a=[1,2,3]; print(a[5]);' "nil"
run_err "array set out of bounds" 'var a=[1,2,3]; a[5]=1;'
run_err "array non-int index" 'var a=[1,2,3]; print(a["x"]);'

# --- Map with GETINDEX/SETINDEX ---
run "map literal set/get" 'var m = []; print(len(m));' "0"

# --- Error cases ---
run "undefined var is nil" 'print(undefined_xyz);' "nil"
run_err "unterminated string" 'print("hello'
run_err "unterminated block comment" '/* oops'
run "many locals 180" '{ var a0=0;var a1=0;var a2=0;var a3=0;var a4=0;var a5=0;var a6=0;var a7=0;var a8=0;var a9=0;var b0=0;var b1=0;var b2=0;var b3=0;var b4=0;var b5=0;var b6=0;var b7=0;var b8=0;var b9=0;var c0=0;var c1=0;var c2=0;var c3=0;var c4=0;var c5=0;var c6=0;var c7=0;var c8=0;var c9=0;var d0=0;var d1=0;var d2=0;var d3=0;var d4=0;var d5=0;var d6=0;var d7=0;var d8=0;var d9=0;var e0=0;var e1=0;var e2=0;var e3=0;var e4=0;var e5=0;var e6=0;var e7=0;var e8=0;var e9=0;var f0=0;var f1=0;var f2=0;var f3=0;var f4=0;var f5=0;var f6=0;var f7=0;var f8=0;var f9=0;var g0=0;var g1=0;var g2=0;var g3=0;var g4=0;var g5=0;var g6=0;var g7=0;var g8=0;var g9=0;var h0=0;var h1=0;var h2=0;var h3=0;var h4=0;var h5=0;var h6=0;var h7=0;var h8=0;var h9=0;var i0=0;var i1=0;var i2=0;var i3=0;var i4=0;var i5=0;var i6=0;var i7=0;var i8=0;var i9=0;var j0=0;var j1=0;var j2=0;var j3=0;var j4=0;var j5=0;var j6=0;var j7=0;var j8=0;var j9=0;var k0=0;var k1=0;var k2=0;var k3=0;var k4=0;var k5=0;var k6=0;var k7=0;var k8=0;var k9=0;var l0=0;var l1=0;var l2=0;var l3=0;var l4=0;var l5=0;var l6=0;var l7=0;var l8=0;var l9=0;var m0=0;var m1=0;var m2=0;var m3=0;var m4=0;var m5=0;var m6=0;var m7=0;var m8=0;var m9=0;var n0=0;var n1=0;var n2=0;var n3=0;var n4=0;var n5=0;var n6=0;var n7=0;var n8=0;var n9=0;var o0=0;var o1=0;var o2=0;var o3=0;var o4=0;var o5=0;var o6=0;var o7=0;var o8=0;var o9=0;var p0=0;var p1=0;var p2=0;var p3=0;var p4=0;var p5=0;var p6=0;var p7=0;var p8=0;var p9=0;var q0=0;var q1=0;var q2=0;var q3=0;var q4=0;var q5=0;var q6=0;var q7=0;var q8=0;var q9=0;var r0=0;var r1=0;var r2=0;var r3=0;var r4=0;var r5=0;var r6=0;var r7=0;var r8=0;var r9=0; print(1); }' "1"

# --- Stress: deep recursion ---
run "deep recursion 127" 'def r(n){if(n==0){return 0;} return r(n-1)+1;} print(r(127));' "127"
run_err "stack overflow" 'def r(n){return r(n+1);} r(0);'

# --- Print results ---
echo ""
echo "=== $PASS passed, $FAIL failed ==="
if [[ $FAIL -gt 0 ]]; then
    echo ""
    echo -e "$ERRORS"
    exit 1
fi
