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