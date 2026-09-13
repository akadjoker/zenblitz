cmake_minimum_required(VERSION 3.21)

# zenblitz3d's engine: window/input (Platform) plus Device/Graphics/Batch/
# Log/Profiler (originally copied from Kinetix3d, now folded into
# namespace engine). Geom/Matrix4 are Blitz3D's own math (see their
# headers). Pixmap/Dds (originally from ZenGL, MIT stb_image underneath,
# now folded into namespace engine) are the CPU-side image buffer for
# LoadImage/LockBuffer/MaskImage.
add_library(zenblitz_engine STATIC
    src/core/Device.cpp
    src/core/Log.cpp
    src/core/BlitzKeys.cpp
    src/core/Platform.cpp
    src/world/DynamicBVH.cpp
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
    src/world/DynamicMesh.cpp
    src/world/MeshCollider.cpp
    src/world/Animator.cpp
    src/world/MeshModel.cpp
    src/world/PlaneModel.cpp
    src/world/Terrain.cpp
    src/world/TerrainRep.cpp
    src/world/ParticleEmitter.cpp
    src/world/CSG.cpp
    src/world/VoxelSprite.cpp
    src/world/Sprite.cpp
    src/world/MeshRenderer.cpp
    src/world/World.cpp
    src/world/MeshUtil.cpp
    src/world/MeshLoader.cpp
    src/world/LoaderB3D.cpp
    src/world/LoaderB3DS.cpp
    src/world/LoaderX.cpp
    third_party/xfile/xfile_parse.c
    third_party/xfile/xfile_mszip.c
    src/world/LoaderGltf.cpp
    src/world/LoaderFbx.cpp
    third_party/ofbx/ofbx.cpp
    third_party/miniz/miniz.c
    src/world/FilePath.cpp
    src/world/Md2Norms.cpp
    src/world/MD2Rep.cpp
    src/world/MD2Model.cpp
    src/world/Texture.cpp
    src/world/TextureCache.cpp
    src/audio/AudioImpl.cpp
    src/audio/Sound.cpp
)

target_include_directories(zenblitz_engine PUBLIC
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/../libzen/third_party
)
target_include_directories(zenblitz_engine PRIVATE third_party/stb third_party/miniaudio third_party/xfile third_party/cgltf third_party/ofbx third_party/miniz)

# gpu_sdl (GPUSDLWindow, which engine::Device wraps) links SDL2 and the
# umbrella `gpu` target itself; nothing else to add here.
target_link_libraries(zenblitz_engine PUBLIC gpu_sdl)
target_compile_options(zenblitz_engine PRIVATE $<$<CONFIG:Release>:-O3>)
