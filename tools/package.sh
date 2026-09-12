#!/usr/bin/env bash
# ==============================================================
# package.sh — stage a complete zenblitz distribution.
#
# Usage:
#   tools/package.sh <platform-name> [exe-suffix]
#
# Produces stage/zenblitz/ holding everything a user needs:
#
#   zenblitz[.exe]      compiler + runtime
#   zenblitz-rt[.exe]   runtime only — the stub `--build` appends programs
#                       to, and what a released game ships with
#   userlibs/           user library declarations (Blitz's userlibs)
#   Games/ Samples/ tutorials/   sample programs, when present
#   README.md
#
# then archives it as zenblitz-<platform-name>.{tar.gz,zip}.
# ==============================================================
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "usage: $0 <platform-name> [exe-suffix]" >&2
    exit 2
fi

platform="$1"
exe="${2:-}"
root="$(cd "$(dirname "$0")/.." && pwd)"
stage="$root/stage/zenblitz"

rm -rf "$root/stage"
mkdir -p "$stage"

# --- binaries ---
for b in zenblitz zenblitz-rt; do
    src="$root/bin/$b$exe"
    [[ -f "$src" ]] || { echo "missing binary: $src" >&2; exit 1; }
    cp "$src" "$stage/"
done
# Debug symbols are not worth the download; strip when the toolchain can.
if command -v strip >/dev/null 2>&1; then
    strip "$stage/zenblitz$exe" "$stage/zenblitz-rt$exe" 2>/dev/null || true
fi

# --- docs, samples, userlibs ---
for f in README.md LICENSE PLANO_BLITZ.md PLANO_BLITZ3D.md; do
    [[ -f "$root/$f" ]] && cp "$root/$f" "$stage/"
done
for d in userlibs Games Samples tutorials; do
    [[ -d "$root/$d" ]] && cp -r "$root/$d" "$stage/"
done

# --- archive ---
cd "$root/stage"
if [[ "$platform" == windows* ]]; then
    zip -qr "$root/zenblitz-$platform.zip" zenblitz
    ls -la "$root/zenblitz-$platform.zip"
else
    tar czf "$root/zenblitz-$platform.tar.gz" zenblitz
    ls -la "$root/zenblitz-$platform.tar.gz"
fi
