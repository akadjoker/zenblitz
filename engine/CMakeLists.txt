cmake_minimum_required(VERSION 3.21)

# zenblitz3d's engine: window/input (Platform) plus, copied from Kinetix3d,
# Device/Graphics/Batch/Input/Log (files are kx:: unrenamed so they stay a
# plain diff against the source; Marco 1 only needs Platform, Device,
# Graphics, Batch, Input, Log, BlitzKeys).
add_library(zenblitz_engine STATIC
    src/core/Device.cpp
    src/core/Input.cpp
    src/core/Log.cpp
    src/core/BlitzKeys.cpp
    src/core/Platform.cpp
    src/core/Profiler.cpp
    src/render/Graphics.cpp
    src/render/Batch.cpp
)

target_include_directories(zenblitz_engine PUBLIC
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/../libzen/third_party
)

# gpu_sdl (GPUSDLWindow, which engine::Device wraps) links SDL2 and the
# umbrella `gpu` target itself; nothing else to add here.
target_link_libraries(zenblitz_engine PUBLIC gpu_sdl mathc)
target_compile_options(zenblitz_engine PRIVATE $<$<CONFIG:Release>:-O3>)
