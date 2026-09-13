#ifndef ENGINE_SURFACE_H
#define ENGINE_SURFACE_H

#include "engine/Brush.h"
#include "engine/GpuGeometry.h"
#include "gpu/GPU.h"
#include <ct/vector.hpp>
#include <ct/string.hpp>

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

        /* surface.h's Monitor: the owning mesh's change counters, which
           the surface bumps whenever it changes geometry. MeshModel's
           cached bounding box, vertex normals and collider are all
           validated against geomChanges, so an edit that does not bump
           it leaves them stale - a mesh grown with AddVertex/
           VertexCoords after its first render would keep culling and
           colliding against its original bounds. Only the calls that
           move or add geometry bump it, exactly as the original did:
           setNormal/setColor/setTexCoords deliberately do not. */
        struct Monitor
        {
            int brushChanges = 0, geomChanges = 0;
        };

        Surface() {}
        explicit Surface(Monitor *mon) : mMon(mon) {}

        void setName(const ct::String &t) { mName = t; }
        void setBrush(const Brush &b) { mBrush = b; }

        void clear(bool verts, bool tris)
        {
            if (verts) { mVertices.clear(); mValidVs = 0; }
            if (tris) { mTriangles.clear(); mValidTs = 0; }
            if (verts || tris) touchGeom();
        }
        int addVertex(const Vertex &v) { mVertices.push_back(v); touchGeom(); return (int)mVertices.size() - 1; }
        void setVertex(int n, const Vertex &v) { mVertices[n] = v; if (n < mValidVs) mValidVs = n; touchGeom(); }
        void setCoords(int n, const Vector &v) { mVertices[n].coords = v; if (n < mValidVs) mValidVs = n; touchGeom(); }
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

        int addTriangle(const Triangle &t) { mTriangles.push_back(t); touchGeom(); return (int)mTriangles.size() - 1; }
        void setTriangle(int n, const Triangle &t) { mTriangles[n] = t; if (n < mValidTs) mValidTs = n; touchGeom(); }

        void updateNormals();

        ct::String getName() const { return mName; }
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

        // what to draw after ensureGpu*() - valid only after an upload
        GpuGeometry geometry() const
        {
            GpuGeometry g;
            g.vb = mVertexBuffer;
            g.ib = mIndexBuffer;
            g.indexCount = (std::uint32_t)gpuIndexCount();
            g.layout = GpuGeometry::LayoutSurface;
            return g;
        }

    private:
        // Null for a surface built outside a mesh (nothing is caching
        // bounds off it, so there is nothing to invalidate).
        void touchGeom() { if (mMon) ++mMon->geomChanges; }

        Monitor *mMon = nullptr;
        // Which device mVertexBuffer/mIndexBuffer belong to. Handles are
        // only meaningful to their own device, so after EndGraphics +
        // Graphics3D these have to be rebuilt rather than updated - see
        // ensureGpu().
        gpu::Device *mGpuOwner = nullptr;
        Brush mBrush;
        ct::String mName;
        ct::Vector<Vertex> mVertices;
        ct::Vector<Triangle> mTriangles;
        int mMeshVs = 0, mMeshTs = 0;
        int mValidVs = 0, mValidTs = 0;

        gpu::BufferHandle mVertexBuffer;
        gpu::BufferHandle mIndexBuffer;
        // CPU-skinning output, kept between frames so ensureGpuSkinned
        // doesn't allocate a full vertex array every call
        ct::Vector<Vertex> mSkinScratch;
    };
}

#endif
