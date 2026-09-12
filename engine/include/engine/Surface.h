#ifndef ENGINE_SURFACE_H
#define ENGINE_SURFACE_H

#include "engine/Brush.h"
#include "gpu/GPU.h"
#include <ct/vector.hpp>
#include <string>

namespace engine
{
    using blitz::Transform;

    static constexpr int kMaxSurfaceBones = 4;

    class Surface
    {
    public:
        struct Vertex
        {
            Vector coords;
            Vector normal;
            unsigned color = ~0u;
            float texCoords[2][2] = {{0, 0}, {0, 0}};
            unsigned char boneBones[kMaxSurfaceBones] = {255, 0, 0, 0};
            float boneWeights[kMaxSurfaceBones] = {0, 0, 0, 0};
        };

        struct Bone
        {
            Transform coordTform;
            blitz::Matrix normalTform;
        };

        struct Triangle
        {
            unsigned short verts[3];
        };

        Surface() {}

        void setName(const std::string &t) { mName = t; }
        void setBrush(const Brush &b) { mBrush = b; }

        void clear(bool verts, bool tris)
        {
            if (verts) { mVertices.clear(); mValidVs = 0; }
            if (tris) { mTriangles.clear(); mValidTs = 0; }
        }

        int addVertex(const Vertex &v) { mVertices.push_back(v); return (int)mVertices.size() - 1; }
        void setVertex(int n, const Vertex &v) { mVertices[n] = v; if (n < mValidVs) mValidVs = n; }
        void setCoords(int n, const Vector &v) { mVertices[n].coords = v; if (n < mValidVs) mValidVs = n; }
        void setNormal(int n, const Vector &v) { mVertices[n].normal = v; if (n < mValidVs) mValidVs = n; }
        void setColor(int n, unsigned argb) { mVertices[n].color = argb; if (n < mValidVs) mValidVs = n; }
        void setColor(int n, const Vector &v);
        Vector getColor(int n) const;
        void setTexCoords(int n, float u, float v, int set)
        {
            mVertices[n].texCoords[set][0] = u;
            mVertices[n].texCoords[set][1] = v;
            if (n < mValidVs) mValidVs = n;
        }

        int addTriangle(const Triangle &t) { mTriangles.push_back(t); return (int)mTriangles.size() - 1; }
        void setTriangle(int n, const Triangle &t) { mTriangles[n] = t; if (n < mValidTs) mValidTs = n; }

        void updateNormals();

        std::string getName() const { return mName; }
        const Brush &getBrush() const { return mBrush; }
        int numVertices() const { return (int)mVertices.size(); }
        int numTriangles() const { return (int)mTriangles.size(); }
        const Vertex &getVertex(int n) const { return mVertices[n]; }
        const Triangle &getTriangle(int n) const { return mTriangles[n]; }

        bool ensureGpu(gpu::Device &dev);
        bool ensureGpuSkinned(gpu::Device &dev, const ct::Vector<Bone> &bones);
        gpu::BufferHandle vertexBuffer() const { return mVertexBuffer; }
        gpu::BufferHandle indexBuffer() const { return mIndexBuffer; }
        int gpuIndexCount() const { return mValidTs * 3; }
        void freeGpu(gpu::Device &dev);

    private:
        Brush mBrush;
        std::string mName;
        ct::Vector<Vertex> mVertices;
        ct::Vector<Triangle> mTriangles;
        int mMeshVs = 0, mMeshTs = 0;
        int mValidVs = 0, mValidTs = 0;

        gpu::BufferHandle mVertexBuffer;
        gpu::BufferHandle mIndexBuffer;
    };
}

#endif
