#include "engine/MD2Rep.h"
#include "engine/Md2Norms.h"
#include <ct/hashmap.hpp>
#include <cstdio>
#include <cstdint>
#include <cstring>

namespace engine
{
    namespace
    {
#pragma pack(push, 1)
        struct Md2Header
        {
            std::int32_t magic, version, skinWidth, skinHeight, frameSize;
            std::int32_t numSkins, numVertices, numTexCoords, numTriangles, numGlCommands, numFrames;
            std::int32_t offsetSkins, offsetTexCoords, offsetTriangles, offsetFrames, offsetGlCommands, offsetEnd;
        };
        struct Md2FileUv { std::int16_t u, v; };
        struct Md2FileVert { unsigned char x, y, z, n; };
        struct Md2FileTri { std::uint16_t verts[3], uvs[3]; };
#pragma pack(pop)

        // "IDP2" on disk, read little-endian into an int
        const std::int32_t kMd2Magic = ((std::int32_t)'2' << 24) | ((std::int32_t)'P' << 16) | ((std::int32_t)'D' << 8) | (std::int32_t)'I';

        bool readAt(FILE *in, long offset, void *buf, size_t bytes)
        {
            if (fseek(in, offset, SEEK_SET) != 0) return false;
            return fread(buf, 1, bytes, in) == bytes;
        }
    }

    MD2Rep::MD2Rep(const std::string &f)
    {
        FILE *in = fopen(f.c_str(), "rb");
        if (!in) return;

        Md2Header header;
        if (fread(&header, 1, sizeof(header), in) != sizeof(header) || header.magic != kMd2Magic || header.version != 8 ||
            header.numVertices <= 0 || header.numTriangles <= 0 || header.numFrames <= 0 || header.numTexCoords <= 0)
        {
            fclose(in);
            return;
        }

        ct::Vector<Md2FileUv> fileUvs;
        fileUvs.resize(header.numTexCoords);
        ct::Vector<Md2FileTri> fileTris;
        fileTris.resize(header.numTriangles);
        if (!readAt(in, header.offsetTexCoords, fileUvs.data(), fileUvs.size() * sizeof(Md2FileUv)) ||
            !readAt(in, header.offsetTriangles, fileTris.data(), fileTris.size() * sizeof(Md2FileTri)))
        {
            fclose(in);
            return;
        }

        // an MD2 vertex is shared by triangles with different uvs; split
        // into (vertex, uv) pairs like the original's map<t_vert,int>,
        // keyed as one int for an O(1) lookup
        ct::HashMap<std::uint32_t, int> pairMap;
        ct::Vector<std::uint16_t> pairVertex;
        mTris = header.numTriangles;
        mIndices.resize((size_t)mTris * 3);
        for (int k = 0; k < mTris; ++k)
        {
            std::uint16_t tri[3];
            for (int j = 0; j < 3; ++j)
            {
                std::uint32_t key = ((std::uint32_t)fileTris[k].verts[j] << 16) | fileTris[k].uvs[j];
                int *found = pairMap.find(key);
                if (found)
                {
                    tri[j] = (std::uint16_t)*found;
                    continue;
                }
                int id = (int)pairVertex.size();
                pairMap.put(key, id);
                pairVertex.push_back(fileTris[k].verts[j]);
                Md2Uv uv;
                const Md2FileUv &fuv = fileUvs[fileTris[k].uvs[j]];
                uv.u = fuv.u / (float)header.skinWidth;
                uv.v = fuv.v / (float)header.skinHeight;
                mUvs.push_back(uv);
                tri[j] = (std::uint16_t)id;
            }
            // the original's setTriangle(k, v0, v2, v1) winding swap
            mIndices[(size_t)k * 3 + 0] = tri[0];
            mIndices[(size_t)k * 3 + 1] = tri[2];
            mIndices[(size_t)k * 3 + 2] = tri[1];
        }
        mVertCount = (int)pairVertex.size();
        mFrames = header.numFrames;

        mVerts.resize((size_t)mFrames * mVertCount);
        ct::Vector<Md2FileVert> fileVerts;
        fileVerts.resize(header.numVertices);

        if (fseek(in, header.offsetFrames, SEEK_SET) != 0) { mFrames = 0; fclose(in); return; }
        for (int k = 0; k < mFrames; ++k)
        {
            float scale[3], trans[3];
            char name[16];
            if (fread(scale, 1, 12, in) != 12 || fread(trans, 1, 12, in) != 12 || fread(name, 1, 16, in) != 16 ||
                fread(fileVerts.data(), 1, fileVerts.size() * sizeof(Md2FileVert), in) != fileVerts.size() * sizeof(Md2FileVert))
            {
                mFrames = 0;
                fclose(in);
                return;
            }
            // Quake's axes -> Blitz's: (x,y,z) file = (z,x,y) engine, applied
            // to scale/translate and to each byte vertex, as the original did
            Md2Vert *out = &mVerts[(size_t)k * mVertCount];
            for (int j = 0; j < mVertCount; ++j)
            {
                const Md2FileVert &mv = fileVerts[pairVertex[j]];
                out[j].x = mv.y * scale[1] + trans[1];
                out[j].y = mv.z * scale[2] + trans[2];
                out[j].z = mv.x * scale[0] + trans[0];
                const float *n = kMd2Norms[mv.n < 162 ? mv.n : 0];
                out[j].nx = n[1];
                out[j].ny = n[2];
                out[j].nz = n[0];
                mBox.update(blitz::Vector(out[j].x, out[j].y, out[j].z));
            }
        }
        fclose(in);
    }

