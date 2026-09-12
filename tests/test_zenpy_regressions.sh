#!/bin/bash
# Regression tests baseados nos bugs encontrados no projecto zenpy.
# Cada test documenta qual bug está a verificar.
# Uso: ./tests/test_zenpy_regressions.sh [path/to/zen]

ZEN="${1:-./bin/zen}"
PASS=0; FAIL=0; CRASH=0

run_test() {
    local desc="$1" code="$2" expected="$3"
    local actual exit_code
    actual=$(ASAN_OPTIONS=detect_leaks=0 timeout 5 "$ZEN" -e "$code" 2>&1)
    exit_code=$?
    if [ $exit_code -eq 124 ]; then
        CRASH=$((CRASH+1))
        printf "  [%3d] %-60s TIMEOUT\n" $((PASS+FAIL+CRASH)) "$desc"
        return
    fi
    if [ "$actual" = "$expected" ]; then
        PASS=$((PASS+1))
        printf "  [%3d] %-60s OK\n" $((PASS+FAIL+CRASH)) "$desc"
    else
        FAIL=$((FAIL+1))
        printf "  [%3d] %-60s FAIL\n" $((PASS+FAIL+CRASH)) "$desc"
        printf "        expected: |%s|\n" "$(echo "$expected" | head -3)"
        printf "        got:      |%s|\n" "$(echo "$actual" | head -3)"
    fi
}

run_test_contains() {
    local desc="$1" code="$2" expected="$3"
    local actual exit_code
    actual=$(ASAN_OPTIONS=detect_leaks=0 timeout 5 "$ZEN" -e "$code" 2>&1)
    exit_code=$?
    if [ $exit_code -eq 124 ]; then
        CRASH=$((CRASH+1))
        printf "  [%3d] %-60s TIMEOUT\n" $((PASS+FAIL+CRASH)) "$desc"
        return
    fi
    if echo "$actual" | grep -qF "$expected"; then
        PASS=$((PASS+1))
        printf "  [%3d] %-60s OK\n" $((PASS+FAIL+CRASH)) "$desc"
    else
        FAIL=$((FAIL+1))
        printf "  [%3d] %-60s FAIL\n" $((PASS+FAIL+CRASH)) "$desc"
        printf "        expected to contain: |%s|\n" "$expected"
        printf "        got:                 |%s|\n" "$(echo "$actual" | head -3)"
    fi
}

echo "========================================================"
echo " Zen/BuLang — Regression Tests from zenpy Bug Audit"
echo "========================================================"
echo ""

# --------------------------------------------------------
# BUG-Z1 / zenpy findbugs BUG-001
# Closure captura parâmetro — R[0] destruído por MOVE antes de close_upvalues
#
# CAUSE: return_statement emite CLOSURE → MOVE R[0]←R[1] → RETURN.
# O MOVE sobrescreve o parâmetro (R[0] = n) com a closure ANTES de
# close_upvalues ser chamado. O upvalue fecha com o valor errado.
# --------------------------------------------------------
echo "--- BUG-Z1: closure captura parâmetro (R[0] destroyed before close) ---"

run_test "make_adder: param capturado por closure (básico)" \
    'def make_adder(n) { def add(x) { return x + n; } return add; } var f = make_adder(10); print(f(5));' \
    "15"

run_test "make_adder: duas instâncias independentes" \
    'def make_adder(n) { def add(x) { return x + n; } return add; } var a = make_adder(10); var b = make_adder(100); print(a(5)); print(b(5));' \
    "15
105"

run_test "chained call make_adder(10)(5)" \
    'def make_adder(n) { def add(x) { return x + n; } return add; } print(make_adder(10)(5));' \
    "15"

run_test "closure captura primeiro param, retorna closure directamente" \
    'def wrap(val) { def get() { return val; } return get; } print(wrap(42)()); print(wrap(99)());' \
    "42
99"

