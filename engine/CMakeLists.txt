cmake_minimum_required(VERSION 3.21)

# zenblitz3d's engine: window/input (Platform) plus, copied from Kinetix3d,
# Device/Graphics/Batch/Log (kx:: unrenamed so they stay a plain diff
# against the source). Geom/Matrix4 are Blitz3D's own math (see their
# headers). Pixmap/Dds (from ZenGL, MIT stb_image underneath) are the
# CPU-side image buffer for LoadImage/LockBuffer/MaskImage.
add_library(zenblitz_engine STATIC
    src/core/Device.cpp
    src/core/Log.cpp
    src/core/BlitzKeys.cpp
    src/core/Platform.cpp
    src/core/Profiler.cpp
    src/core/Geom.cpp
    src/render/Graphics.cpp
    src/render/Batch.cpp
    src/pixmap/Pixmap.cpp
    src/pixmap/Dds.cpp
    src/pixmap/StbImpl.cpp
    src/pixmap/Image.cpp
    src/world/Entity.cpp
    src/world/Object.cpp
    src/world/Collision.cpp
    src/world/Frustum.cpp
    src/world/Camera.cpp
    src/world/Surface.cpp
    src/world/MeshCollider.cpp
    src/world/Animator.cpp
    src/world/MeshModel.cpp
)

target_include_directories(zenblitz_engine PUBLIC
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/../libzen/third_party
)
target_include_directories(zenblitz_engine PRIVATE third_party/stb)

# gpu_sdl (GPUSDLWindow, which engine::Device wraps) links SDL2 and the
# umbrella `gpu` target itself; nothing else to add here.
target_link_libraries(zenblitz_engine PUBLIC gpu_sdl)
target_compile_options(zenblitz_engine PRIVATE $<$<CONFIG:Release>:-O3>)
