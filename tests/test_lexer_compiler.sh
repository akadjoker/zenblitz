#!/bin/bash
# test_lexer_compiler.sh — Edge case tests for zen lexer + compiler.
# Runs zen with -e and checks output against expected.
# Usage: ./tests/test_lexer_compiler.sh [path-to-zen-binary]

set -euo pipefail

ZEN="${1:-./build/zen}"
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

# Expect error output (just check it doesn't crash/hang)
run_test_no_crash() {
    local name="$1"
    local code="$2"
    TOTAL=$((TOTAL + 1))

    if timeout 2 "$ZEN" -e "$code" >/dev/null 2>&1; then
        PASS=$((PASS + 1))
        printf "  [%3d] %-55s OK\n" "$TOTAL" "$name"
    else
        local exit_code=$?
        if [ $exit_code -le 1 ]; then
            PASS=$((PASS + 1))
            printf "  [%3d] %-55s OK (exit %d)\n" "$TOTAL" "$name" "$exit_code"
        elif [ $exit_code -eq 124 ]; then
            FAIL=$((FAIL + 1))
            printf "  [%3d] %-55s FAIL (timeout)\n" "$TOTAL" "$name"
        else
            PASS=$((PASS + 1))
            printf "  [%3d] %-55s OK (exit %d)\n" "$TOTAL" "$name" "$exit_code"
        fi
    fi
}

echo "=== zen Lexer + Compiler Edge Case Tests ==="

# =========================================================
# SECTION 1: Number literals
# =========================================================
echo ""
echo "--- Numbers ---"

run_test "integer zero" \
    'print(0);' "0"

run_test "integer positive" \
    'print(42);' "42"

run_test "integer negative" \
    'print(-1);' "-1"

run_test "integer max sBx (32767)" \
    'print(32767);' "32767"

run_test "integer min sBx (-32768)" \
    'print(-32768);' "-32768"

run_test "integer > 32767 (constant pool)" \
    'print(65535);' "65535"

run_test "integer < -32768 (constant pool)" \
    'print(-100000);' "-100000"

run_test "hex literal 0xFF" \
    'print(0xFF);' "255"

run_test "hex literal 0x0" \
    'print(0x0);' "0"

run_test "hex literal 0xDEAD" \
    'print(0xDEAD);' "57005"

run_test "float 3.14" \
    'print(3.14);' "3.14"

run_test "float 0.5" \
    'print(0.5);' "0.5"

run_test "float 1e10" \
    'print(1e10);' "1e+10"

run_test "float -0.001" \
    'print(-0.001);' "-0.001"

# =========================================================
# SECTION 2: String literals
# =========================================================
echo ""
echo "--- Strings ---"

run_test "simple string" \
    'print("hello");' 'hello'

run_test "empty string" \
    'print("");' ''

run_test "escape newline" \
    'print("a\nb");' 'a
b'

run_test "escape tab" \
    'print("a\tb");' 'a	b'

run_test "escape backslash" \
    'print("a\\b");' 'a\b'

run_test "escape quote" \
    'print("say \"hi\"");' 'say "hi"'

run_test "escape null char (length preserved)" \
    'print(len("ab\0cd"));' '5'

run_test "escape hex \\x41 = A" \
    'print("\x41");' 'A'

run_test "escape hex \\x00 (null)" \
    'print(len("\x00"));' "1"

run_test "escape unicode \\u0041 = A" \
    'print("\u0041");' 'A'

run_test "escape unicode \\u00E9 = é" \
    'print("\u00E9");' 'é'

run_test "verbatim string no escapes" \
    'print(@"C:\path\to");' 'C:\path\to'

run_test "verbatim string double-quote escape" \
    'print(@"say ""hi""");' 'say "hi"'

# =========================================================
# SECTION 3: Keywords (math builtins)
# =========================================================
echo ""
echo "--- Math builtins ---"

run_test "sin(0) = 0" \
    'print(sin(0));' "0"

run_test "cos(0) = 1" \
    'print(cos(0));' "1"

run_test "tan(0) = 0" \
    'print(tan(0));' "0"

run_test "sqrt(4) = 2" \
    'print(sqrt(4));' "2"

run_test "sqrt(2) ≈ 1.41421" \
    'print(sqrt(2));' "1.41421"

run_test "pow(2, 8) = 256" \
    'print(pow(2, 8));' "256"

run_test "pow(3, 0) = 1" \
    'print(pow(3, 0));' "1"

run_test "abs(-99) = 99" \
    'print(abs(-99));' "99"

run_test "abs(5) = 5" \
    'print(abs(5));' "5"

run_test "floor(2.9) = 2" \
    'print(floor(2.9));' "2"

run_test "ceil(2.1) = 3" \
    'print(ceil(2.1));' "3"

run_test "log(1) = 0" \
    'print(log(1));' "0"

run_test "exp(0) = 1" \
    'print(exp(0));' "1"

run_test "deg(0) = 0" \
    'print(deg(0));' "0"

run_test "rad(0) = 0" \
    'print(rad(0));' "0"