run_test "closure captura param + modifica outro local" \
    'def make(base) { var offset = 0; def step() { offset = offset + 1; return base + offset; } return step; } var f = make(100); print(f()); print(f()); print(f());' \
    "101
102
103"

run_test "param capturado em closure aninhada (2 níveis)" \
    'def outer(x) { def mid() { def inner() { return x; } return inner; } return mid; } print(outer(7)()());' \
    "7"

echo ""

# --------------------------------------------------------
# BUG #5b (audit original) / zenpy confirmado
# OP_CALL destrói variável local usada como callee.
# OP_CALL escreve resultado em R[base]. Se a função é uma local
# no reg base, a segunda chamada encontra nil em vez da função.
# --------------------------------------------------------
echo "--- BUG-5b: nested function chamada duas vezes (callee overwritten) ---"

run_test "nested def chamado duas vezes" \
    'def outer() { def inc() { return 1; } var a = inc(); var b = inc(); return a + b; } print(outer());' \
    "2"

run_test "nested def chamado em loop" \
    'def outer() { def greet() { return "hi"; } var s = ""; var i = 0; while (i < 3) { s = s + greet(); i = i + 1; } return s; } print(outer());' \
    "hihihi"

run_test "nested def com argumento, chamado duas vezes" \
    'def outer() { def double(n) { return n * 2; } return double(3) + double(5); } print(outer());' \
    "16"

run_test "nested def retorna valor, chamado 3x" \
    'def outer() { def f() { return 42; } print(f()); print(f()); print(f()); } outer();' \
    "42
42
42"

echo ""

# --------------------------------------------------------
# BUG #6 — OP_FORLOOP trunca int64 para int32
# int32_t counter = R[a].as.integer + R[a+2].as.integer;
# Valores > 2^31 causam overflow silencioso.
# --------------------------------------------------------
echo "--- BUG-6: int64 truncation em operações numéricas ---"

run_test "int além de 2^31 preservado em var" \
    'var big = 2147483648; print(big);' \
    "2147483648"

run_test "2^31 + 1 = 2147483649" \
    'var big = 2147483647; big = big + 2; print(big);' \
    "2147483649"

run_test "int negativo além de -2^31" \
    'var big = -2147483649; print(big);' \
    "-2147483649"

run_test "aritmética com valor > 2^32" \
    'var a = 4294967296; var b = a + 1; print(b);' \
    "4294967297"

# --------------------------------------------------------
# BUG #7 — OP_MOD trunca int64 para int32 antes do módulo
# --------------------------------------------------------
echo ""
echo "--- BUG-7: OP_MOD com valores > 2^31 ---"

run_test "2^31 mod 10 = 8" \
    'var big = 2147483648; print(big % 10);' \
    "8"

run_test "2^32 mod 7 = 4" \
    'var big = 4294967296; print(big % 7);' \
    "4"

run_test "2^33 mod 3 = 2" \
    'var big = 8589934592; print(big % 3);' \
    "2"

# --------------------------------------------------------
# BUG #8 — OP_ABS trunca int64 para int32
# --------------------------------------------------------
echo ""
echo "--- BUG-8: OP_ABS com valores > 2^31 (abs é opcode, não math.abs) ---"

run_test "abs(-2147483648) = 2147483648 (não wrap)" \
    'print(abs(-2147483648));' \
    "2147483648"

run_test "abs(-2147483649) = 2147483649" \
    'print(abs(-2147483649));' \
    "2147483649"

run_test "abs(2147483648) = 2147483648" \
    'print(abs(2147483648));' \
    "2147483648"

# --------------------------------------------------------
# GC stress — Bugs #1 e #2
# GC não marca OBJ_STRUCT_DEF nem os field_names.
# Forçar GC com muitos objectos para revelar use-after-free.
# --------------------------------------------------------
echo ""
echo "--- GC stress: OBJ_STRUCT_DEF marking (Bugs #1, #2) ---"

