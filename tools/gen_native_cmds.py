#!/usr/bin/env python3
"""Generate native command metadata from the existing BBCommand tables."""

import re
import sys
from pathlib import Path

ENTRY = re.compile(r'\{"([^"\\]*(?:\\.[^"\\]*)*)"\s*,\s*(c_[A-Za-z0-9_]+)\}')
TAG = re.compile(r"([%#$])([^%#$]*)")
NATIVE_WRAPPERS = {
    "Print": "zen_print",
    "Graphics": "zen_cmd_graphics",
    "Graphics3D": "zen_cmd_graphics3d",
    "Flip": "zen_cmd_flip",
    "EndGraphics": "zen_cmd_endgraphics",
}


def collect(paths):
    commands = {}
    for path_text in paths:
        path = Path(path_text)
        for match in ENTRY.finditer(path.read_text(encoding="utf-8")):
            signature = bytes(match.group(1), "utf-8").decode("unicode_escape")
            symbol = match.group(2)
            commands.setdefault(signature, symbol)
    return sorted(commands.items())


def cpp_string(value):
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def parse_signature(signature):
    offset = 0
    return_tag = ""
    if signature and signature[0] in "%#$":
        return_tag = signature[0]
        offset = 1
    name_end = len(signature)
    for index in range(offset, len(signature)):
        if signature[index] in "%#$":
            name_end = index
            break
    name = signature[offset:name_end]
    offset = name_end
    params = []
    for param in TAG.finditer(signature, offset):
        token = param.group(2)
        if "=" in token:
            param_name, default = token.split("=", 1)
        else:
            param_name, default = token, ""
        params.append((param.group(1), param_name, default))
    return return_tag, name, params


def main(argv):
    if len(argv) < 3:
        raise SystemExit("usage: gen_native_cmds.py OUTPUT SOURCE...")
    output = Path(argv[1])
    commands = collect(argv[2:])
    lines = [
        "#ifndef ZEN_NATIVE_CMDS_HPP",
        "#define ZEN_NATIVE_CMDS_HPP",
        "",
        "struct ZenNativeCommand",
        "{",
        "    const char *signature;",
        "    const char *name;",
        "    const char *adapter;",
        "    const char *native_wrapper;",
        "    char return_kind;",
        "    unsigned parameter_count;",
        "    const char *parameters;",
        "};",
        "",
        "static const ZenNativeCommand zen_native_commands[] = {",
    ]
    for signature, symbol in commands:
        return_tag, name, params = parse_signature(signature)
        encoded = ";".join(
            f"{kind}:{param}:{default}" for kind, param, default in params
        )
        lines.append(
            f"    {{{cpp_string(signature)}, {cpp_string(name)}, {cpp_string(symbol)}, "
            f"{cpp_string(NATIVE_WRAPPERS.get(name, ''))}, '{return_tag or '0'}', "
            f"{len(params)}, {cpp_string(encoded)}}},"
        )
    lines.extend(
        [
            "};",
            "",
            "static const unsigned zen_native_command_count =",
            "    static_cast<unsigned>(sizeof(zen_native_commands) / sizeof(zen_native_commands[0]));",
            "",
            "#endif",
            "",
        ]
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="ascii")


if __name__ == "__main__":
    main(sys.argv)
