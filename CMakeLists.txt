cmake_minimum_required(VERSION 3.10)
project(zen LANGUAGES CXX)

# This repository is the core only: libzen (compiler + VM + builtin libs)
# and the plain `zen` CLI. Native modules (raylib, SDL2, GLFW, OpenGL,
# audio, sqlite, dnn, stb_*, gif, canvas), the zen_game / zen_gl runners and
# the graphics demos live in the separate zenvm-modules repository, which
# consumes this one as a git submodule via add_subdirectory().

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Common warnings
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

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

# --- Class API tests (C++ ↔ Script vtable) ---
add_subdirectory(tests/test_class_api)

# --- Fuzzer (requires clang + -DZEN_BUILD_FUZZ=ON) ---
option(ZEN_BUILD_FUZZ "Build libFuzzer harness" OFF)
add_subdirectory(tests/fuzz)

# --- Snapshot tests for tests/*.zen (ctest integration) ---
# Run with:
#   cd build && ctest --output-on-failure
# Refresh snapshots with:
#   tests/run_tests.sh ./bin/zen --update
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