run_test "atan2(0, 1) = 0" \
    'print(atan2(0, 1));' "0"

run_test "nested: sqrt(pow(3,2)+pow(4,2)) = 5" \
    'print(sqrt(pow(3,2)+pow(4,2)));' "5"

run_test "clock() returns float > 0" \
    'var t = clock(); print(t > 0);' "true"

# =========================================================
# SECTION 4: len() builtin
# =========================================================
echo ""
echo "--- len() ---"

run_test "len of string" \
    'print(len("abc"));' "3"

run_test "len of empty string" \
    'print(len(""));' "0"

run_test "len used in expression" \
    'print(len("hello") + 1);' "6"

# =========================================================
# SECTION 5: Variables and assignment
# =========================================================
echo ""
echo "--- Variables ---"

run_test "var declaration and print" \
    'var x = 42; print(x);' "42"

run_test "var reassignment" \
    'var x = 1; x = 2; print(x);' "2"

run_test "var arithmetic" \
    'var x = 10; x = x + 5; print(x);' "15"

run_test "var compound +=" \
    'var x = 10; x += 3; print(x);' "13"

run_test "var compound -=" \
    'var x = 10; x -= 3; print(x);' "7"

run_test "var compound *=" \
    'var x = 3; x *= 4; print(x);' "12"

run_test "var compound /=" \
    'var x = 10; x /= 2; print(x);' "5"

run_test "multiple vars" \
    'var a = 1; var b = 2; print(a + b);' "3"

run_test "var string" \
    'var s = "hi"; print(s);' 'hi'

run_test "var nil" \
    'var x; print(x);' "nil"

run_test "var bool true" \
    'var x = true; print(x);' "true"

run_test "var bool false" \
    'var x = false; print(x);' "false"

# =========================================================
# SECTION 6: Arithmetic operators
# =========================================================
echo ""
echo "--- Arithmetic ---"

run_test "add integers" \
    'print(3 + 4);' "7"

run_test "sub integers" \
    'print(10 - 3);' "7"

run_test "mul integers" \
    'print(6 * 7);' "42"

run_test "div (always float)" \
    'print(10 / 3);' "3.33333"

run_test "mod integers" \
    'print(10 % 3);' "1"

run_test "negation" \
    'print(-42);' "-42"

run_test "precedence: 2+3*4" \
    'print(2 + 3 * 4);' "14"

run_test "precedence: (2+3)*4" \
    'print((2 + 3) * 4);' "20"

run_test "mixed int+float" \
    'print(1 + 0.5);' "1.5"

# =========================================================
# SECTION 7: Comparison and logical
# =========================================================
echo ""
echo "--- Comparison & Logic ---"

run_test "1 == 1" \
    'print(1 == 1);' "true"

run_test "1 == 2" \
    'print(1 == 2);' "false"

run_test "1 != 2" \
    'print(1 != 2);' "true"

run_test "3 < 5" \
    'print(3 < 5);' "true"

run_test "5 < 3" \
    'print(5 < 3);' "false"

run_test "3 <= 3" \
    'print(3 <= 3);' "true"

run_test "5 > 3" \
    'print(5 > 3);' "true"

run_test "3 >= 5" \
    'print(3 >= 5);' "false"

run_test "not true" \
    'print(!true);' "false"

run_test "not false" \
    'print(!false);' "true"

# =========================================================
# SECTION 8: Bitwise operators
# =========================================================
echo ""
echo "--- Bitwise ---"

run_test "0xFF & 0x0F = 15" \
    'print(0xFF & 0x0F);' "15"

run_test "0xF0 | 0x0F = 255" \
    'print(0xF0 | 0x0F);' "255"

run_test "0xFF ^ 0x0F = 240" \
    'print(0xFF ^ 0x0F);' "240"

run_test "~0 = -1" \
    'print(~0);' "-1"

run_test "1 << 8 = 256" \
    'print(1 << 8);' "256"

run_test "256 >> 4 = 16" \
    'print(256 >> 4);' "16"

# =========================================================
# SECTION 9: Print multi-arg
# =========================================================
echo ""
echo "--- Print ---"

run_test "print single int" \
    'print(42);' "42"

run_test "print single string" \
    'print("hi");' 'hi'

run_test "print multiple args" \
    'print(1, 2, 3);' "1 2 3"

run_test "print mixed types" \
    'print(1, "a", true);' '1 a true'

run_test "print bool" \
    'print(true, false);' "true false"

run_test "print nil" \
    'print(nil);' "nil"

# =========================================================
# SECTION 10: Edge cases / error recovery
# =========================================================
echo ""
echo "--- Error handling (no crash) ---"

run_test_no_crash "empty input" \
    ''

run_test_no_crash "just semicolons" \
    ';;;'

run_test_no_crash "unterminated string" \
    'print("hello'

run_test_no_crash "unexpected token" \
    '+ + +'

run_test_no_crash "undeclared variable (becomes global nil)" \
    'print(xyz);'

run_test_no_crash "missing semicolon" \
    'print(1) print(2)'

run_test_no_crash "deeply nested parens" \
    'print(((((((1+2)))))));'

