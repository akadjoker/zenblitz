#ifndef ZEN_COMPILER_H
#define ZEN_COMPILER_H

/*
** compiler.h — Blitz Basic compiler front-end for the Zen VM.
**
** Source text (Blitz3D syntax) -> ObjFunc holding the main program.
** Functions, Types, Dim arrays and Data are set up as VM globals during
** compilation; run the returned function with VM::run().
*/

#include "object.h"
#include "memory.h"

namespace zen
{
    class VM;

    /* Register the Blitz command set and runtime helpers as VM globals.
       compile() does this on first use; call it explicitly before loading
       precompiled bytecode so the native globals exist in the same slots. */
    void install_runtime(VM *vm);

    class Compiler
    {
    public:
        Compiler();
        ~Compiler();

        ObjFunc *compile(GC *gc, VM *vm, const char *source, const char *filename = "<script>");

        /* last error message ("" if the last compile succeeded) */
        const char *error() const { return error_; }
        /* extra runtime checks (array bounds per dimension, ...) */
        void set_debug(bool d) { debug_ = d; }

    private:
        char error_[512];
        bool debug_;
    };
}

#endif /* ZEN_COMPILER_H */
