cmake_minimum_required(VERSION 3.10)
project(zen LANGUAGES CXX)

# zenblitz: Blitz Basic compiler + the Zen VM runtime (libzen) + the CLI.

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Common warnings (MSVC spells them differently and rejects the GCC forms)
if(MSVC)
    add_compile_options(/W3)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# MSYS2/MinGW links every executable against its own runtime DLLs
# (libgcc_s_seh-1.dll, libstdc++-6.dll, and - once std::thread/mutex is
# pulled in - libwinpthread-1.dll) dynamically by default. None of those
# ship with Windows; they exist only on a machine with this exact MSYS2
# install, so any zenblitz*.exe copied elsewhere - or just run outside a
# shell that has ucrt64/bin on PATH - refused to start ("libstdc++-6.dll
# was not found"). All three are the toolchain's own libs, not the OS's,
# so mingw's linker driver treats `-static` as shorthand for exactly this
# trio - not a fully static exe (KERNEL32 and the api-ms-win-crt-* pieces
# of the Universal CRT stay dynamic; Windows 10+ ships those, and mingw has
# no static archive for them anyway). -static-libgcc/-static-libstdc++
# alone turned out not to be enough: statically pulling in libstdc++ made
# it need libwinpthread-1.dll as a new *direct* dependency of the exe,
# where before it was one level down, inside the now-gone libstdc++-6.dll.
# The vendored SDL2.dll is the one remaining non-system DLL these
# executables need, and it's copied next to the exe instead (see
# zen_copy_sdl2_runtime below), so together this is what makes bin/*.exe
# runnable standalone, not just from inside this build or this machine.
if(MINGW)
    add_link_options(-static-libgcc -static-libstdc++ -static)
endif()

# --- Build types ---
# Default to Release: a bare `cmake -B build` used to select Debug, which
# turns on ASan+UBSan below — anyone benchmarking without passing an explicit
# build type was measuring a sanitised -O0 binary. Ask for Debug to get it.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Sanitizer flags for Debug builds — only on a toolchain that actually
# ships the ASan/UBSan runtime. Some MinGW-w64 GCC installs (notably a
# plain MSYS2 UCRT64 install) accept -fsanitize=... at compile time but
# fail to *link* with "cannot read spec file 'libsanitizer.spec'"; probing
# with check_cxx_compiler_flag (compile-only) misses that, so this does a
# real try_compile instead, which builds and links a tiny program.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -fno-omit-frame-pointer)
    if(NOT MSVC)
        include(CheckCXXSourceCompiles)
        set(CMAKE_REQUIRED_FLAGS "-fsanitize=address,undefined")
        set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=address,undefined")
        check_cxx_source_compiles("int main(){return 0;}" ZEN_SANITIZERS_SUPPORTED)
        unset(CMAKE_REQUIRED_FLAGS)
        unset(CMAKE_REQUIRED_LINK_OPTIONS)
        if(ZEN_SANITIZERS_SUPPORTED)
            add_compile_options(-fsanitize=address,undefined)
            add_link_options(-fsanitize=address,undefined)
        else()
            message(WARNING "ASan/UBSan requested (Debug build) but this toolchain "
                             "can't link them (missing libsanitizer runtime) — "
                             "building Debug without sanitizers instead.")
        endif()
    endif()
endif()

# --- libzen (static + shared libraries) ---
add_subdirectory(libzen)

# --- CLI (zen executable) ---
# OFF lets a parent project that add_subdirectory()s this repo build its own
# `zen` target from cli/main.cpp with extra native libraries registered (see
# ZEN_CLI_EXTRA_LIBS_HEADER in cli/main.cpp) without a target-name clash.
option(ZEN_BUILD_CLI "Build the zen CLI (zen, zen_debug)" ON)
if(ZEN_BUILD_CLI)
    add_subdirectory(cli)
endif()

