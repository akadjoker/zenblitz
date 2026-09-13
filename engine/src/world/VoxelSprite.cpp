#include "engine/VoxelSprite.h"
#include "engine/Frustum.h"

namespace engine
{
    namespace
    {
        static float absf(float value) { return value < 0.0f ? -value : value; }
    }

    VoxelSprite::VoxelSprite(int slices)
    {
        if (slices < 1) slices = 1;
        if (slices > 128) slices = 128;
        mSlices = slices;
        setBlend(BlendAlpha);
        setFX(FxFullbright | FxDoubleSided | FxNoFog);
        rebuild();
    }

    VoxelSprite::VoxelSprite(const VoxelSprite &other)
        : Model(other), mSlices(other.mSlices), mColumns(other.mColumns), mRows(other.mRows),
          mFirstFrame(other.mFirstFrame), mFrameCount(other.mFrameCount)
    {
        rebuild();
    }

    void VoxelSprite::setAtlas(int columns, int rows, int firstFrame, int frameCount)
    {
        if (columns < 1) columns = 1;
        if (rows < 1) rows = 1;
        const int total = columns * rows;
        if (firstFrame < 0) firstFrame = 0;
        if (firstFrame >= total) firstFrame = total - 1;
        if (frameCount < 1 || frameCount > total - firstFrame) frameCount = total - firstFrame;

        if (mColumns == columns && mRows == rows && mFirstFrame == firstFrame && mFrameCount == frameCount) return;
        mColumns = columns;
        mRows = rows;
        mFirstFrame = firstFrame;
        mFrameCount = frameCount;
        rebuild();
    }

    void VoxelSprite::rebuild()
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            buildAxis(axis, true);
            buildAxis(axis, false);
        }
    }

    void VoxelSprite::buildAxis(int axis, bool positiveEye)
    {
        DynamicMesh &mesh = mMeshes[axis * 2 + (positiveEye ? 0 : 1)];
        const int count = mSlices * 2 + 1;
        mesh.begin(count * 4, count * 2);

        const float invColumns = 1.0f / (float)mColumns;
        const float invRows = 1.0f / (float)mRows;
        const float insetU = invColumns * 0.001f;
        const float insetV = invRows * 0.001f;
        const int first = positiveEye ? 0 : count - 1;
        const int last = positiveEye ? count : -1;
        const int step = positiveEye ? 1 : -1;

        for (int slice = first; slice != last; slice += step)
        {
            const float p = (float)(slice - mSlices) / (float)mSlices;
            const int frame = mFirstFrame + (slice * mFrameCount) / count;
            const int column = frame % mColumns;
            const int row = frame / mColumns;
            const float u0 = (float)column * invColumns + insetU;
            const float v0 = (float)row * invRows + insetV;
            const float u1 = (float)(column + 1) * invColumns - insetU;
            const float v1 = (float)(row + 1) * invRows - insetV;

            Vector points[4];
            Vector normal;
            if (axis == 0)
            {
                points[0] = Vector(p, -1, -1); points[1] = Vector(p, 1, -1);
                points[2] = Vector(p, 1, 1); points[3] = Vector(p, -1, 1);
                normal = Vector(positiveEye ? 1.0f : -1.0f, 0, 0);
            }
            else if (axis == 1)
            {
                points[0] = Vector(-1, p, -1); points[1] = Vector(1, p, -1);
                points[2] = Vector(1, p, 1); points[3] = Vector(-1, p, 1);
                normal = Vector(0, positiveEye ? 1.0f : -1.0f, 0);
            }
            else
            {
                points[0] = Vector(-1, -1, p); points[1] = Vector(1, -1, p);
                points[2] = Vector(1, 1, p); points[3] = Vector(-1, 1, p);
                normal = Vector(0, 0, positiveEye ? 1.0f : -1.0f);
            }

            const int base = mesh.vertexCount();
            const float uvs[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
            for (int vertex = 0; vertex < 4; ++vertex)
            {
                DynamicMesh::Vertex v;
                v.coords = points[vertex];
                v.normal = normal;
                v.texCoords[0][0] = uvs[vertex][0];
                v.texCoords[0][1] = uvs[vertex][1];
                mesh.addVertex(v);
            }
            mesh.addTriangle((unsigned short)base, (unsigned short)(base + 1), (unsigned short)(base + 2));
            mesh.addTriangle((unsigned short)base, (unsigned short)(base + 2), (unsigned short)(base + 3));
        }
    }

    bool VoxelSprite::render(const RenderContext &rc)
    {
        const Transform &world = getRenderTform();
        Vector corners[8];
        int corner = 0;
        for (int z = -1; z <= 1; z += 2)
            for (int y = -1; y <= 1; y += 2)
                for (int x = -1; x <= 1; x += 2)
                    corners[corner++] = world * Vector((float)x, (float)y, (float)z);
        if (!rc.getWorldFrustum().cull(corners, 8)) return false;

        Vector eye = (-world) * rc.getCameraTform().v;
        const float x = absf(eye.x), y = absf(eye.y), z = absf(eye.z);
        int axis = 0;
        float sign = eye.x;
        if (y > x && y >= z) { axis = 1; sign = eye.y; }
        else if (z > x && z > y) { axis = 2; sign = eye.z; }
        enqueue(&mMeshes[axis * 2 + (sign >= 0.0f ? 0 : 1)], getRenderBrush());
        return true;
    }

    void VoxelSprite::uploadQueue(gpu::Device &dev, int type)
    {
        ct::Vector<QueueEntry> &entries = queue(type);
        for (size_t index = 0; index < entries.size(); ++index)
        {
            DynamicMesh *mesh = entries[index].dynamicMesh;
            if (mesh && mesh->upload(dev)) entries[index].geom = mesh->geometry();
        }
    }

    void VoxelSprite::freeGpu(gpu::Device &dev)
    {
        for (int index = 0; index < 6; ++index) mMeshes[index].freeGpu(dev);
    }
}
