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

# --- Build types ---
# Default to Release: a bare `cmake -B build` used to select Debug, which
# turns on ASan+UBSan below — anyone benchmarking without passing an explicit
# build type was measuring a sanitised -O0 binary. Ask for Debug to get it.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Sanitizer flags for Debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -fno-omit-frame-pointer)
    add_compile_options(-fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
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

# --- zenblitz3d: the engine build (window, input, 2D/3D rendering) ---
# OFF by default so every existing target (zenblitz, zenblitz-rt, and every
# CI job — Android, Emscripten, MSVC included) keeps building without SDL or
# a GPU backend, exactly as verified today. See PLANO_ENGINE.md.
option(ZEN_BUILD_ENGINE "Build zenblitz3d (engine + SDL + GPU)" OFF)
if(ZEN_BUILD_ENGINE)
    set(GPU_BUILD_OPENGL ON CACHE BOOL "" FORCE)
    set(GPU_BUILD_SDL ON CACHE BOOL "" FORCE)
    set(GPU_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    set(GPU_BUILD_OPENGL_TESTS OFF CACHE BOOL "" FORCE)
    set(MATHC_NATIVE_ARCH OFF CACHE BOOL "" FORCE) # -march=native would break other machines/CI
    set(MATHC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(MATHC_BUILD_BENCH OFF CACHE BOOL "" FORCE)
    add_subdirectory(extern/GPU/gpu)
    add_subdirectory(extern/math)
    add_subdirectory(engine)
    add_subdirectory(runtime3d)
endif()