# --- Snapshot tests for tests/*.bb (ctest integration) ---
# Run with:
#   cd build && ctest --output-on-failure
# Refresh snapshots with:
#   tests/run_tests.sh ./bin/zenblitz --update
enable_testing()
if(ZEN_BUILD_CLI AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.sh")
    add_test(
        NAME zen_snapshot_suite
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.sh"
                "$<TARGET_FILE:zenblitz>"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    set_tests_properties(zen_snapshot_suite PROPERTIES
        TIMEOUT 600
        LABELS "snapshot"
    )
endif()

# --- SDL2 (vendored: extern/SDL, pinned to release-2.32.10) ---
# Both SDL2 consumers in this tree used to hunt for a *system* SDL2 -
# extern/GPU with find_package(SDL2), extern/iGUI with pkg-config - and a
# fresh MSYS2/UCRT64 (or MSVC) checkout has neither, so configure died
# inside extern/iGUI before the editor was even added. SDL2 is a submodule
# built in-tree instead: `git submodule update --init --recursive` is the
# whole install step, on every platform, with no pacman/vcpkg/apt.
#
# Called from both the engine and the editor blocks below; the TARGET guard
# makes the second call a no-op, and extern/GPU already checks for the same
# target before falling back to find_package. SDL compiles once into
# build/extern/SDL - a `cmake -B build` refresh re-runs configure but does
# not rebuild it; only wiping build/ does.
#
# Both include styles in this repo keep working: SDL's build tree exports
# ${SDL2_BINARY_DIR}/include *and* .../include/SDL2, so engine/'s
# <SDL2/SDL.h> and editor/ + iGUI's <SDL.h> both resolve.
macro(zen_require_sdl2)
    if(NOT TARGET SDL2::SDL2)
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/SDL/CMakeLists.txt")
            message(FATAL_ERROR
                "extern/SDL is empty - SDL2 is a submodule now. Run:\n"
                "    git submodule update --init --recursive")
        endif()
        # project() above declares CXX only; SDL is a C library.
        enable_language(C)
        # SDL 2.32 still declares cmake_minimum_required(VERSION 3.0.0...3.10),
        # which CMake 4 rejects outright ("compatibility with CMake < 3.5 has
        # been removed"). This is the escape hatch CMake 4 provides for
        # exactly that case - it raises SDL's floor without editing SDL.
        set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
        set(SDL_SHARED ON CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
        set(SDL_TEST OFF CACHE BOOL "" FORCE)
        set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
        set(SDL2_DISABLE_UNINSTALL ON CACHE BOOL "" FORCE)
        add_subdirectory(extern/SDL EXCLUDE_FROM_ALL)
        unset(CMAKE_POLICY_VERSION_MINIMUM)
        # -Wall -Wextra (and Debug's sanitizers) are meant for our code, not
        # for a vendored dependency: -w comes last on the command line, so
        # GCC/Clang drop the warning noise out of SDL's own sources.
        #
        # The sanitizer flags need actively undoing, not just outweighing:
        # add_compile_options() above applies -fsanitize=address,undefined
        # to every target in this build (SDL included), but that instruments
        # SDL's own object files without ever linking libasan/libubsan into
        # the resulting libSDL2 shared library - undefined __asan_*/__ubsan_*
        # symbols only surface at final link time, for whichever executable
        # first calls into an SDL source file the sanitizer actually
        # instrumented (SDL_blendfillrect.c's software blitter, seen in
        # practice). A "-w" appended after "-fsanitize=..." cannot remove a
        # codegen flag, only silence warnings, so this needs its own
        # -fno-sanitize=all appended after it on SDL's targets specifically.
        if(NOT MSVC)
            foreach(zen_sdl_target SDL2 SDL2main)
                if(TARGET ${zen_sdl_target})
                    target_compile_options(${zen_sdl_target} PRIVATE -w -fno-sanitize=all)
                    target_link_options(${zen_sdl_target} PRIVATE -fno-sanitize=all)
                endif()
            endforeach()
        endif()
    endif()
endmacro()

# Copies SDL2.dll next to a just-linked executable. A vendored SDL builds
# as a shared library, and Windows resolves DLLs from the exe's own
# directory - our exes land in bin/, outside the build tree where the DLL
# is produced, so without this bin/*.exe only starts from inside the build
# tree. WIN32-only and a TARGET check, so this is a no-op on Linux/Mac and
# against a system SDL2.
#
# Deliberately NOT in the business of linking SDL2::SDL2main: that shim
# supplies WinMain, which calls a symbol named SDL_main - only present when
# the executable's own entry-point .cpp does `#include <SDL.h>` (SDL_main.h
# then #defines main to SDL_main). runtime3d/src/main3d.cpp doesn't - it's
# a console-style CLI with a plain main() - so linking SDL2main there fails
# with "undefined reference to SDL_main". editor/main.cpp does include
# <SDL.h> and needs the shim, but gets it transitively and in the right
# link order through igui_sdl2 (see extern/iGUI/CMakeLists.txt), which is
# where that call belongs: on the target whose own source makes it true.
function(zen_copy_sdl2_runtime target)
    if(WIN32 AND TARGET SDL2)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:SDL2>" "$<TARGET_FILE_DIR:${target}>"
            COMMENT "Copying SDL2.dll next to ${target}")
    endif()
endfunction()

# --- zenblitz3d: the engine build (window, input, 2D/3D rendering) ---
# OFF by default so every existing target (zenblitz, zenblitz-rt, and every
# CI job — Android, Emscripten, MSVC included) keeps building without SDL or
# a GPU backend, exactly as verified today. See PLANO_ENGINE.md.
#
# Math: engine/ uses blitz::Vector/Matrix (Geom.h, ported from Blitz3D
# itself) for everything the game/engine code touches, and engine::Matrix4
# (Matrix4.h) as the one GPU-facing 4x4 the Batch's uniform buffer needs —
# there is no general-purpose math library dependency (no extern/math).
option(ZEN_BUILD_ENGINE "Build zenblitz3d (engine + SDL + GPU)" OFF)
if(ZEN_BUILD_ENGINE)
    zen_require_sdl2()
    set(GPU_BUILD_OPENGL ON CACHE BOOL "" FORCE)
    set(GPU_BUILD_SDL ON CACHE BOOL "" FORCE)
    set(GPU_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    set(GPU_BUILD_OPENGL_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(extern/GPU/gpu)
    add_subdirectory(engine)
    add_subdirectory(runtime3d)
endif()

# --- zenblitz-editor: a minimal .bb code editor with a Play button ---
# OFF by default, same reasoning as ZEN_BUILD_ENGINE: keeps every other
# target free of a GUI/SDL2 dependency. Uses iGUI (extern/iGUI, vendored as
# a git submodule together with its own containers dependency,
# extern/containers) over iGUI's SDL2 backend - not raylib, to stay in the
# same SDL family as the rest of this project, and not extern/GPU/OpenGL
# yet (iGUI has no GPU-backed backend of its own today; see PLANO_ENGINE.md
# "Editor"). See editor/main.cpp for what this first slice does and does
# not do yet.
option(ZEN_BUILD_EDITOR "Build zenblitz-editor (a minimal .bb IDE with Play)" ON)
if(ZEN_BUILD_EDITOR)
    zen_require_sdl2()
    set(IGUI_BUILD_SDL2_DEMO ON CACHE BOOL "" FORCE)
    set(IGUI_BUILD_RAYLIB_BACKEND OFF CACHE BOOL "" FORCE)
    set(IGUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    # editor/main.cpp only uses the immediate-mode Context (igui/igui_sdl2) -
    # the retained widget toolkit and the legacy compatibility layer aren't
    # needed and just add build time.
    set(IGUI_BUILD_RETAINED OFF CACHE BOOL "" FORCE)
    set(IGUI_BUILD_LEGACY OFF CACHE BOOL "" FORCE)
    set(IGUI_FONT_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/editor/assets/LiberationMono-Regular.ttf" CACHE FILEPATH "Editor font" FORCE)
    add_subdirectory(extern/iGUI)
    add_subdirectory(editor)
endif()