run_test "classe simples sobrevive a GC pressure" \
    'class Point { var x; var y; def init(x, y) { self.x = x; self.y = y; } def sum() { return self.x + self.y; } } var pts = []; var i = 0; while (i < 500) { pts.push(Point(i, i * 2)); i = i + 1; } var freed = collect(); var last = pts[499]; print(last.sum());' \
    "1497"

run_test "GC não colecta class def com instâncias vivas" \
    'class Box { var v; def init(v) { self.v = v; } def get() { return self.v; } } var b = Box(123); var noise = []; var i = 0; while (i < 200) { noise.push("str" + str(i)); i = i + 1; } noise = nil; collect(); print(b.get());' \
    "123"

run_test "herança sobrevive a GC: super method call" \
    'class A { def compute() { return 7; } } class B : A { def compute() { return super.compute() + 1; } } var b = B(); var noise = []; var i = 0; while (i < 300) { noise.push(str(i)); i = i + 1; } noise = nil; collect(); print(b.compute());' \
    "8"

# --------------------------------------------------------
# BUG #18 — LoopCtx::breaks[64] overflow não verificado
# Mais de 64 breaks dentro do mesmo loop → buffer overflow no compiler.
# --------------------------------------------------------
echo ""
echo "--- BUG-18: LoopCtx::breaks array overflow (>64 breaks) ---"

# 64 breaks: OK. 65 breaks: overflow → "Expected end of file" no compiler.
# Threshold exacto confirmado experimentalmente.
code64breaks="var x = 0; var i = 0; while (i < 200) { "
for j in $(seq 1 64); do
    code64breaks="${code64breaks}if (i == ${j}) { x = x + 1; break; } "
done
code64breaks="${code64breaks}i = i + 1; } print(x);"

code65breaks="var x = 0; var i = 0; while (i < 200) { "
for j in $(seq 1 65); do
    code65breaks="${code65breaks}if (i == ${j}) { x = x + 1; break; } "
done
code65breaks="${code65breaks}i = i + 1; } print(x);"

run_test "64 breaks num loop (dentro do limite)" \
    "$code64breaks" \
    "1"

run_test "65 breaks num loop (overflow → compile error)" \
    "$code65breaks" \
    "1"

# --------------------------------------------------------
# BUG #25 — UpvalDesc::index é uint8_t → max 255 upvalues
# --------------------------------------------------------
echo ""
echo "--- BUG-25: upvalue index uint8_t limit ---"

# Gerar closure com 10 upvalues (razoável, não wrapping)
run_test "closure com muitos upvalues (10)" \
    'def make() { var a=1; var b=2; var c=3; var d=4; var e=5; var f=6; var g=7; var h=8; var i=9; var j=10; def sum() { return a+b+c+d+e+f+g+h+i+j; } return sum; } print(make()());' \
    "55"

# --------------------------------------------------------
# Extras — patterns do zenpy que são bons smoke tests
# --------------------------------------------------------
echo ""
echo "--- Smoke: patterns validados no zenpy ---"

run_test "multi-return via array: duas funções partilham upvalue" \
    'def make() { var x = 0; def inc() { x = x + 1; return x; } def get() { return x; } inc(); inc(); inc(); return get; } var g = make(); print(g());' \
    "3"

run_test "closures independentes nao interferem" \
    'def make(n) { def f() { return n; } return f; } var a = make(1); var b = make(2); var c = make(3); print(a()); print(b()); print(c());' \
    "1
2
3"

run_test "closure recursiva via upvalue" \
    'def make_fib() { def fib(n) { if (n <= 1) { return n; } return fib(n-1) + fib(n-2); } return fib; } var fib = make_fib(); print(fib(10));' \
    "55"

echo ""
echo "========================================================"
TOTAL=$((PASS+FAIL+CRASH))
echo " Results: $PASS/$TOTAL passed  |  $FAIL failed  |  $CRASH timeouts/crashes"
echo "========================================================"

[ $FAIL -eq 0 ] && [ $CRASH -eq 0 ] && exit 0 || exit 1
