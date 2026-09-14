cmake_minimum_required(VERSION 3.10)
project(zenblitz_cli LANGUAGES CXX)

# CMAKE_SOURCE_DIR on purpose: when zenvm is a subproject the binaries land
# in the parent's bin/, next to whatever else that project builds.
set(ZEN_BIN_DIR ${CMAKE_SOURCE_DIR}/bin)

# The plain CLI links only the core. A batteries-included `zen` with native
# modules is built by zenvm-modules from this same main.cpp — see
# ZEN_CLI_EXTRA_LIBS_HEADER there.
add_executable(zenblitz main.cpp backend_stdio.cpp)
set_target_properties(zenblitz PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ZEN_BIN_DIR})
target_compile_options(zenblitz PRIVATE $<$<CONFIG:Release>:-O3>)
target_compile_definitions(zenblitz PRIVATE $<$<CONFIG:Release>:NDEBUG>)
target_link_libraries(zenblitz zen_static)
if(NOT WIN32)
    target_link_options(zenblitz PRIVATE -rdynamic)
else()
    # Windows defaults to a 1 MB stack; the compiler's nested-function
    # CompilerState frames (~25 KB each, up to kMaxFuncNesting deep) need
    # the same 8 MB Linux gives by default.
    target_link_options(zenblitz PRIVATE -Wl,--stack,8388608)
endif()


# Runtime-only executable: what `zenblitz --build` appends the program to
# (it looks for this file next to itself). No compiler, so a distributed
# game carries only the VM and the command set.
add_executable(zenblitz-rt rt_main.cpp backend_stdio.cpp)
set_target_properties(zenblitz-rt PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ZEN_BIN_DIR})
target_compile_options(zenblitz-rt PRIVATE $<$<CONFIG:Release>:-O3>)
target_compile_definitions(zenblitz-rt PRIVATE $<$<CONFIG:Release>:NDEBUG>)
target_link_libraries(zenblitz-rt zen_vm)
add_dependencies(zenblitz zenblitz-rt)

# Debug trace build
add_executable(zenblitz_debug main.cpp backend_stdio.cpp)
set_target_properties(zenblitz_debug PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ZEN_BIN_DIR})
target_link_libraries(zenblitz_debug zen_static)
target_compile_definitions(zenblitz_debug PRIVATE ZEN_DEBUG_TRACE_EXEC)
if(WIN32)
    target_link_options(zenblitz_debug PRIVATE -Wl,--stack,8388608)
endif()
if(NOT WIN32)
    target_link_options(zenblitz_debug PRIVATE -rdynamic)
endif()
