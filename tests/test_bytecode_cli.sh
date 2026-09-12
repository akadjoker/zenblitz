#!/bin/bash
set -u

BIN="$1"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMPDIR="${TMPDIR:-/tmp}/zen_bytecode_tests.$$"
PASS=0
FAIL=0
ERRORS=""

mkdir -p "$TMPDIR"
trap 'rm -rf "$TMPDIR"' EXIT

run_roundtrip() {
    local name="$1"
    local script="$2"
    local expected="$3"
    local bc="$TMPDIR/$name.zbc"
    local source_out bytecode_out

    source_out=$("$BIN" "$ROOT/$script" 2>&1)
    if [[ "$source_out" != "$expected" ]]; then
        ((FAIL++))
        ERRORS+="FAIL source $name\nexpected: |$expected|\ngot:      |$source_out|\n\n"
        printf "  [---] %-36s source FAIL\n" "$name"
        return
    fi

    "$BIN" --dump "$bc" "$ROOT/$script" >/dev/null 2>&1
    if [[ $? -ne 0 ]]; then
        ((FAIL++))
        ERRORS+="FAIL dump $name\n\n"
        printf "  [---] %-36s dump FAIL\n" "$name"
        return
    fi

    bytecode_out=$("$BIN" "$bc" 2>&1)
    if [[ "$bytecode_out" != "$expected" ]]; then
        ((FAIL++))
        ERRORS+="FAIL bytecode $name\nexpected: |$expected|\ngot:      |$bytecode_out|\n\n"
        printf "  [---] %-36s bytecode FAIL\n" "$name"
        return
    fi

    ((PASS++))
    printf "  [%3d] %-36s OK\n" "$PASS" "$name"
}

run_contains() {
    local name="$1"
    local code="$2"
    local needle="$3"
    local out

    out=$("$BIN" -v --dump "$TMPDIR/$name.zbc" -e "$code" 2>&1)
    if [[ "$out" == *"$needle"* ]]; then
        ((PASS++))
        printf "  [%3d] %-36s OK\n" "$PASS" "$name"
    else
        ((FAIL++))
        ERRORS+="FAIL $name\nmissing: |$needle|\ngot:     |$out|\n\n"
        printf "  [---] %-36s FAIL\n" "$name"
    fi
}

run_err_contains() {
    local name="$1"
    local code="$2"
    local needle="$3"
    local out

    out=$("$BIN" -e "$code" 2>&1)
    if [[ $? -ne 0 && "$out" == *"$needle"* ]]; then
        ((PASS++))
        printf "  [%3d] %-36s OK\n" "$PASS" "$name"
    else
        ((FAIL++))
        ERRORS+="FAIL $name\nmissing error: |$needle|\ngot:          |$out|\n\n"
        printf "  [---] %-36s FAIL\n" "$name"
    fi
}

echo "=== Bytecode CLI Tests ==="

run_roundtrip "str_operator" "tests/test_str_operator.zen" "Vec2(1,2)
b=Vec2(3,4)
c=Vec2(4,6)
ok: __str__ operator"

run_roundtrip "bytecode_roundtrip" "tests/test_bytecode_roundtrip.zen" "Counter(hits=40)
41
after=Counter(hits=41)
15
1,2,3,4
3
579
ok: bytecode roundtrip"

run_roundtrip "bytecode_processes" "tests/test_bytecode_processes.zen" "alive0=2
total0=30
alive1=0
total1=33
alive2=0
total2=33
ok: bytecode processes"

run_roundtrip "class_operator_hints" "tests/test_class_operator_hints.zen" "global=Box(13)
assign=Box(12)
local=Box(12)
returned=Box(13)
eq=true
ok: class operator hints"

run_roundtrip "str_runtime_fallback" "tests/test_str_runtime_fallback.zen" "Label[yes]
x=Label[yes]
Label[no]
y=Label[no]
ok: __str__ runtime fallback"

run_roundtrip "bytecode_classes_nested" "tests/test_bytecode_classes_nested.zen" "Sprite(hero@Point(2,3))
Sprite(hero@Point(7,10))
again=Sprite(hero@Point(7,10))
ok: bytecode nested classes"

run_contains "verbose_process_count" \
    'process p(){ frame; } p();' \
    'processes:    1'

run_contains "dis_tostring_obj" \
    'class V { def __str__() { return "V"; } } def main(){ var v = V(); print("x={v}"); } main();' \
    'functions:'

dis_out=$("$BIN" --dis-only -e 'class V { def __str__() { return "V"; } } def main(){ var v = V(); print("x={v}"); } main();' 2>&1)
if [[ "$dis_out" == *"TOSTRING_OBJ"* ]]; then
    ((PASS++))
    printf "  [%3d] %-36s OK\n" "$PASS" "disassembly_tostring_obj"
else
    ((FAIL++))
    ERRORS+="FAIL disassembly_tostring_obj\nmissing TOSTRING_OBJ\ngot: |$dis_out|\n\n"
    printf "  [---] %-36s FAIL\n" "disassembly_tostring_obj"
fi

ops_dis=$("$BIN" --dis-only "$ROOT/tests/test_class_operator_hints.zen" 2>&1)
for op in ADD_OBJ SUB_OBJ EQ_OBJ; do
    if [[ "$ops_dis" == *"$op"* ]]; then
        ((PASS++))
        printf "  [%3d] %-36s OK\n" "$PASS" "disassembly_$op"
    else
        ((FAIL++))
        ERRORS+="FAIL disassembly_$op\nmissing $op\ngot: |$ops_dis|\n\n"
        printf "  [---] %-36s FAIL\n" "disassembly_$op"
    fi
done

run_err_contains "__str__ type check" \
    'class Bad { def __str__() { return 123; } } print(Bad());' \
    '__str__ must return a string'

echo ""
echo "PASS=$PASS FAIL=$FAIL"
if [[ $FAIL -ne 0 ]]; then
    printf "%b" "$ERRORS"
    exit 1
fi
