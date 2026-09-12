#ifndef ZEN_RUNTIME_H
#define ZEN_RUNTIME_H

/*
** runtime.h — the part of libzen a compiled program needs, without the
** compiler. Runtime-only executables (zenblitz-rt and the platform
** runtimes) include this instead of compiler.h.
*/

#include "object.h" /* NativeFn */

namespace zen
{
    class VM;

    /* Register the Blitz command set and runtime helpers as VM globals.
       Compiler::compile() does this on first use; call it explicitly before
       loading precompiled bytecode so the native globals exist in the same
       slots the compiler assigned them. */
    void install_runtime(VM *vm);

    /* Load the Blitz3D-style userlibs in `dir` (its *.decls files and the
       shared libraries they name), adding what they declare to the command
       set. Call after install_runtime() and before compiling or loading a
       program, so the commands land in the same global slots both times.
       A missing directory is not an error; returns false only on a
       malformed .decls, with the reason in `err`. */
    bool install_userlibs(VM *vm, const char *dir, char *err = nullptr, int err_len = 0);

    /* A command declaration in the Blitz signature format the parser
       already understands (see bb_cmds.cpp for examples):
       return-tag name then a tag+name per parameter, tags %#$ for
       int/float/string, "=" for a default value. E.g.
       "%CreateCube%parent=0" — an engine (runtime3d) registers its comands
       (Graphics, DrawImage, CreateCube, ...) this way, exactly like the
       console command set install_runtime() installs, so the compiler
       treats them identically — no separate mechanism, no header from
       inside libzen needed. Call after install_runtime(). */
    struct CommandDecl { const char *sig; NativeFn fn; };
    void install_commands(VM *vm, const CommandDecl *cmds, int count);
}

#endif /* ZEN_RUNTIME_H */
