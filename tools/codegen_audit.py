#!/usr/bin/env python3
"""Audit the native codegen against the VM's command tables.

Three things it reports:

  * every command the VM publishes (2D tables in libzen/src and 3D tables in
    runtime3d/src) that zencc does not know, with how often it appears in the
    .bb samples - a command the emitter does not know cannot even be compiled
    ("Function 'x' not found");
  * every wrapper in the SDK headers whose body is a no-op, i.e. only (void)
    casts and return 0/-1, since those compile but silently do nothing. The
    audit used to look for an empty `{}` alone and so missed stubs written as
    `{ (void)x; (void)y; return 0; }` - zen_load_sprite was one of those;
  * which of those no-op wrappers have a VM counterpart that does something.
    That last list is the one that matters: a no-op is only legitimate when
    the command is a no-op in the VM too (WireFrame, FlushJoy and CloseMovie
    do nothing there; LockBuffer/UnlockBuffer only validate their argument).

Run from the repository root:  python3 tools/codegen_audit.py
"""
import collections
import glob
import os
import re

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

VM_SOURCES = glob.glob('runtime3d/src/*.cpp') + glob.glob('libzen/src/*.cpp')


def read(path):
    return open(path, errors='ignore').read()


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def body_of(text, at):
    """Body of the block whose '{' follows position `at`, brace matched."""
    start = text.find('{', at)
    if start < 0:
        return None
    depth, i = 0, start
    while i < len(text):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[start + 1:i]
        i += 1
    return None


def is_noop(body):
    """True when the body does nothing: no calls, only (void) casts and a
    return of a constant (or nothing at all)."""
    if body is None:
        return False
    for statement in strip_comments(body).split(';'):
        s = ' '.join(statement.split())
        if not s or s.startswith('(void)'):
            continue
        if re.fullmatch(r'return\s*(-?\d+(\.\d+f?)?)?', s):
            continue
        return False
    return True


# --- the VM's commands, and what each of them does -------------------------
# A signature can contain an escaped quote in a defaulted string parameter
# ("Print$string=\"\""), and a string-returning command starts with the
# $ return marker ("$Upper$string"), so both have to be handled or exactly
# those commands are reported as "registered but not published by the VM"
# while the VM was publishing them all along.
COMMAND_LINE = re.compile(r'\{"((?:[^"\\]|\\.)+)"\s*,\s*(c_[A-Za-z0-9_]+)')

vm_commands = {}    # lowercase command name -> VM implementation function
vm_signatures = {}  # lowercase command name -> the signature string the VM publishes
vm_sources = [read(path) for path in VM_SOURCES]
for text in vm_sources:
    for line in text.splitlines():
        m = COMMAND_LINE.match(line.strip())
        if m:
            name = re.split(r'[%$#]', m.group(1).lstrip('%#$'), maxsplit=1)[0]
            if name:
                vm_commands[name.lower()] = m.group(2)
                vm_signatures.setdefault(name.lower(), m.group(1))

vm_trivial = {}     # VM implementation -> does nothing?
for func in set(vm_commands.values()):
    for text in vm_sources:
        m = re.search(r'static int ' + re.escape(func) + r'\s*\([^;{]*\)\s*(\{)', text)
        if m:
            vm_trivial[func] = is_noop(body_of(text, m.end() - 1))
            break

# --- what zencc registers, and what the emitter answers inline ---------------
zencc = read('codegen/zencc.cpp')
registrations = {}  # lowercase command name -> wrapper
for cmd, wrapper in re.findall(r'reg\("([^"]+)"\s*,\s*"([^"]+)"', zencc):
    registrations[cmd.lower()] = wrapper

# The emitter also answers a handful of core calls inline (zen_left, zen_mid,
# zen_instr ... are written straight into the expression). Those commands never
# reach reg(), so reading zencc alone reported the most common string functions
# in the language as missing.
intrinsics = {name.lower() for name in re.findall(r'call->ident == "([^"]+)"', read('codegen/c_codegen.cpp'))}

known = set(registrations) | intrinsics
missing = sorted(set(vm_commands) - known)

