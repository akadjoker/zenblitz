cmake_minimum_required(VERSION 3.10)
project(libzen LANGUAGES CXX)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Core sources: VM, GC, emitter, bytecode, Blitz compiler and runtime
file(GLOB CORE_SRCS CONFIGURE_DEPENDS src/*.cpp)

add_library(zen_static STATIC ${CORE_SRCS})
target_include_directories(zen_static PUBLIC include include/zen PRIVATE src third_party)
target_compile_options(zen_static PRIVATE $<$<CONFIG:Release>:-O3>)
target_compile_definitions(zen_static PRIVATE $<$<CONFIG:Release>:NDEBUG>)
set_target_properties(zen_static PROPERTIES OUTPUT_NAME zen POSITION_INDEPENDENT_CODE ON)

add_library(zen_shared SHARED ${CORE_SRCS})
target_include_directories(zen_shared PUBLIC include include/zen PRIVATE src third_party)
target_compile_options(zen_shared PRIVATE $<$<CONFIG:Release>:-O3>)
target_compile_definitions(zen_shared PRIVATE $<$<CONFIG:Release>:NDEBUG>)
set_target_properties(zen_shared PROPERTIES OUTPUT_NAME zen)
