add_library(zenblitz_runtime3d STATIC
    src/runtime3d.cpp
    src/bb_cmds_graphics.cpp
    src/bb_cmds_canvas.cpp
    src/bb_cmds_image.cpp
    src/bb_cmds_buffer.cpp
    src/bb_cmds_input.cpp
    src/bb_cmds_gfxmode.cpp
    src/bb_cmds_entityquery.cpp
    src/bb_cmds_world.cpp
    src/bb_cmds_texture.cpp
    src/bb_cmds_collision.cpp
    src/bb_cmds_meshutil.cpp
    src/bb_cmds_surface.cpp
    src/bb_cmds_sprite.cpp
    src/bb_cmds_particle.cpp
    src/bb_cmds_sound.cpp
)
target_include_directories(zenblitz_runtime3d PUBLIC include)
target_link_libraries(zenblitz_runtime3d PUBLIC zen_static zenblitz_engine)
target_compile_options(zenblitz_runtime3d PRIVATE $<$<CONFIG:Release>:-O3>)

# zenblitz3d: Marco 1 CLI (compiles and runs a .bb with the window/input
# commands). Reuses cli/backend_stdio.cpp for Print/files, same as zenblitz.
add_executable(zenblitz3d src/main3d.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../cli/backend_stdio.cpp)
target_include_directories(zenblitz3d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../cli)
# SDL2::SDL2 is created by the vendored extern/SDL subdirectory in the root
# build. Keep this dependency explicit so this runtime cannot fall back to a
# system SDL installation.
target_link_libraries(zenblitz3d PRIVATE zenblitz_runtime3d SDL2::SDL2)
target_compile_options(zenblitz3d PRIVATE $<$<CONFIG:Release>:-O3>)
set_target_properties(zenblitz3d PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
# SDL2.dll beside bin/zenblitz3d.exe on Windows; no-op on Linux/Mac.
# main3d.cpp has a plain main() (no #include <SDL.h>), so this target must not
# link SDL2::SDL2main.
zen_copy_sdl2_runtime(zenblitz3d)
