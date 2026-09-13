#include "engine/DynamicMesh.h"

namespace engine
{
    bool DynamicMesh::upload(gpu::Device &dev)
    {
        const int vertices = (int)mVertices.size();
        const int indices = (int)mIndices.size();
        if (!vertices || !indices) return false;

        if (vertices > mVertexCapacity || indices > mIndexCapacity)
        {
            if (mVertexBuffer.valid()) dev.destroy(mVertexBuffer);
            if (mIndexBuffer.valid()) dev.destroy(mIndexBuffer);
            mVertexCapacity = vertices + mVertexCapacity / 2 + 32;
            mIndexCapacity = indices + mIndexCapacity / 2 + 96;

            gpu::BufferDesc verticesDesc;
            verticesDesc.size = (std::uint64_t)mVertexCapacity * sizeof(Vertex);
            verticesDesc.usage = gpu::BufferUsageVertex;
            verticesDesc.debugName = "dynamic_mesh.vb";
            mVertexBuffer = dev.createBuffer(verticesDesc);

            gpu::BufferDesc indicesDesc;
            indicesDesc.size = (std::uint64_t)mIndexCapacity * sizeof(unsigned short);
            indicesDesc.usage = gpu::BufferUsageIndex;
            indicesDesc.debugName = "dynamic_mesh.ib";
            mIndexBuffer = dev.createBuffer(indicesDesc);
        }

        dev.updateBuffer(mVertexBuffer, 0, {mVertices.data(), mVertices.size() * sizeof(Vertex)});
        dev.updateBuffer(mIndexBuffer, 0, {mIndices.data(), mIndices.size() * sizeof(unsigned short)});
        return mVertexBuffer.valid() && mIndexBuffer.valid();
    }

    GpuGeometry DynamicMesh::geometry() const
    {
        GpuGeometry geometry;
        geometry.vb = mVertexBuffer;
        geometry.ib = mIndexBuffer;
        geometry.indexCount = (std::uint32_t)mIndices.size();
        geometry.layout = GpuGeometry::LayoutDynamic;
        return geometry;
    }

    void DynamicMesh::freeGpu(gpu::Device &dev)
    {
        if (mVertexBuffer.valid()) dev.destroy(mVertexBuffer);
        if (mIndexBuffer.valid()) dev.destroy(mIndexBuffer);
        mVertexBuffer = gpu::BufferHandle();
        mIndexBuffer = gpu::BufferHandle();
        mVertexCapacity = 0;
        mIndexCapacity = 0;
    }
}
