#ifndef ENGINE_DYNAMICMESH_H
#define ENGINE_DYNAMICMESH_H

#include "engine/GpuGeometry.h"
#include "engine/Geom.h"
#include "gpu/GPU.h"
#include <ct/vector.hpp>

namespace engine
{
    class DynamicMesh
    {
    public:
        struct Vertex
        {
            blitz::Vector coords;
            blitz::Vector normal;
            unsigned color = ~0u;
            float texCoords[2][2] = {{0, 0}, {0, 0}};
        };

        void begin(int vertices, int triangles)
        {
            if (vertices > 0) mVertices.reserve((size_t)vertices);
            if (triangles > 0) mIndices.reserve((size_t)triangles * 3);
            mVertices.clear();
            mIndices.clear();
        }

        int addVertex(const Vertex &vertex) { mVertices.push_back(vertex); return (int)mVertices.size() - 1; }
        void addTriangle(unsigned short a, unsigned short b, unsigned short c)
        {
            mIndices.push_back(a);
            mIndices.push_back(b);
            mIndices.push_back(c);
        }

        int vertexCount() const { return (int)mVertices.size(); }
        int triangleCount() const { return (int)mIndices.size() / 3; }
        bool upload(gpu::Device &dev);
        GpuGeometry geometry() const;
        void freeGpu(gpu::Device &dev);

    private:
        ct::Vector<Vertex> mVertices;
        ct::Vector<unsigned short> mIndices;
        int mVertexCapacity = 0, mIndexCapacity = 0;
        gpu::BufferHandle mVertexBuffer;
        gpu::BufferHandle mIndexBuffer;
    };
}

#endif