    MD2Rep::~MD2Rep() {}

    bool MD2Rep::ensureGpu(gpu::Device &dev)
    {
        if (mVb.valid()) return true;
        if (!valid()) return false;

        gpu::BufferDesc vb;
        vb.size = mVerts.size() * sizeof(Md2Vert);
        vb.usage = gpu::BufferUsageVertex;
        vb.debugName = "md2.frames";
        mVb = dev.createBuffer(vb);

        gpu::BufferDesc uvb;
        uvb.size = mUvs.size() * sizeof(Md2Uv);
        uvb.usage = gpu::BufferUsageVertex;
        uvb.debugName = "md2.uv";
        mUvb = dev.createBuffer(uvb);

        gpu::BufferDesc ib;
        ib.size = mIndices.size() * sizeof(std::uint16_t);
        ib.usage = gpu::BufferUsageIndex;
        ib.debugName = "md2.ib";
        mIb = dev.createBuffer(ib);

        if (!mVb.valid() || !mUvb.valid() || !mIb.valid()) return false;
        dev.updateBuffer(mVb, 0, {mVerts.data(), mVerts.size() * sizeof(Md2Vert)});
        dev.updateBuffer(mUvb, 0, {mUvs.data(), mUvs.size() * sizeof(Md2Uv)});
        dev.updateBuffer(mIb, 0, {mIndices.data(), mIndices.size() * sizeof(std::uint16_t)});
        return true;
    }

    void MD2Rep::freeGpu(gpu::Device &dev)
    {
        if (mVb.valid()) { dev.destroy(mVb); mVb = gpu::BufferHandle(); }
        if (mUvb.valid()) { dev.destroy(mUvb); mUvb = gpu::BufferHandle(); }
        if (mIb.valid()) { dev.destroy(mIb); mIb = gpu::BufferHandle(); }
    }

    GpuGeometry MD2Rep::geometry(int frameA, int frameB, float t) const
    {
        GpuGeometry g;
        g.layout = GpuGeometry::LayoutMd2Morph;
        g.vb = mVb;
        g.vbOffset = (std::uint64_t)frameA * mVertCount * sizeof(Md2Vert);
        g.vb2 = mVb;
        g.vb2Offset = (std::uint64_t)frameB * mVertCount * sizeof(Md2Vert);
        g.uvb = mUvb;
        g.ib = mIb;
        g.indexCount = (std::uint32_t)mIndices.size();
        g.morph = t;
        return g;
    }

    GpuGeometry MD2Rep::geometryFrom(gpu::BufferHandle vbA, int frameB, float t) const
    {
        GpuGeometry g = geometry(0, frameB, t);
        g.vb = vbA;
        g.vbOffset = 0;
        return g;
    }
}
