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

echo "=== OP_INVOKE Method Tests ==="
echo ""
echo "--- Array methods ---"

run "array.push" \
    'var a=[1,2]; a.push(3); print(a[2]);' "3"

run "array.pop" \
    'var a=[1,2,3]; var x=a.pop(); print(x); print(a.len());' "3
2"

run "array.len" \
    'var a=[10,20,30]; print(a.len());' "3"

run "array.contains true" \
    'var a=[1,2,3]; print(a.contains(2));' "true"

run "array.contains false" \
    'var a=[1,2,3]; print(a.contains(9));' "false"

run "array.index_of found" \
    'var a=["a","b","c"]; print(a.index_of("b"));' "1"

run "array.index_of not found" \
    'var a=[1,2,3]; print(a.index_of(99));' "-1"

run "array.reverse" \
    'var a=[1,2,3]; a.reverse(); print(a.join(","));' "3,2,1"

run "array.slice" \
    'var a=[10,20,30,40,50]; var b=a.slice(1,3); print(b.join(","));' "20,30"

run "array.insert" \
    'var a=[1,3]; a.insert(1,2); print(a.join(","));' "1,2,3"

run "array.remove" \
    'var a=[1,2,3]; a.remove(1); print(a.join(","));' "1,3"

run "array.clear" \
    'var a=[1,2,3]; a.clear(); print(a.len());' "0"

run "array.join" \
    'var a=[1,2,3]; print(a.join("-"));' "1-2-3"

run "array.sort asc" \
    'var a=[3,1,2]; a.sort(); print(a.join(","));' "1,2,3"

run "array.sort desc" \
    'var a=[3,1,2]; a.sort("desc"); print(a.join(","));' "3,2,1"

run "array.sort strings" \
    'var a=["banana","apple","cherry"]; a.sort(); print(a.join(","));' "apple,banana,cherry"

echo ""
echo "--- String methods ---"

run "string.len" \
    'var s="hello"; print(s.len());' "5"

run "string.upper" \
    'print("hello".upper());' "HELLO"

run "string.lower" \
    'print("HELLO".lower());' "hello"

run "string.sub" \
    'print("hello world".sub(0,5));' "hello"

run "string.find found" \
    'print("hello world".find("world"));' "6"

run "string.find not found" \
    'print("hello".find("xyz"));' "-1"

run "string.replace" \
    'print("hello world".replace("world","zen"));' "hello zen"

run "string.starts_with true" \
    'print("hello".starts_with("hel"));' "true"

run "string.starts_with false" \
    'print("hello".starts_with("xyz"));' "false"

run "string.ends_with true" \
    'print("hello".ends_with("llo"));' "true"

run "string.ends_with false" \
    'print("hello".ends_with("xyz"));' "false"

run "string.trim" \
    'print("  hi  ".trim());' "hi"

run "string.char_at" \
    'print("hello".char_at(1));' "e"

run "string.split" \
    'var p="a,b,c".split(","); print(p.join("|"));' "a|b|c"

run "string.split no match" \
    'var p="hello".split(","); print(p.len()); print(p[0]);' "1
hello"

echo ""
echo "--- Map methods ---"

run "map.set/get" \
    'var m={}; m.set("x",42); print(m.get("x"));' "42"

run "map.has true" \
    'var m={}; m.set("a",1); print(m.has("a"));' "true"

run "map.has false" \
    'var m={}; print(m.has("z"));' "false"

run "map.size" \
    'var m={}; m.set("a",1); m.set("b",2); print(m.size());' "2"

run "map.delete" \
    'var m={}; m.set("a",1); m.set("b",2); m.delete("a"); print(m.size()); print(m.has("a"));' "1
false"

run "map.keys" \
    'var m={}; m.set("x",1); var k=m.keys(); print(k.len());' "1"

run "map.values" \
    'var m={}; m.set("x",99); var v=m.values(); print(v[0]);' "99"

run "map.clear" \
    'var m={}; m.set("a",1); m.clear(); print(m.size());' "0"

run "map.get with default" \
    'var m={}; print(m.get("missing", 42));' "42"

echo ""
echo "--- Set literal + methods ---"

run "set literal size" \
    'var s = #{1, 2, 3}; print(s.size());' "3"

run "set deduplicates" \
    'var s = #{1, 1, 2, 2, 3}; print(s.size());' "3"

run "set empty" \
    'var s = #{}; print(s.size());' "0"

run "set.has true" \
    'var s = #{10, 20, 30}; print(s.has(20));' "true"

run "set.has false" \
    'var s = #{10, 20, 30}; print(s.has(99));' "false"

run "set.add" \
    'var s = #{1, 2}; s.add(3); print(s.size());' "3"

run "set.add duplicate" \
    'var s = #{1, 2}; s.add(2); print(s.size());' "2"

run "set.delete" \
    'var s = #{1, 2, 3}; s.delete(2); print(s.size()); print(s.has(2));' "2
false"

run "set.clear" \
    'var s = #{1, 2, 3}; s.clear(); print(s.size());' "0"

run "set.values" \
    'var s = #{42}; var v = s.values(); print(v.len());' "1"

run "set with strings" \
    'var s = #{"a", "b", "c"}; print(s.has("b")); print(s.size());' "true
3"

echo ""
echo "--- Error cases ---"

run_err "array unknown method" \
    'var a=[1]; a.foo();'

run_err "string unknown method" \
    'var s="hi"; s.foo();'

run_err "map unknown method" \
    'var m={}; m.foo();'

run_err "set unknown method" \
    'var s=#{}; s.foo();'

run_err "array.push no args" \
    'var a=[1]; a.push();'

run_err "array.sort bad arg" \
    'var a=[1]; a.sort(123);'

echo ""
echo "--- Chaining ---"

run "method on result" \
    'var a=[3,1,2]; var s=a.slice(0,2); print(s.join(","));' "3,1"

run "string method chain" \
    'print("Hello World".lower().len());' "11"

echo ""
echo "=== $((PASS + FAIL)) tests: $PASS passed, $FAIL failed ==="
if [[ $FAIL -gt 0 ]]; then
    echo ""
    echo -e "$ERRORS"
    exit 1
fi
echo "ALL INVOKE TESTS OK!"
