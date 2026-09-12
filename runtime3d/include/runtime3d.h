/*
** runtime3d.h — register zenblitz3d's commands (window, input; more added
** per milestone) on top of install_runtime()'s console command set.
*/
#ifndef RUNTIME3D_H
#define RUNTIME3D_H

namespace zen { class VM; }

namespace bb3d
{
    void install_graphics_commands(zen::VM *vm);
    // Frees every runtime-owned resource for this VM (scene, textures,
    // fonts, brushes, the World and the Platform/GL device). Call once,
    // after the program finishes.
    void shutdown_graphics(zen::VM *vm);
}

#endif