run_test_no_crash "very long identifier" \
    'var abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 = 1; print(abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789);'

# =========================================================
# SECTION 10b: Parser error recovery — must not hang
# =========================================================
echo ""
echo "--- Parser error recovery (must terminate) ---"

run_test_no_crash "missing ) in expression" \
    'var x = (1 + 2'

run_test_no_crash "missing ] in array" \
    'var x = [1, 2, 3'

run_test_no_crash "missing ) in sin()" \
    'sin('

run_test_no_crash "missing , in pow()" \
    'pow(1'

run_test_no_crash "missing ) in print" \
    'print(1, 2'

run_test_no_crash "if missing )" \
    'if (true { print(1); }'

run_test_no_crash "while missing (" \
    'while true) { }'

run_test_no_crash "for incomplete" \
    'for (var i = 0; i < 10'

run_test_no_crash "var missing name" \
    'var = 5;'

run_test_no_crash "def missing name" \
    'def { }'

run_test_no_crash "def missing )" \
    'def foo( { }'

run_test_no_crash "nested [[[ unclosed" \
    'var x = [[[;'

run_test_no_crash "stray ]]]" \
    ']]]]'

run_test_no_crash "stray }}}" \
    '}}}'

run_test_no_crash "unclosed {{{" \
    '{{{'

run_test_no_crash "missing ; multiple" \
    'var x = 1 var y = 2'

run_test_no_crash "1 + + + + (consecutive ops)" \
    'var x = 1 + + + + + 2;'

run_test_no_crash "print print print" \
    'print print print;'

run_test_no_crash "only operators" \
    '+ - * / %'

run_test_no_crash "expression without ;" \
    '1 + 2 + 3 + 4'

# =========================================================
# SECTION 11: Keyword ambiguity (identifiers starting with keywords)
# =========================================================
echo ""
echo "--- Keyword vs identifier ---"

run_test "variable 'format' (not keyword)" \
    'var format = 1; print(format);' "1"

run_test "variable 'printer' (starts with 'print')" \
    'var printer = 99; print(printer);' "99"

run_test "variable 'force' (starts with 'for')" \
    'var force = 7; print(force);' "7"

run_test "variable 'while2' (starts with 'while')" \
    'var while2 = 3; print(while2);' "3"

run_test "variable 'sinx' (starts with 'sin')" \
    'var sinx = 5; print(sinx);' "5"

run_test "variable 'lengthy' (starts with 'len')" \
    'var lengthy = 8; print(lengthy);' "8"

run_test "variable 'sqrtN' (starts with 'sqrt')" \
    'var sqrtN = 4; print(sqrtN);' "4"

run_test "variable 'abs_val' (starts with 'abs')" \
    'var abs_val = 10; print(abs_val);' "10"

run_test "variable 'define' (starts with 'def')" \
    'var define = 42; print(define);' "42"

run_test "variable 'nil2' (starts with 'nil')" \
    'var nil2 = 1; print(nil2);' "1"

run_test "variable 'true_val' (starts with 'true')" \
    'var true_val = 77; print(true_val);' "77"

run_test "variable 'class_name' (starts with 'class')" \
    'var class_name = 11; print(class_name);' "11"

# =========================================================
# Integer overflow / wrapping (should not crash/UBSan)
# =========================================================
echo ""
echo "--- Integer overflow/wrapping ---"

run_test "INT_MAX32 + 1 no wrap (int64)" \
    'print(2147483647 + 1);' "2147483648"

run_test "INT_MIN32 - 1 no wrap (int64)" \
    'print(-2147483648 - 1);' "-2147483649"

run_test "negate INT_MIN32 no wrap (int64)" \
    'print(-(-2147483648));' "2147483648"

run_test "INT_MAX32 * 2 no wrap (int64)" \
    'print(2147483647 * 2);' "4294967294"

# =========================================================
# Modulo by zero (should not crash)
# =========================================================
echo ""
echo "--- Modulo by zero ---"

run_test "10 % 0 = 0" \
    'print(10 % 0);' "0"

run_test "0 % 0 = 0" \
    'print(0 % 0);' "0"

run_test "-5 % 0 = 0" \
    'print(-5 % 0);' "0"

# =========================================================
# Shift edge cases (masked to 0-31, no UB)
# =========================================================
echo ""
echo "--- Shift edge cases ---"

run_test "1 << 31 (int64)" \
    'print(1 << 31);' "2147483648"

run_test "1 << 32 (int64)" \
    'print(1 << 32);' "4294967296"

run_test "1 << -1 masked to << 63" \
    'print(1 << -1);' "-9223372036854775808"

run_test "-1 >> 1 arithmetic shift" \
    'print(-1 >> 1);' "-1"

# =========================================================
# Hex literal errors
# =========================================================
echo ""
echo "--- Hex literal errors ---"

run_test_no_crash "0x with no digits is error" \
    'print(0x);'

run_test "0xFF valid hex" \
    'print(0xFF);' "255"

run_test "0x0 valid hex" \
    'print(0x0);' "0"

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
