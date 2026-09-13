#ifndef ENGINE_GPUGEOMETRY_H
#define ENGINE_GPUGEOMETRY_H

#include "gpu/GPU.h"
#include <cstdint>

namespace engine
{
    // What a draw call actually binds: GPU buffers, offsets and the vertex
    // layout they use. Surface, MD2 and (later) terrain/sprites each
    // produce one of these; the render queue and MeshRenderer only ever
    // see this, never the CPU-side owner.
    struct GpuGeometry
    {
        enum Layout : std::uint8_t
        {
            LayoutSurface = 0,  // one stream of Surface::Vertex
            LayoutMd2Morph = 1, // stream 0/1 = Md2Vert frames A/B, stream 2 = uv
            LayoutDynamic = 2,
        };

        gpu::BufferHandle vb;
        std::uint64_t vbOffset = 0;
        gpu::BufferHandle vb2;
        std::uint64_t vb2Offset = 0;
        gpu::BufferHandle uvb;
        gpu::BufferHandle ib;
        std::uint32_t indexCount = 0;
        Layout layout = LayoutSurface;
        float morph = 0.0f;

        bool valid() const { return vb.valid() && ib.valid() && indexCount > 0; }
    };

    // Vertex of an MD2 frame stream (positions and normals decoded once
    // at load, all frames back to back in one buffer)
    struct Md2Vert
    {
        float x, y, z;
        float nx, ny, nz;
    };
    struct Md2Uv
    {
        float u, v;
    };
}

#endif
