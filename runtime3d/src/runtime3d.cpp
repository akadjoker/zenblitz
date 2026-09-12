#include "runtime3d.h"
#include "runtime.h"

using namespace zen;

namespace bb3d
{
    extern const CommandDecl bb3d_cmds_graphics[];
    extern const int bb3d_cmds_graphics_count;

    void install_graphics_commands(VM *vm)
    {
        install_commands(vm, bb3d_cmds_graphics, bb3d_cmds_graphics_count);
    }
}
