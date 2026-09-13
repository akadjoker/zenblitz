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
