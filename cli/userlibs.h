/*
** userlibs.h — install the userlibs directory that sits next to the
** running executable, shared by the compiler and the runtime so that both
** register the same commands in the same global slots.
*/
#ifndef ZENBLITZ_USERLIBS_H
#define ZENBLITZ_USERLIBS_H

#include "vm.h"
#include "runtime.h"
#include "embedded.h"

namespace zen
{
    inline void install_userlibs_beside_exe(VM &vm, const char *who)
    {
        char dir[4096];
        if (!path_beside_exe("userlibs", dir, sizeof(dir))) return;
        char err[256] = {0};
        if (!install_userlibs(&vm, dir, err, sizeof(err)))
            fprintf(stderr, "%s: %s\n", who, err[0] ? err : "failed to load userlibs");
    }
}

#endif
