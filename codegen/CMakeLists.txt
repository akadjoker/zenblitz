cmake_minimum_required(VERSION 3.10)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(ZEN_NATIVE_CMDS ${CMAKE_CURRENT_BINARY_DIR}/generated/zen_native_cmds.hpp)
file(GLOB ZEN_COMMAND_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../libzen/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../runtime3d/src/*.cpp)
add_custom_command(
    OUTPUT ${ZEN_NATIVE_CMDS}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/gen_native_cmds.py
            ${ZEN_NATIVE_CMDS} ${ZEN_COMMAND_SOURCES}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../tools/gen_native_cmds.py ${ZEN_COMMAND_SOURCES}
    VERBATIM)
add_custom_target(zen_native_cmds DEPENDS ${ZEN_NATIVE_CMDS})

add_library(zen_codegen STATIC c_codegen.cpp)
target_include_directories(zen_codegen PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/../libzen/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../libzen/third_party)
add_dependencies(zen_codegen zen_native_cmds)
target_compile_options(zen_codegen PRIVATE $<$<CONFIG:Release>:-O3>)
target_compile_definitions(zen_codegen PRIVATE $<$<CONFIG:Release>:NDEBUG>)

add_executable(zencc zencc.cpp)
set_target_properties(zencc PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
target_include_directories(zencc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../libzen/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../libzen/third_party)
target_compile_options(zencc PRIVATE $<$<CONFIG:Release>:-O3>)
target_compile_definitions(zencc PRIVATE $<$<CONFIG:Release>:NDEBUG>)
target_link_libraries(zencc PRIVATE zen_codegen zen_static)
add_dependencies(zencc zen_native_cmds)
if(TARGET zenblitz)
    # Pass SDL2 include/library paths to the test harness so the generated
    # C++ (which includes zenblitzsdk.h -> SDL_rwops.h) can find and link SDL2.
    if(TARGET SDL2)
        get_target_property(_sdl2_inc SDL2 INTERFACE_INCLUDE_DIRECTORIES)
        get_target_property(_sdl2_loc SDL2 LOCATION)
        set(NATIVE_CXXFLAGS "-I${_sdl2_inc}" PARENT_SCOPE)
        set(NATIVE_LDFLAGS "${_sdl2_loc};-lm;-lpthread;-ldl" PARENT_SCOPE)
    endif()
    add_test(
        NAME zen_native_codegen_parity
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/../tests/native_compare.sh
                $<TARGET_FILE:zenblitz>
                $<TARGET_FILE:zencc>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/control_flow.bb
                ${CMAKE_CURRENT_SOURCE_DIR}/../tests/hello.bb
                ${CMAKE_CURRENT_SOURCE_DIR}/../tests/funcs.bb
                ${CMAKE_CURRENT_SOURCE_DIR}/../tests/types.bb
                ${CMAKE_CURRENT_SOURCE_DIR}/../tests/arrays.bb
                ${CMAKE_CURRENT_SOURCE_DIR}/../tests/control.bb
                ${CMAKE_CURRENT_SOURCE_DIR}/../tests/edge.bb
                ${CMAKE_CURRENT_SOURCE_DIR}/../tests/files_banks.bb)
    set_tests_properties(zen_native_codegen_parity PROPERTIES
        TIMEOUT 300
        LABELS "native;codegen")
endif()
