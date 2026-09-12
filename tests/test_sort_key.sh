#!/bin/bash
# test_sort_key.sh — arr.sort(f) orders by the key f returns for each element.
# Usage: ./tests/test_sort_key.sh [path-to-zen-binary]

set -euo pipefail

ZEN="${1:-./build/cli/zen}"
PASS=0
FAIL=0
TOTAL=0

export ASAN_OPTIONS=detect_leaks=0

run_test() {
    local name="$1" code="$2" expected="$3"
    TOTAL=$((TOTAL + 1))
    local actual
    actual=$("$ZEN" -e "$code" 2>&1) || true
    if [ "$actual" = "$expected" ]; then
        PASS=$((PASS + 1)); printf "  [%3d] %-48s OK\n" "$TOTAL" "$name"
    else
        FAIL=$((FAIL + 1)); printf "  [%3d] %-48s FAIL\n" "$TOTAL" "$name"
        printf "        expected: %s\n" "$expected"
        printf "        actual:   %s\n" "$actual"
    fi
}

echo "--- sort() ---"

run_test "no argument still sorts ascending" \
    'var a = [3,1,2]; a.sort(); print(a.join(","));' "1,2,3"

run_test "\"desc\" still reverses" \
    'var a = [3,1,2]; a.sort("desc"); print(a.join(","));' "3,2,1"

run_test "key function orders by its result" \
    'def k(x) { return -x; } var a = [3,1,2]; a.sort(k); print(a.join(","));' "3,2,1"

run_test "key function over instance fields" \
    'class P { var v; def init(v) { self.v = v; } }
     def byv(p) { return p.v; }
     var a = [P(3), P(1), P(2)]; a.sort(byv);
     print(str(a[0].v) + str(a[1].v) + str(a[2].v));' "123"

# Equal keys must not reorder — a game sorting by one axis every frame
# depends on this to stay coherent between frames.
run_test "equal keys keep original order (stable)" \
    'def k(x) { return 0; } var a = [3,1,2]; a.sort(k); print(a.join(","));' "3,1,2"

run_test "empty array" \
    'def k(x) { return x; } var a = []; a.sort(k); print(len(a));' "0"

run_test "single element" \
    'def k(x) { return x; } var a = [7]; a.sort(k); print(a.join(","));' "7"

run_test "float keys" \
    'def k(x) { return x * 0.5; } var a = [3,1,2]; a.sort(k); print(a.join(","));' "1,2,3"

# The key function runs inside the sort, so an error there must unwind
# cleanly rather than leave the array half-ordered or leak the key buffer.
run_test "error inside the key function is reported" \
    'def bad(x) { return x.nope; } var a = [1,2,3]; a.sort(bad);' \
    '[zen runtime error] sort() key function failed
  File "<cmdline>", line 1, in <cmdline>'

run_test "a non-string non-function argument is rejected" \
    'var a = [1,2]; a.sort(42);' \
    '[zen runtime error] sort() argument must be "asc"/"desc" or a key function
  File "<cmdline>", line 1, in <cmdline>'

echo ""
echo "=== $PASS / $TOTAL PASSED ==="
if [ "$FAIL" -gt 0 ]; then echo "*** $FAIL TESTS FAILED ***"; exit 1; fi
echo "ALL TESTS OK!"
