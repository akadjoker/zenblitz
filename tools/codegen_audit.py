#!/usr/bin/env python3
"""Audit the native codegen against the VM's command tables.

Two things it reports:

  * every command the VM publishes that zencc does not know, with how often it
    appears in the .bb samples (a command the emitter does not know cannot even
    be compiled: "Function 'x' not found");
  * every wrapper in codegen/zen_engine_sdk.h that is an empty stub, since those
    compile but silently do nothing.

Run from the repository root:  python3 tools/codegen_audit.py
"""
import collections
import glob
import os
import re

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

vm = set()
for f in glob.glob('runtime3d/src/bb_cmds_*.cpp'):
    for line in open(f, errors='ignore'):
        m = re.match(r'\s*\{"([^"]+)"\s*,', line)
        if m:
            name = re.split(r'[%$#]', m.group(1).lstrip('%#'), maxsplit=1)[0]
            if name:
                vm.add(name.lower())

zencc = open('codegen/zencc.cpp').read()
registered = {n.lower() for n in re.findall(r'reg\("([^"]+)"', zencc)}
missing = sorted(vm - registered)

counts = collections.Counter()
for f in glob.glob('**/*.bb', recursive=True):
    if 'attic' in f:
        continue
    try:
        txt = open(f, errors='ignore').read().lower()
    except OSError:
        continue
    for name in missing:
        c = len(re.findall(r'(?<![a-z0-9_])' + re.escape(name) + r'(?![a-z0-9_])', txt))
        if c:
            counts[name] += c

sdk = open('codegen/zen_engine_sdk.h').read()
stubs = sorted(set(re.findall(r'inline [A-Za-z0-9_:* ]+ (zen_[a-z_0-9]+)\([^)]*\)\s*\{\}', sdk)))

print('comandos da VM: %d | registados no zencc: %d | AUSENTES: %d (usados nos samples: %d)'
      % (len(vm), len(registered), len(missing), sum(1 for n in missing if counts[n])))
if missing:
    print('\nausentes, por uso:')
    for name in sorted(missing, key=lambda n: -counts[n]):
        print('  %-22s %4d' % (name, counts[name]))
print('\nstubs vazios no SDK (%d):' % len(stubs))
for name in stubs:
    print('  ' + name)
