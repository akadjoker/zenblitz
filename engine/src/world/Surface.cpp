#include "engine/Surface.h"
#include <cmath>

namespace engine
{
    void Surface::setColor(int n, const Vector &v)
    {
        int r = (int)std::floor(v.x * 255); if (r < 0) r = 0; else if (r > 255) r = 255;
        int g = (int)std::floor(v.y * 255); if (g < 0) g = 0; else if (g > 255) g = 255;
        int b = (int)std::floor(v.z * 255); if (b < 0) b = 0; else if (b > 255) b = 255;
        unsigned argb = 0xff000000u | ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
        mVertices[n].color = argb;
        if (n < mValidVs) mValidVs = n;
    }

    Vector Surface::getColor(int n) const
    {
        const unsigned c = mVertices[n].color;
        float r = (float)((c & 0x00ff0000u) >> 16);
        float g = (float)((c & 0x0000ff00u) >> 8);
        float b = (float)(c & 0x000000ffu);
        return Vector(r / 255.0f, g / 255.0f, b / 255.0f);
    }

    void Surface::updateNormals()
    {
        ct::Vector<Vector> accum(mVertices.size());
        for (size_t k = 0; k < mVertices.size(); ++k) accum.push_back(Vector());

        for (size_t k = 0; k < mTriangles.size(); ++k)
        {
            const Triangle &t = mTriangles[k];
            const Vector &v0 = mVertices[t.verts[0]].coords;
            const Vector &v1 = mVertices[t.verts[1]].coords;
            const Vector &v2 = mVertices[t.verts[2]].coords;
            Vector n = (v1 - v0).cross(v2 - v0);
            if (n.length() <= blitz::EPSILON) continue;
            n.normalize();
            for (int i = 0; i < 3; ++i)
            {
                for (size_t j = 0; j < mVertices.size(); ++j)
                {
                    if (mVertices[j].coords == mVertices[t.verts[i]].coords)
                        accum[(int)j] += n;
                }
            }
        }
        for (size_t k = 0; k < mVertices.size(); ++k)
            mVertices[k].normal = accum[(int)k].normalized();
    }

    bool Surface::ensureGpu(gpu::Device &dev)
    {
        if (mValidVs == (int)mVertices.size() && mValidTs == (int)mTriangles.size() && mVertexBuffer.valid())
            return true;

        if (mMeshVs < (int)mVertices.size() || mMeshTs < (int)mTriangles.size())
        {
            if (mVertexBuffer.valid()) dev.destroy(mVertexBuffer);
            if (mIndexBuffer.valid()) dev.destroy(mIndexBuffer);
            mMeshVs = (int)mVertices.size() + mMeshVs / 2;
            mMeshTs = (int)mTriangles.size() + mMeshTs / 2;

            gpu::BufferDesc vbDesc;
            vbDesc.size = (std::uint64_t)mMeshVs * sizeof(Vertex);
            vbDesc.usage = gpu::BufferUsageVertex;
            vbDesc.debugName = "surface.vb";
            mVertexBuffer = dev.createBuffer(vbDesc);

            gpu::BufferDesc ibDesc;
            ibDesc.size = (std::uint64_t)mMeshTs * 3 * sizeof(std::uint16_t);
            ibDesc.usage = gpu::BufferUsageIndex;
            ibDesc.debugName = "surface.ib";
            mIndexBuffer = dev.createBuffer(ibDesc);

            mValidVs = 0;
            mValidTs = 0;
        }

        if (mValidVs < (int)mVertices.size())
        {
            dev.updateBuffer(mVertexBuffer, (std::uint64_t)mValidVs * sizeof(Vertex),
                              {&mVertices[mValidVs], (mVertices.size() - mValidVs) * sizeof(Vertex)});
            mValidVs = (int)mVertices.size();
        }
        if (mValidTs < (int)mTriangles.size())
        {
            dev.updateBuffer(mIndexBuffer, (std::uint64_t)mValidTs * 3 * sizeof(std::uint16_t),
                              {&mTriangles[mValidTs], (mTriangles.size() - mValidTs) * sizeof(Triangle)});
            mValidTs = (int)mTriangles.size();
        }
        return mVertexBuffer.valid() && mIndexBuffer.valid();
    }

    bool Surface::ensureGpuSkinned(gpu::Device &dev, const ct::Vector<Bone> &bones)
    {
        mValidVs = 0;
        mValidTs = 0;

        if (mMeshVs < (int)mVertices.size() || mMeshTs < (int)mTriangles.size())
        {
            if (mVertexBuffer.valid()) dev.destroy(mVertexBuffer);
            if (mIndexBuffer.valid()) dev.destroy(mIndexBuffer);
            mMeshVs = (int)mVertices.size();
            mMeshTs = (int)mTriangles.size();

            gpu::BufferDesc vbDesc;
            vbDesc.size = (std::uint64_t)mMeshVs * sizeof(Vertex);
            vbDesc.usage = gpu::BufferUsageVertex;
            vbDesc.debugName = "surface.vb.skinned";
            mVertexBuffer = dev.createBuffer(vbDesc);

            gpu::BufferDesc ibDesc;
            ibDesc.size = (std::uint64_t)mMeshTs * 3 * sizeof(std::uint16_t);
            ibDesc.usage = gpu::BufferUsageIndex;
            ibDesc.debugName = "surface.ib.skinned";
            mIndexBuffer = dev.createBuffer(ibDesc);
        }

        ct::Vector<Vertex> skinned;
        skinned.reserve(mVertices.size());
        for (size_t k = 0; k < mVertices.size(); ++k)
        {
            const Vertex &v = mVertices[k];
            Vertex out = v;
            if (v.boneBones[0] == 255)
            {
                const Bone &bone = bones[0];
                out.coords = bone.coordTform * v.coords;
                out.normal = bone.normalTform * v.normal;
            }
            else if (v.boneBones[1] == 255)
            {
                const Bone &bone = bones[v.boneBones[0]];
                out.coords = bone.coordTform * v.coords;
                out.normal = bone.normalTform * v.normal;
            }
            else
            {
                Vector tv, tn;
                for (int n = 0; n < kMaxSurfaceBones; ++n)
                {
                    if (v.boneBones[n] == 255) break;
                    const Bone &bone = bones[v.boneBones[n]];
                    tv += bone.coordTform * v.coords * v.boneWeights[n];
                    tn += bone.normalTform * v.normal * v.boneWeights[n];
                }
                out.coords = tv;
                out.normal = tn.normalized();
            }
            skinned.push_back(out);
        }

        dev.updateBuffer(mVertexBuffer, 0, {skinned.data(), skinned.size() * sizeof(Vertex)});
        dev.updateBuffer(mIndexBuffer, 0, {mTriangles.data(), mTriangles.size() * sizeof(Triangle)});
        mValidVs = (int)mVertices.size();
        mValidTs = (int)mTriangles.size();
        return mVertexBuffer.valid() && mIndexBuffer.valid();
    }

    void Surface::freeGpu(gpu::Device &dev)
    {
        if (mVertexBuffer.valid()) { dev.destroy(mVertexBuffer); mVertexBuffer = gpu::BufferHandle(); }
        if (mIndexBuffer.valid()) { dev.destroy(mIndexBuffer); mIndexBuffer = gpu::BufferHandle(); }
        mMeshVs = mMeshTs = mValidVs = mValidTs = 0;
    }
}
