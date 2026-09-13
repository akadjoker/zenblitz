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
    extern const CommandDecl bb3d_cmds_buffer[];
    extern const int bb3d_cmds_buffer_count;
    extern const CommandDecl bb3d_cmds_input[];
    extern const int bb3d_cmds_input_count;
    extern const CommandDecl bb3d_cmds_gfxmode[];
    extern const int bb3d_cmds_gfxmode_count;
    extern const CommandDecl bb3d_cmds_entityquery[];
    extern const int bb3d_cmds_entityquery_count;
    extern const CommandDecl bb3d_cmds_world[];
    extern const int bb3d_cmds_world_count;
    extern const CommandDecl bb3d_cmds_texture[];
    extern const int bb3d_cmds_texture_count;
    extern const CommandDecl bb3d_cmds_collision[];
    extern const int bb3d_cmds_collision_count;
    extern const CommandDecl bb3d_cmds_meshutil[];
    extern const int bb3d_cmds_meshutil_count;
    extern const CommandDecl bb3d_cmds_surface[];
    extern const int bb3d_cmds_surface_count;
    extern const CommandDecl bb3d_cmds_sprite[];
    extern const int bb3d_cmds_sprite_count;
    extern const CommandDecl bb3d_cmds_particle[];
    extern const int bb3d_cmds_particle_count;
    extern const CommandDecl bb3d_cmds_sound[];
    extern const int bb3d_cmds_sound_count;

    void install_graphics_commands(VM *vm)
    {
        install_commands(vm, bb3d_cmds_graphics, bb3d_cmds_graphics_count);
        install_commands(vm, bb3d_cmds_canvas, bb3d_cmds_canvas_count);
        install_commands(vm, bb3d_cmds_image, bb3d_cmds_image_count);
        install_commands(vm, bb3d_cmds_buffer, bb3d_cmds_buffer_count);
        install_commands(vm, bb3d_cmds_input, bb3d_cmds_input_count);
        install_commands(vm, bb3d_cmds_gfxmode, bb3d_cmds_gfxmode_count);
        install_commands(vm, bb3d_cmds_entityquery, bb3d_cmds_entityquery_count);
        install_commands(vm, bb3d_cmds_world, bb3d_cmds_world_count);
        install_commands(vm, bb3d_cmds_texture, bb3d_cmds_texture_count);
        install_commands(vm, bb3d_cmds_collision, bb3d_cmds_collision_count);
        install_commands(vm, bb3d_cmds_meshutil, bb3d_cmds_meshutil_count);
        install_commands(vm, bb3d_cmds_surface, bb3d_cmds_surface_count);
        install_commands(vm, bb3d_cmds_sprite, bb3d_cmds_sprite_count);
        install_commands(vm, bb3d_cmds_particle, bb3d_cmds_particle_count);
        install_commands(vm, bb3d_cmds_sound, bb3d_cmds_sound_count);
    }
}
