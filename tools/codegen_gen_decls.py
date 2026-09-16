#!/usr/bin/env python3
"""Insert zencc declarations and registrations for a batch of commands.

Usage:
    python3 tools/codegen_gen_decls.py SIGS.json [DECL_ANCHOR] [REG_ANCHOR]

SIGS.json maps the bb command name to [wrapper_name, "VM table signature"],
where the signature is the string runtime3d publishes in its own command table
("%Name%int#float$string=default"). The emitter parses exactly that string, so
generating the declaration from it means the arity and the defaults cannot drift
from the VM.

The two anchors are substrings of existing lines in codegen/zencc.cpp; the new
declarations go after the first, the new registrations after the second.
Regenerating a command that is already declared or registered leaves it alone --
this script will not create a second copy of anything.
"""
import json
import re
import sys

CT = {'int': 'bb::Type::int_type', 'float': 'bb::Type::float_type', 'string': 'bb::Type::string_type'}
TYPECHAR = {'%': 'int', '$': 'string', '#': 'float'}
DEFAULT_ANCHOR_DECL = 'decl_void("clearsurface"'
DEFAULT_ANCHOR_REG = 'reg("clearsurface"'


def parse(sig):
    ret = 'void'
    i = 0
    if sig[0] in ('%', '#'):
        ret = 'int' if sig[0] == '%' else 'float'
        i = 1
    parts = []
    for m in re.finditer(r'([%$#])([A-Za-z_0-9]+)(?:=([-0-9.]+))?', sig[i:]):
        typ = TYPECHAR[m.group(1)]
        default = m.group(3)
        if default is not None:
            default = default + 'LL' if typ == 'int' else default + '.0'
        parts.append((m.group(2), typ, default))
    return ret, parts


def line_for(name, wrapper, sig):
    ret, parts = parse(sig)
    body = ''
    for pn, pt, pd in parts:
        body += ' q->insertDecl("%s", %s, bb::DECL_PARAM%s);' % (
            pn, CT[pt], ', new bb::ConstType(%s)' % pd if pd else '')
    lam = '[&]() { auto *q = new bb::DeclSeq();%s return q; }()' % body
    if ret == 'int':
        decl = '    decl_int("%s", %s);' % (name, lam)
    elif ret == 'void':
        decl = '    decl_void("%s", %s);' % (name, lam)
    else:
        decl = ('    decls->insertDecl("%s", new bb::FuncType(bb::Type::float_type, %s, false, false),'
                ' bb::DECL_FUNC);' % (name, lam))
    return decl, '    reg("%s", "%s", true);' % (name, wrapper)


def insert_after(src, anchor, lines):
    i = src.index(anchor)
    j = src.index('\n', i)
    return src[:j + 1] + '\n'.join(lines) + '\n' + src[j + 1:]


def already_known(src, name):
    return ('"%s"' % name) in src and re.search(r'reg\("%s"' % re.escape(name), src) is not None


def main():
    sigs = json.load(open(sys.argv[1]))
    decl_anchor = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_ANCHOR_DECL
    reg_anchor = sys.argv[3] if len(sys.argv) > 3 else DEFAULT_ANCHOR_REG

    src = open('codegen/zencc.cpp').read()
    decls, regs, skipped = [], [], []
    for name, (wrapper, sig) in sigs.items():
        if already_known(src, name):
            skipped.append(name)
            continue
        decl, reg = line_for(name, wrapper, sig)
        decls.append(decl)
        regs.append(reg)

    if decls:
        src = insert_after(src, decl_anchor, decls)
        src = insert_after(src, reg_anchor, regs)
        open('codegen/zencc.cpp', 'w').write(src)

    print('declaracoes: %d | regs: %d | ja existiam: %d %s'
          % (len(decls), len(regs), len(skipped), ' '.join(skipped)))
    print('wrappers:', ' '.join(w for w, _ in sigs.values()))


if __name__ == '__main__':
    main()
