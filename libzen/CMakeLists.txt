cmake_minimum_required(VERSION 3.10)
project(libzen LANGUAGES CXX)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# The runtime: VM, GC, bytecode loader, and the Blitz command set. This is
# everything a compiled program needs; zenblitz-rt and the platform runtimes
# link only this.
set(ZEN_VM_SRCS
    src/vm.cpp src/vm_dispatch.cpp src/memory.cpp src/zen_arena.cpp
    src/debug.cpp src/runtime.cpp
    src/bb_std.cpp src/bb_type.cpp src/bb_runtime.cpp
    src/bb_cmds.cpp src/bb_cmds_io.cpp src/bb_userlibs.cpp)

# The compiler: Blitz front-end, code generator, and the bytecode emitter.
set(ZEN_COMPILER_SRCS
    src/bb_toker.cpp src/bb_parser.cpp src/bb_semant.cpp src/bb_codegen.cpp
    src/emitter.cpp src/compiler.cpp)

# bytecode.cpp is built twice: the runtime needs the loader, while writing
# .zbc files is the compiler's job (ZEN_NO_BYTECODE_WRITER drops the writer
# and the stdio calls it makes).
add_library(zen_bytecode_rt_obj OBJECT src/bytecode.cpp)
target_compile_definitions(zen_bytecode_rt_obj PRIVATE ZEN_NO_BYTECODE_WRITER)
add_library(zen_bytecode_obj OBJECT src/bytecode.cpp)

# Each file is compiled once (object libraries) and combined into:
#   zen_vm      runtime only                  -> zenblitz-rt
#   zen_static  runtime + compiler (libzen.a) -> zenblitz, other projects
#   zen_shared  the same as a shared library
add_library(zen_vm_obj OBJECT ${ZEN_VM_SRCS})
add_library(zen_compiler_obj OBJECT ${ZEN_COMPILER_SRCS})
foreach(t zen_vm_obj zen_compiler_obj zen_bytecode_obj zen_bytecode_rt_obj)
    target_include_directories(${t} PUBLIC include include/zen PRIVATE src third_party)
    target_compile_options(${t} PRIVATE $<$<CONFIG:Release>:-O3>)
    target_compile_definitions(${t} PRIVATE $<$<CONFIG:Release>:NDEBUG>)
endforeach()

add_library(zen_vm STATIC $<TARGET_OBJECTS:zen_vm_obj> $<TARGET_OBJECTS:zen_bytecode_rt_obj>)
add_library(zen_static STATIC $<TARGET_OBJECTS:zen_vm_obj> $<TARGET_OBJECTS:zen_bytecode_obj>
                              $<TARGET_OBJECTS:zen_compiler_obj>)
add_library(zen_shared SHARED $<TARGET_OBJECTS:zen_vm_obj> $<TARGET_OBJECTS:zen_bytecode_obj>
                              $<TARGET_OBJECTS:zen_compiler_obj>)
foreach(t zen_vm zen_static zen_shared)
    target_include_directories(${t} PUBLIC include include/zen)
endforeach()
# Users of zen_vm see the same bytecode.h the library was built with.
target_compile_definitions(zen_vm INTERFACE ZEN_NO_BYTECODE_WRITER)
set_target_properties(zen_vm PROPERTIES OUTPUT_NAME zen_vm)
set_target_properties(zen_static PROPERTIES OUTPUT_NAME zen)
set_target_properties(zen_shared PROPERTIES OUTPUT_NAME zen)
