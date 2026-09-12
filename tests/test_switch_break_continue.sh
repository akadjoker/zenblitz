#!/bin/bash
# test_switch_break_continue.sh — Regression: 'break' inside a switch case
# used to resolve against state_->loops[loop_depth-1], the nearest ENCLOSING
# LOOP — switch never pushed its own LoopCtx entry. So `break` inside a case
# silently broke out of the whole enclosing while/for/foreach instead of just
# the switch, and `switch` used outside any loop rejected `break` entirely
# ("'break' outside of loop.") even though break-in-switch is valid on its
# own. Fixed by giving switch its own LoopCtx entry (is_switch = true):
# break resolves to the nearest entry (switch or loop), continue skips over
# switch entries to reach the nearest real loop.
# Usage: ./tests/test_switch_break_continue.sh [path-to-zen-binary]

set -euo pipefail

ZEN="${1:-./build/cli/zen}"
PASS=0
FAIL=0
TOTAL=0

export ASAN_OPTIONS=detect_leaks=0

# Asserts stdout matches exactly (trailing newline ignored).
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

# Asserts a compile/runtime error containing $3, and non-zero exit.
run_error() {
    local name="$1"
    local code="$2"
    local expect_substr="$3"
    TOTAL=$((TOTAL + 1))

    local actual status
    set +e
    actual=$("$ZEN" -e "$code" 2>&1)
    status=$?
    set -e

    if [ "$status" -eq 0 ]; then
        FAIL=$((FAIL + 1))
        printf "  [%3d] %-55s FAIL (expected error, exit 0)\n" "$TOTAL" "$name"
        return
    fi

    case "$actual" in
        *"$expect_substr"*)
            PASS=$((PASS + 1))
            printf "  [%3d] %-55s OK\n" "$TOTAL" "$name"
            ;;
        *)
            FAIL=$((FAIL + 1))
            printf "  [%3d] %-55s FAIL\n" "$TOTAL" "$name"
            printf "        expected substring: %s\n" "$expect_substr"
            printf "        actual:             %s\n" "$actual"
            ;;
    esac
}

echo "--- switch: break/continue interaction with every enclosing loop kind ---"

# The exact reported shape: break inside a case must not swallow the rest
# of the enclosing foreach. Before the fix this printed only "case a".
run_exact "break in switch inside foreach runs every iteration" \
    'foreach (cmd in ["a", "b", "c"]) { switch (cmd) { case "a": { print("case a"); break; } default: { print("default"); } } print("after:" + cmd); }' \
    $'case a\nafter:a\ndefault\nafter:b\ndefault\nafter:c'

run_exact "break in switch inside while runs every iteration" \
    'var i = 0; while (i < 3) { switch (i) { case 0: { print("zero"); break; } default: { print("other", i); } } print("after:" + i); i = i + 1; }' \
    $'zero\nafter:0\nother 1\nafter:1\nother 2\nafter:2'

run_exact "break in switch inside numeric for runs every iteration" \
    'for (i = 0; i < 3; i = i + 1) { switch (i) { case 1: { print("one"); break; } default: { print("other", i); } } print("after:" + i); }' \
    $'other 0\nafter:0\none\nafter:1\nother 2\nafter:2'

run_exact "break in switch inside do-while runs every iteration" \
    'var i = 0; do { switch (i) { case 0: { print("zero"); break; } default: { print("other", i); } } print("after:" + i); i = i + 1; } while (i < 3);' \
    $'zero\nafter:0\nother 1\nafter:1\nother 2\nafter:2'

run_exact "break in switch inside 'loop' only exits the switch, needs its own break to exit the loop" \
    'var i = 0; loop { switch (i) { case 5: { print("done"); break; } default: {} } if (i == 5) { break; } i = i + 1; } print("i=" + i);' \
    $'done\ni=5'

echo ""
echo "--- switch: break/continue with no enclosing loop at all ---"

# switch on its own is a valid break target — previously this was a hard
# compile error ("'break' outside of loop.") even though the switch itself
# should absorb the break.
run_exact "break in switch with no enclosing loop compiles and runs" \
    'switch ("a") { case "a": { print("case a"); break; } default: { print("default"); } } print("after switch");' \
    $'case a\nafter switch'

run_error "continue with no enclosing loop (switch present) still errors" \
    'switch ("a") { case "a": { continue; } }' \
    "'continue' outside of loop"

run_error "break with truly nothing enclosing still errors" \
    'break;' \
    "'break' outside of loop"

run_error "continue with truly nothing enclosing still errors" \
    'continue;' \
    "'continue' outside of loop"

echo ""
echo "--- switch: continue must skip the switch and reach the real loop ---"

# continue inside a switch case must NOT be captured by the switch (a switch
# doesn't iterate) — it has to skip past the switch's LoopCtx entry and hit
# the enclosing foreach/while, same as if the switch weren't there.
run_exact "continue in switch inside foreach skips to next iteration" \
    'foreach (i in 0..5) { switch (i) { case 2: { print("skip"); continue; } default: { print("normal", i); } } print("after:" + i); }' \
    $'normal 0\nafter:0\nnormal 1\nafter:1\nskip\nnormal 3\nafter:3\nnormal 4\nafter:4'

echo ""
echo "--- switch: nesting ---"

# A switch nested inside another switch's case: break must exit only the
# INNER switch, and control must still reach the outer switch's own
# end-of-case fallthrough / trailing code.
run_exact "break in nested switch exits only the inner switch" \
    'switch (1) { case 1: { switch (2) { case 2: { print("inner"); break; } default: {} } print("outer continues"); break; } default: {} } print("done");' \
    $'inner\nouter continues\ndone'

# A loop nested inside a switch case, itself containing a switch: break in
# the innermost switch must not touch the loop or the outer switch.
run_exact "loop nested inside a switch case: inner switch break stays local" \
    'switch (1) { case 1: { var n = 0; while (n < 3) { switch (n) { case 1: { print("mid"); break; } default: { print("n", n); } } n = n + 1; } print("loop done"); break; } } print("switch done");' \
    $'n 0\nmid\nn 2\nloop done\nswitch done'

echo ""
echo "--- sanity: plain break/continue in a loop with no switch still work ---"

run_exact "break in a plain while loop (no switch) still works" \
    'var i = 0; while (true) { if (i == 3) { break; } print(i); i = i + 1; }' \
    $'0\n1\n2'

run_exact "continue in a plain foreach (no switch) still works" \
    'foreach (i in 0..4) { if (i == 2) { continue; } print(i); }' \
    $'0\n1\n3'

echo ""
echo "=== $PASS / $TOTAL PASSED ==="
if [ "$FAIL" -gt 0 ]; then
    echo "*** $FAIL TESTS FAILED ***"
    exit 1
else
    echo "ALL TESTS OK!"
    exit 0
fi
