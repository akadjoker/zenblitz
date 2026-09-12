/*
** bb_userlibs.h — Blitz3D-style user libraries.
**
** A .decls file declares functions living in a shared library:
**
**     .lib "user32.dll"
**     MessageBoxA%(hwnd%, text$, title$, flags%) : "MessageBoxA"
**
** The compiler needs the signatures to type-check calls; the runtime loads
** the library and calls through to the exported symbol. Both happen through
** the backend (zen::Backend::lib_*), so a platform without dynamic loading
** simply declares no loader and the commands report the library as missing
** instead of the runtime failing to build.
*/
#ifndef BB_USERLIBS_H
#define BB_USERLIBS_H

#include "bb_std.h"

namespace zen { class VM; }

namespace bb
{
    struct BBRuntime;

    /* Load every .decls file in `dir` and register what they declare.
       Missing directory is not an error (there simply are no user libs).
       Returns false only on a malformed .decls, with why in `err`. */
    bool load_userlibs(zen::VM *vm, BBRuntime *rt, const string &dir,
                       char *err = 0, int err_len = 0);
}

#endif