counts = collections.Counter()
for f in glob.glob('**/*.bb', recursive=True):
    if 'attic' in f:
        continue
    try:
        txt = read(f).lower()
    except OSError:
        continue
    for name in missing:
        c = len(re.findall(r'(?<![a-z0-9_])' + re.escape(name) + r'(?![a-z0-9_])', txt))
        if c:
            counts[name] += c

# --- the SDK's wrappers, and which of them do nothing ----------------------
noop_wrappers = {}
for header in ('codegen/zen_codegen_support.hpp', 'codegen/zenblitzsdk.h', 'codegen/zen_engine_sdk.h'):
    text = read(header)
    for m in re.finditer(r'inline [A-Za-z0-9_:*<>, ]+ (zen_[A-Za-z0-9_]+)\s*\(([^;{]*)\)\s*(\{)', text):
        noop_wrappers.setdefault(m.group(1), (is_noop(body_of(text, m.end() - 1)),
                                              m.group(0).split('(')[0].split()[-2]))
noop = sorted(name for name, (flag, _) in noop_wrappers.items() if flag)
noop_void = [n for n in noop if noop_wrappers[n][1] == 'void']
noop_value = [n for n in noop if noop_wrappers[n][1] != 'void']

wrapper_commands = collections.defaultdict(list)
for cmd, wrapper in registrations.items():
    wrapper_commands[wrapper].append(cmd)

real_gaps, legitimate = [], []
for wrapper in noop_void:
    commands = wrapper_commands.get(wrapper, [])
    if not commands:
        real_gaps.append((wrapper, 'nenhum comando aponta para este wrapper'))
        continue
    for cmd in commands:
        func = vm_commands.get(cmd)
        if func is None:
            real_gaps.append((wrapper, cmd + ' (a VM nao publica este comando)'))
        elif not vm_trivial.get(func, False):
            real_gaps.append((wrapper, cmd + ' -> ' + func))
        else:
            legitimate.append((wrapper, cmd + ' -> ' + func))

print('comandos da VM: %d (2D+3D) | reg. no zencc: %d | intrínsecos: %d | AUSENTES: %d (usados nos samples: %d)'
      % (len(vm_commands), len(registrations), len(intrinsics), len(missing),
         sum(1 for n in missing if counts[n])))
if missing:
    print('\nausentes, por uso:')
    for name in sorted(missing, key=lambda n: -counts[n]):
        print('  %-24s %4d' % (name, counts[name]))

extra = sorted(set(registrations) - set(vm_commands))
print('\nregistados no zencc que a VM nao publica (%d):' % len(extra))
for name in extra:
    print('  ' + name)

# A command registered *without* uses_window must have its wrapper in a header
# both flavours include; one that only exists in zen_engine_sdk.h compiles for
# a 3D program and fails for a 2D one (LoadSound did exactly that).
support = set()
for header in ('codegen/zen_codegen_support.hpp', 'codegen/zenblitzsdk.h'):
    support |= set(re.findall(r'inline [A-Za-z0-9_:*<>, ]+ (zen_[A-Za-z0-9_]+)\s*\(', read(header)))
engine_only = set(re.findall(r'inline [A-Za-z0-9_:*<>, ]+ (zen_[A-Za-z0-9_]+)\s*\(', read('codegen/zen_engine_sdk.h')))
stranded = []
for m in re.finditer(r'reg\("([^"]+)"\s*,\s*"([^"]+)"\s*(,\s*true)?\)', zencc):
    if not m.group(3) and m.group(2) in engine_only and m.group(2) not in support:
        stranded.append((m.group(1), m.group(2)))
print('\nregistados como core mas definidos só no header 3D (%d):' % len(stranded))
for cmd, wrapper in sorted(stranded):
    print('  %-20s %s' % (cmd, wrapper))

