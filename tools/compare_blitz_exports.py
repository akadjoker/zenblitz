#!/usr/bin/env python3
"""Compare public Blitz runtime registrations with native codegen commands."""

import argparse
import re
from pathlib import Path

RTSYM = re.compile(r'rtSym\s*\(\s*"((?:\\.|[^"\\])*)"\s*,\s*([A-Za-z_][A-Za-z0-9_]*)')
COMMAND = re.compile(r'\{"((?:\\.|[^"\\])*)"\s*,\s*(c_[A-Za-z0-9_]+)\}')


def decode(value):
    return bytes(value, "utf-8").decode("unicode_escape")


def collect(root, pattern, suffixes):
    entries = {}
    for path in sorted(root.rglob("*")):
        if path.suffix not in suffixes:
            continue
        for match in pattern.finditer(
            path.read_text(encoding="utf-8", errors="ignore")
        ):
            signature = decode(match.group(1))
            entries.setdefault(signature, (path, match.group(2)))
    return entries


def command_name(signature):
    return re.split(r"[%#$]", signature.lstrip("%#$"), maxsplit=1)[0]


def grouped_by_name(entries):
    result = {}
    for signature in entries:
        result.setdefault(command_name(signature), []).append(signature)
    return result


def print_entries(title, signatures, entries):
    print(f"\n{title} ({len(signatures)})")
    for signature in sorted(signatures):
        path, symbol = entries[signature]
        print(f"  {signature}\t{path}:{symbol}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--legacy-root",
        type=Path,
        default=Path("tmp/blitz3d"),
        help="legacy Blitz source tree (default: tmp/blitz3d)",
    )
    parser.add_argument(
        "--command-root",
        type=Path,
        action="append",
        default=[Path("libzen/src"), Path("runtime3d/src")],
        help="source tree containing codegen command tables; repeatable",
    )
    parser.add_argument(
        "--include-internal",
        action="store_true",
        help="include legacy _bb runtime/compiler helpers",
    )
    args = parser.parse_args()

    legacy = collect(args.legacy_root, RTSYM, {".c", ".cpp", ".h"})
    current = {}
    for root in args.command_root:
        current.update(collect(root, COMMAND, {".cpp"}))

    if not args.include_internal:
        legacy = {
            signature: entry
            for signature, entry in legacy.items()
            if not signature.startswith("_")
        }

    legacy_names = grouped_by_name(legacy)
    current_names = grouped_by_name(current)
    legacy_signatures = set(legacy)
    current_signatures = set(current)
    common_names = set(legacy_names) & set(current_names)
    mismatched_names = {
        name
        for name in common_names
        if not set(legacy_names[name]) & set(current_names[name])
    }
    missing_names = set(legacy_names) - set(current_names)

    print(f"Legacy registrations: {len(legacy)}")
    print(f"Codegen command entries: {len(current)}")
    print(f"Exact signature matches: {len(legacy_signatures & current_signatures)}")
    print(f"Missing exact signatures: {len(legacy_signatures - current_signatures)}")
    print(f"Names missing entirely: {len(missing_names)}")
    print(f"Names with signature mismatch: {len(mismatched_names)}")
    print(f"Codegen-only names: {len(set(current_names) - set(legacy_names))}")

    print_entries(
        "Names missing entirely",
        {signature for name in missing_names for signature in legacy_names[name]},
        legacy,
    )
    print("\nNames with signature mismatch")
    for name in sorted(mismatched_names):
        print(f"  {name}")
        print(f"    legacy:  {legacy_names[name]}")
        print(f"    codegen: {current_names[name]}")
    print_entries(
        "Codegen-only entries",
        current_signatures - legacy_signatures,
        current,
    )


if __name__ == "__main__":
    main()
