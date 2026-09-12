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

    void install_commands(VM *vm, const CommandDecl *cmds, int count)
    {
        if (!vm || !cmds || count <= 0) return;
        /* CommandDecl mirrors bb::BBCommand field-for-field on purpose, so
           an external caller (runtime3d) never needs bb_runtime.h — but
           convert explicitly rather than reinterpret_cast between unrelated
           types, since nothing here depends on them staying bit-identical
           if either grows a field later. */
        bb::vector<bb::BBCommand> converted;
        converted.reserve((size_t)count);
        for (int i = 0; i < count; ++i)
            converted.push_back(bb::BBCommand{cmds[i].sig, cmds[i].fn});
        bb::bb_runtime_for(vm)->registerCommands(converted.data(), count);
    }
}
