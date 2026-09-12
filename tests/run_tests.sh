#!/usr/bin/env bash
# ==============================================================
# run_tests.sh — Snapshot test runner for tests/*.bb
#
# Usage:
#   tests/run_tests.sh <zenblitz-binary> [--update]
#
# Behaviour:
#   - For each tests/*.bb file (NOT in tests/snapshot_skip.txt):
#       1. Run with the given binary (combined stdout+stderr).
#       2. Strip trailing whitespace from each line, drop trailing blank
#          lines (output-end normalisation).
#       3. Compare with tests/expected/<name>.out.
#   - Files listed in tests/snapshot_skip.txt are skipped (one
#     basename per line; lines starting with # are comments).
#   - With --update the expected snapshots are (re)generated and
#     no comparison is performed.
#
# Exit code:
#   0  = all snapshots match (or --update succeeded)
#   1  = at least one mismatch
#   2  = usage / setup error
# ==============================================================

set -u

if [[ $# -lt 1 ]]; then
    echo "usage: $0 <zenblitz-binary> [--update]" >&2
    exit 2
fi

BIN="$1"
UPDATE=0
if [[ "${2:-}" == "--update" ]]; then
    UPDATE=1
fi

if [[ ! -x "$BIN" ]]; then
    echo "error: '$BIN' is not an executable file" >&2
    exit 2
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TESTS_DIR="$ROOT/tests"
EXPECTED_DIR="$TESTS_DIR/expected"
SKIP_FILE="$TESTS_DIR/snapshot_skip.txt"

mkdir -p "$EXPECTED_DIR"

# Build skip set (common file + optional per-platform file, e.g.
# snapshot_skip_windows.txt on MSYS2/Git-Bash runners)
declare -A SKIP=()
load_skip_file() {
    [[ -f "$1" ]] || return 0
    while IFS= read -r line; do
        line="${line%%#*}"          # strip comments
        line="${line//[$'\t\r ']/}" # strip whitespace
        [[ -z "$line" ]] && continue
        SKIP["$line"]=1
    done < "$1"
}
load_skip_file "$SKIP_FILE"
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) load_skip_file "$TESTS_DIR/snapshot_skip_windows.txt" ;;
esac

# Windows (MSYS2/Git-Bash) may hand the binary a mixed-form path (D:/...);
# capture it so path normalisation catches both spellings of ROOT.
ROOT_WIN=""
if command -v cygpath >/dev/null 2>&1; then
    ROOT_WIN="$(cygpath -m "$ROOT")"
fi

# Normalisation, so a snapshot is comparable across machines and checkouts:
#   - make ROOT-relative paths (and the Windows-form ROOT, if any), since the
#     snapshot may have been recorded from a different working directory
#     (or on a different machine entirely)
#   - rewrite the path inside File "..." to just the basename, and the same
#     for ", in <path>" traceback lines
#   - rstrip each line
#   - mask timing values (seconds=..., or a bare high-precision float on
#     its own line), which differ on every run and every machine
#   - drop leading and trailing blank lines
normalize() {
    local script='s|'"$ROOT"'/||g'
    if [[ -n "$ROOT_WIN" ]]; then
        script+=$'\n''s|'"$ROOT_WIN"'/||g'
    fi
    sed -e "$script" | sed -E \
           -e 's/[[:space:]]+$//' \
           -e 's|File "[^"]*/([^"/]+)"|File "\1"|g' \
           -e 's|, in [^ ]*/([^/ ]+)$|, in \1|' \
           -e 's/(seconds|elapsed|time)=[0-9]+\.?[0-9]*(e[-+]?[0-9]+)?/\1=<T>/gI' \
           -e 's/^[0-9]+\.[0-9]{4,}([eE][-+]?[0-9]+)?$/<T>/' | awk '
        { buf[NR] = $0
          if ($0 != "") { lastnonblank = NR; if (!firstnonblank) firstnonblank = NR } }
        END { if (firstnonblank) for (i = firstnonblank; i <= lastnonblank; i++) print buf[i] }
    '
}

PASS=0
FAIL=0
SKIPPED=0
UPDATED=0
FAIL_NAMES=""

shopt -s nullglob
for src in "$TESTS_DIR"/*.bb; do
    name="$(basename "$src" .bb)"
    base="$(basename "$src")"

    if [[ -n "${SKIP[$base]:-}" ]]; then
        printf '  [SKIP] %s\n' "$name"
        ((SKIPPED++))
        continue
    fi

    expected_path="$EXPECTED_DIR/$name.out"
    actual="$("$BIN" "$src" 2>&1 < /dev/null | normalize)"

    if [[ $UPDATE -eq 1 ]]; then
        printf '%s\n' "$actual" > "$expected_path"
        printf '  [UPD ] %s\n' "$name"
        ((UPDATED++))
        continue
    fi

    if [[ ! -f "$expected_path" ]]; then
        printf '  [MISS] %s (no expected snapshot — run with --update)\n' "$name"
        ((FAIL++))
        FAIL_NAMES+="    $name (missing snapshot)"$'\n'
        continue
    fi

    expected="$(cat "$expected_path" | normalize)"
    if [[ "$actual" == "$expected" ]]; then
        ((PASS++))
        printf '  [ OK ] %s\n' "$name"
    else
        ((FAIL++))
        FAIL_NAMES+="    $name"$'\n'
        printf '  [FAIL] %s\n' "$name"
        if [[ -n "${ZEN_TEST_VERBOSE:-}" ]]; then
            diff <(printf '%s\n' "$expected") <(printf '%s\n' "$actual") | head -40 | sed 's/^/        /'
        fi
    fi
done
shopt -u nullglob

echo
if [[ $UPDATE -eq 1 ]]; then
    echo "Updated $UPDATED snapshot(s); skipped $SKIPPED."
    exit 0
fi

echo "passed=$PASS  failed=$FAIL  skipped=$SKIPPED"
if [[ $FAIL -gt 0 ]]; then
    echo "Failures:"
    printf '%s' "$FAIL_NAMES"
    echo "(set ZEN_TEST_VERBOSE=1 to see diffs; or run with --update to refresh snapshots)"
    exit 1
fi
exit 0
