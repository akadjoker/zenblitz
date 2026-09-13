#ifndef ENGINE_MD2REP_H
#define ENGINE_MD2REP_H

#include "engine/GpuGeometry.h"
#include "engine/Geom.h"
#include <ct/vector.hpp>
#include <ct/string.hpp>

namespace engine
{
    using blitz::Box;

    // An MD2 file decoded once: every frame's vertices as floats, back to
    // back in one array (frame k at k*numVertices), plus shared uvs and
    // indices. Immutable after load, so MD2Model copies share one Rep.
    class MD2Rep
    {
    public:
        MD2Rep(const ct::String &f);
        ~MD2Rep();

        bool valid() const { return mFrames > 0; }
        int numFrames() const { return mFrames; }
        int numVertices() const { return mVertCount; }
        const Box &getBox() const { return mBox; }

        const Md2Vert *frameVerts(int frame) const { return &mVerts[(size_t)frame * mVertCount]; }

        // uploads the frame/uv/index buffers once - no render pass open
        bool ensureGpu(gpu::Device &dev);
        void freeGpu(gpu::Device &dev);

        // frames A and B selected by offset into the one static buffer
        GpuGeometry geometry(int frameA, int frameB, float t) const;
        // stream A from an external buffer (a captured transition pose)
        GpuGeometry geometryFrom(gpu::BufferHandle vbA, int frameB, float t) const;

        int refCount = 1;

    private:
        Box mBox;
        int mFrames = 0, mVertCount = 0, mTris = 0;
        ct::Vector<Md2Vert> mVerts;
        ct::Vector<Md2Uv> mUvs;
        ct::Vector<unsigned short> mIndices;

        gpu::BufferHandle mVb, mUvb, mIb;
    };
}

#endif
