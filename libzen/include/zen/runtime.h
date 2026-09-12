#ifndef ZEN_RUNTIME_H
#define ZEN_RUNTIME_H

/*
** runtime.h — the part of libzen a compiled program needs, without the
** compiler. Runtime-only executables (zenblitz-rt and the platform
** runtimes) include this instead of compiler.h.
*/

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
}

#endif /* ZEN_RUNTIME_H */
