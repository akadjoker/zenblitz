#include "runtime3d.h"
#include "runtime.h"

using namespace zen;

namespace bb3d
{
    extern const CommandDecl bb3d_cmds_graphics[];
    extern const int bb3d_cmds_graphics_count;
    extern const CommandDecl bb3d_cmds_canvas[];
    extern const int bb3d_cmds_canvas_count;
    extern const CommandDecl bb3d_cmds_image[];
    extern const int bb3d_cmds_image_count;
    extern const CommandDecl bb3d_cmds_world[];
    extern const int bb3d_cmds_world_count;
    extern const CommandDecl bb3d_cmds_texture[];
    extern const int bb3d_cmds_texture_count;
    extern const CommandDecl bb3d_cmds_collision[];
    extern const int bb3d_cmds_collision_count;

    void install_graphics_commands(VM *vm)
    {
        install_commands(vm, bb3d_cmds_graphics, bb3d_cmds_graphics_count);
        install_commands(vm, bb3d_cmds_canvas, bb3d_cmds_canvas_count);
        install_commands(vm, bb3d_cmds_image, bb3d_cmds_image_count);
        install_commands(vm, bb3d_cmds_world, bb3d_cmds_world_count);
        install_commands(vm, bb3d_cmds_texture, bb3d_cmds_texture_count);
        install_commands(vm, bb3d_cmds_collision, bb3d_cmds_collision_count);
    }
}
