/*
** runtime.cpp — runtime entry point (see runtime.h). Lives on the VM side
** of the library so zenblitz-rt links without the compiler.
*/
#include "runtime.h"
#include "bb_runtime.h"
#include "bb_userlibs.h"

namespace zen
{
    void install_runtime(VM *vm)
    {
        bb::bb_runtime_for(vm);
    }

    bool install_userlibs(VM *vm, const char *dir, char *err, int err_len)
    {
        if (!vm || !dir) return true;
        return bb::load_userlibs(vm, bb::bb_runtime_for(vm), dir, err, err_len);
    }
}