# --- signatures: a call the VM accepts, the emitter has to accept too -------
# The VM writes each command's signature as a string - the return marker, the
# name, then one marker per parameter (%int, #float, $string), "=name" for the
# ones with defaults - so "%LoadMesh$file%parent=0" documents two parameters.
# zencc builds the same list in code: p->insertDecl(...) lines feeding
# decl_*(name, p), or an inline lambda. Nothing compared the two until
# scare.bb's LoadMesh("file", parent) came back "Too many parameters for
# function 'loadmesh'" while the VM ran it happily - the declaration had only
# the file. A signature can also be *shorter* in the emitter (a call that
# passes every documented parameter is rejected) or longer (the callers'
# defaults differ), so both directions are printed.
def vm_param_count(signature):
    text = re.sub(r'\\"', '', signature)     # defaults may carry escaped quotes
    i = 1                                    # skip the return marker
    while i < len(text) and text[i] not in '%#$':
        i += 1
    return sum(1 for c in text[i:] if c in '%#$')


def zencc_param_count(text, at, args):
    # One parameter per param_*(...) helper or insertDecl line, except that the
    # list-of-names loop (`for (const char *n : {"a", "b", ...}) insertDecl(n,
    # ...)`) is several parameters behind one call. The two commands whose
    # parameter list comes from a shared builder lambda (buffer_copy_params)
    # are not counted here at all: the scanner cannot see into it, and guessing
    # produced false positives on every command sharing a block's DeclSeq.
    count = len(re.findall(r'\bparam_(?:str|int|float)\(', args))
    loops = re.findall(r'for \(const char \*\w+ : \{([^}]*)\}\)', args)
    for names in loops:
        count += len(re.findall(r'"', names)) // 2
    count += len(re.findall(r'insertDecl\(', args)) - len(loops)
    if count:
        return count
    if 'no_params()' in args:
        return 0
    if re.fullmatch(r'\s*\w+\s*', args):
        block = text.rfind('{', 0, at)
        return len(re.findall(r'insertDecl\(', text[block:at]))
    return None   # an expression the scanner does not understand


signature_gaps = []
unverifiable = []
decl_start = re.compile(r'\bdecl_(?:void|int|float|str)\("([^"]+)"\s*,')
for m in decl_start.finditer(zencc):
    name = m.group(1).lower()
    if name not in vm_commands:
        continue
    # Walk to the matching close paren to get the whole argument list, since
    # the lambda form spans lines and has parens of its own.
    depth, i = 1, m.end()
    while i < len(zencc) and depth:
        if zencc[i] == '(':
            depth += 1
        elif zencc[i] == ')':
            depth -= 1
        i += 1
    declared = zencc_param_count(zencc, m.start(), zencc[m.end():i])
    signature = vm_signatures.get(name)
    if signature is None:
        continue
    documented = vm_param_count(signature)
    if declared is None:
        unverifiable.append((name, documented, signature))
    elif declared != documented:
        signature_gaps.append((name, declared, documented, signature))

print('\nASSINATURAS - o emitter aceita menos/m mais parametros do que a VM documenta (%d):'
      % len(signature_gaps))
for name, declared, documented, signature in sorted(signature_gaps):
    print('  %-20s emitter: %d  VM: %d   %s' % (name, declared, documented, signature))
print('  (nao verificaveis, o contador passa ao lado: %s)'
      % ', '.join('%s VM:%d' % (n, d) for n, d, _ in sorted(unverifiable)))

print('\nwrappers sem efeito nenhum (%d: %d de comando, %d de valor)'
      % (len(noop), len(noop_void), len(noop_value)))
for name in noop_value:
    # Queries answer a constant on purpose (GraphicsDepth is 32, VertexW is
    # 1.0, the joystick and video-memory answers are 0 - the VM's c_ bodies do
    # the same), so they are listed for review rather than called gaps.
    command = (wrapper_commands.get(name) or ['?'])[0]
    func = vm_commands.get(command, '?')
    print('  (valor) %-24s %s -> %s%s' % (name, command, func,
                                          '' if vm_trivial.get(func, False) else '  <-- a VM calcula um valor'))
for name in noop_void:
    print('  (void)  ' + name)

print('\nno-ops LEGITIMOS - a VM tambem nao faz nada (%d):' % len(legitimate))
for wrapper, why in legitimate:
    print('  %-24s %s' % (wrapper, why))

print('\nDIVERGENCIAS REAIS - o wrapper nao faz nada e a VM faz (%d):' % len(real_gaps))
for wrapper, why in real_gaps:
    print('  %-24s %s' % (wrapper, why))
