#!/bin/bash
BIN="$1"
PASS=0; FAIL=0; ERRORS=""

run() {
    local name="$1" code="$2" expected="$3"
    local actual
    actual=$($BIN -e "$code" 2>&1)
    if [[ "$actual" == "$expected" ]]; then
        ((PASS++))
        printf "  [%3d] %-50s OK\n" "$PASS" "$name"
    else
        ((FAIL++))
        ERRORS+="  FAIL: $name\n    code: $code\n    expected: |$expected|\n    got:      |$actual|\n\n"
        printf "  [---] %-50s FAIL\n" "$name"
    fi
}

run_err() {
    local name="$1" code="$2"
    $BIN -e "$code" >/dev/null 2>&1
    if [[ $? -ne 0 ]]; then
        ((PASS++))
        printf "  [%3d] %-50s OK\n" "$PASS" "$name"
    else
        ((FAIL++))
        ERRORS+="  FAIL (expected error): $name\n    code: $code\n\n"
        printf "  [---] %-50s FAIL\n" "$name"
    fi
}

echo "=== Typed Buffer Tests ==="
echo ""
echo "--- Int32Array ---"

run "Int32Array(n) zeros" \
    'var a = Int32Array(3); print(a[0]); print(a[1]); print(a[2]);' "0
0
0"

run "Int32Array set/get" \
    'var a = Int32Array(3); a[0]=10; a[1]=20; a[2]=30; print(a[0]); print(a[1]); print(a[2]);' "10
20
30"

run "Int32Array from array" \
    'var a = Int32Array([5,10,15]); print(a[0]); print(a[1]); print(a[2]);' "5
10
15"

run "Int32Array.len()" \
    'var a = Int32Array(7); print(a.len());' "7"

run "Int32Array.fill()" \
    'var a = Int32Array(3); a.fill(42); print(a[0]); print(a[1]); print(a[2]);' "42
42
42"

run "Int32Array.byte_len()" \
    'var a = Int32Array(4); print(a.byte_len());' "16"

run "Int32Array truncates float" \
    'var a = Int32Array(1); a[0] = 3.7; print(a[0]);' "3"

echo ""
echo "--- Uint8Array ---"

run "Uint8Array(n) zeros" \
    'var a = Uint8Array(2); print(a[0]); print(a[1]);' "0
0"

run "Uint8Array set/get" \
    'var a = Uint8Array(2); a[0]=255; a[1]=128; print(a[0]); print(a[1]);' "255
128"

run "Uint8Array overflow wraps" \
    'var a = Uint8Array(1); a[0]=256; print(a[0]);' "0"

run "Uint8Array.byte_len()" \
    'var a = Uint8Array(10); print(a.byte_len());' "10"

run "Uint8Array from array" \
    'var a = Uint8Array([65, 66, 67]); print(a[0]); print(a[1]); print(a[2]);' "65
66
67"

echo ""
echo "--- Float64Array ---"

run "Float64Array(n) zeros" \
    'var a = Float64Array(2); print(a[0]); print(a[1]);' "0
0"

run "Float64Array set/get" \
    'var a = Float64Array(2); a[0]=3.14; a[1]=2.718; print(a[0]); print(a[1]);' "3.14
2.718"

run "Float64Array from array" \
    'var a = Float64Array([1.5, 2.5]); print(a[0]); print(a[1]);' "1.5
2.5"

run "Float64Array.byte_len()" \
    'var a = Float64Array(4); print(a.byte_len());' "32"

echo ""
echo "--- Float32Array ---"

run "Float32Array set/get" \
    'var a = Float32Array(1); a[0]=1.5; print(a[0]);' "1.5"

run "Float32Array.byte_len()" \
    'var a = Float32Array(4); print(a.byte_len());' "16"

echo ""
echo "--- Int16Array / Uint16Array / Int8Array / Uint32Array ---"

run "Int16Array" \
    'var a = Int16Array(1); a[0]=-1000; print(a[0]);' "-1000"

run "Uint16Array" \
    'var a = Uint16Array(1); a[0]=65535; print(a[0]);' "65535"

run "Int8Array" \
    'var a = Int8Array(1); a[0]=-50; print(a[0]);' "-50"

run "Uint32Array" \
    'var a = Uint32Array(1); a[0]=4000000000; print(a[0]);' "4000000000"

echo ""
echo "--- len() builtin ---"

run "len(buffer)" \
    'var a = Int32Array(5); print(len(a));' "5"

echo ""
echo "--- Error cases ---"

run_err "buffer index out of bounds" \
    'var a = Int32Array(3); print(a[5]);'

run_err "buffer set out of bounds" \
    'var a = Int32Array(3); a[5] = 1;'

run_err "buffer negative size" \
    'var a = Int32Array(-1);'

run_err "buffer non-number assign" \
    'var a = Int32Array(1); a[0] = "hi";'

run_err "buffer unknown method" \
    'var a = Int32Array(1); a.foo();'

echo ""
echo "--- Performance loop ---"

run "1M buffer writes" \
    'var a = Int32Array(1000000); var i = 0; while (i < 1000000) { a[i] = i; i = i + 1; } print(a[999999]);' "999999"

echo ""
echo "=== $((PASS + FAIL)) tests: $PASS passed, $FAIL failed ==="
if [[ $FAIL -gt 0 ]]; then
    echo ""
    echo -e "$ERRORS"
    exit 1
fi
echo "ALL BUFFER TESTS OK!"
