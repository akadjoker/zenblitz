# Classic BlitzIDE-style editor using iGUI and its SDL2 backend.
# Assets and their license notices live in editor/assets.

# NativeFileDialogProvider is one class (see NativeFileDialogProvider.hpp)
# with a platform-specific .cpp: dirent.h/sys/stat.h on Linux/Mac, native
# Win32 (FindFirstFileW) on Windows - Editor.hpp/main.cpp never branch on
# platform themselves.
if (WIN32)
    set(ZEN_NATIVE_FILE_DIALOG_SRC NativeFileDialogProvider_win32.cpp)
else()
    set(ZEN_NATIVE_FILE_DIALOG_SRC NativeFileDialogProvider_posix.cpp)
endif()

add_executable(zenblitz-editor
    main.cpp
    Editor.cpp
    EditorSettings.cpp
    Toolbar.cpp
    FindBar.cpp
    SymbolPanel.cpp
    SymbolIndex.cpp
    OutputPanel.cpp
    Runner.cpp
    FileIO.cpp
    ${ZEN_NATIVE_FILE_DIALOG_SRC}
)
target_link_libraries(zenblitz-editor PRIVATE igui_sdl2 igui)
# The editor plays scripts through zenblitz3d, and these are the library paths
# a native build driven from the editor would hand its compiler - but
# ZEN_BUILD_EDITOR is ON by default while ZEN_BUILD_ENGINE is OFF, so the
# $<TARGET_FILE:...> expressions only have a target to name when the engine is
# part of this configuration. Unguarded, they failed the generate step for the
# whole project (the same trap the tests target at the bottom of this file
# hit). Nothing under editor/ reads these definitions yet; a 2D-only
# configuration simply does without them.
if (TARGET zenblitz_engine AND TARGET gpu_sdl AND TARGET gpu AND TARGET gpu_common AND TARGET SDL2)
target_compile_definitions(zenblitz-editor PRIVATE
    ZEN_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    ZEN_EDITOR_BUILD_DIR="${CMAKE_BINARY_DIR}"
    ZEN_EDITOR_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
    ZEN_EDITOR_ENGINE_LIBRARY="$<TARGET_FILE:zenblitz_engine>"
    ZEN_EDITOR_GPU_SDL_LIBRARY="$<TARGET_FILE:gpu_sdl>"
    ZEN_EDITOR_GPU_LIBRARY="$<TARGET_FILE:gpu>"
    ZEN_EDITOR_GPU_COMMON_LIBRARY="$<TARGET_FILE:gpu_common>"
    ZEN_EDITOR_SDL_LIBRARY="$<TARGET_FILE:SDL2>"
)
endif()
if (WIN32)
    # SHGetKnownFolderPath needs Shell32 + Ole32 (CoTaskMemFree).
    target_link_libraries(zenblitz-editor PRIVATE Shell32 Ole32)
endif()

set_target_properties(zenblitz-editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

# SDL2.dll beside bin/zenblitz-editor.exe on Windows; no-op on Linux/Mac
# and against a system SDL2. (SDL2main itself comes from igui_sdl2, which
# main.cpp's #include <SDL.h> actually needs - see zen_copy_sdl2_runtime's
# comment in the root CMakeLists.txt.)
zen_copy_sdl2_runtime(zenblitz-editor)

# No ZEN_EDITOR_ASSET_DIR: the toolbar bitmap is compiled in (see
# ToolbarIcons.h), so the exe no longer carries this machine's absolute
# source path around and keeps working when copied elsewhere.

# Focused regressions, explicitly built so normal editor builds stay small.
add_executable(zenblitz-editor-tests EXCLUDE_FROM_ALL tests/classic.cpp SymbolIndex.cpp)
target_include_directories(zenblitz-editor-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(zenblitz-editor-tests PRIVATE igui_sdl2 igui)
target_compile_options(zenblitz-editor-tests PRIVATE -UNDEBUG)
if (WIN32)
    target_link_libraries(zenblitz-editor-tests PRIVATE Shell32 Ole32)
endif()

target_sources(zenblitz-editor-tests PRIVATE Editor.cpp EditorSettings.cpp Toolbar.cpp FindBar.cpp
    SymbolPanel.cpp OutputPanel.cpp Runner.cpp FileIO.cpp ${ZEN_NATIVE_FILE_DIALOG_SRC})
# The runner needs the engine and the GPU libraries it links; the tests target is
# only generated when they are part of the configuration, otherwise the
# $<TARGET_FILE:...> expressions below fail the generate step for the whole
# project (a plain 2D build has no zenblitz_engine).
if (TARGET zenblitz_engine AND TARGET gpu_sdl AND TARGET gpu AND TARGET gpu_common AND TARGET SDL2)
target_compile_definitions(zenblitz-editor-tests PRIVATE
    ZEN_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    ZEN_EDITOR_BUILD_DIR="${CMAKE_BINARY_DIR}"
    ZEN_EDITOR_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
    ZEN_EDITOR_ENGINE_LIBRARY="$<TARGET_FILE:zenblitz_engine>"
    ZEN_EDITOR_GPU_SDL_LIBRARY="$<TARGET_FILE:gpu_sdl>"
    ZEN_EDITOR_GPU_LIBRARY="$<TARGET_FILE:gpu>"
    ZEN_EDITOR_GPU_COMMON_LIBRARY="$<TARGET_FILE:gpu_common>"
    ZEN_EDITOR_SDL_LIBRARY="$<TARGET_FILE:SDL2>"
)
endif()
