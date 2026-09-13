#ifndef ENGINE_TERRAINREP_H
#define ENGINE_TERRAINREP_H

#include "engine/DynamicMesh.h"
#include "engine/Frustum.h"
#include "engine/Model.h"
#include <ct/priority_queue.hpp>
#include <ct/pool.hpp>
#include <ct/vector.hpp>

namespace engine
{
    class TerrainRep
    {
    public:
        explicit TerrainRep(int cellShift);
        TerrainRep(const TerrainRep &other);

        void clear();
        void setShading(bool shading);
        void setDetail(int detail, bool morph);
        void setHeight(int x, int z, float height, bool realtime);

        int getSize() const;
        float getHeight(int x, int z) const;
        void render(Model *model, const RenderContext &context);
        bool collide(const blitz::Line &line, float radius, Collision *current, const Transform &transform) const;

        void freeGpu(gpu::Device &dev) { mMesh.freeGpu(dev); }

    private:
        struct Cell { unsigned char height = 0; };
        struct Error { unsigned char error = 0, bound = 0; };
        struct Vert
        {
            short x = 0, z = 0;
            Vector value;
            float sourceY = 0.0f;
        };
        struct Tri
        {
            int id = 0;
            short clip = 0, v0 = 0, v1 = 0, v2 = 0;
            Tri *e0 = nullptr, *e1 = nullptr, *e2 = nullptr;
            float projectedError = 0.0f;

            Tri() = default;
            Tri(int id, int clip, int v0, int v1, int v2, Tri *e0 = nullptr, Tri *e1 = nullptr, Tri *e2 = nullptr)
                : id(id), clip((short)clip), v0((short)v0), v1((short)v1), v2((short)v2), e0(e0), e1(e1), e2(e2) {}
            void unlink();
        };
        struct TriCompare
        {
            bool operator()(const Tri *a, const Tri *b) const { return a->projectedError < b->projectedError; }
        };

        using TriQueue = ct::PriorityQueue<Tri *, TriCompare>;

        ct::Vector<Cell> mCells;
        mutable ct::Vector<Error> mErrors;
        mutable bool mErrorsValid = true;
        int mCellSize = 0, mCellShift = 0, mCellMask = 0, mEndTriId = 0;
        int mDetail = 2000;
        bool mMorph = true, mShading = false;

        ct::Vector<Vert> mVertices;
        ct::Pool<Tri> mTriPool;
        TriQueue mQueue;
        ct::Vector<Tri *> mLeaves;
        DynamicMesh mMesh;

        Vert makeVert(int x, int z) const;
        void insert(Tri *tri, const Frustum &frustum, const Vector &eye);
        void split(Tri *tri, const Frustum &frustum, const Vector &eye);
        void clearWorking();
        Tri *createTri(int id, int clip, int v0, int v1, int v2, Tri *e0 = nullptr, Tri *e1 = nullptr, Tri *e2 = nullptr)
        {
            return mTriPool.create(id, clip, v0, v1, v2, e0, e1, e2);
        }
        void destroyTri(Tri *tri) { mTriPool.destroy(tri); }
        void validateErrors() const;
        Vector getNormal(int x, int z) const;
        Error calcError(int id, const Vert &v0, const Vert &v1, const Vert &v2) const;
        Error calcError(int id, int x, int z, const Vert &v0, const Vert &v1, const Vert &v2) const;
        bool collide(const blitz::Line &line, Collision *current, const Transform &transform, int id,
                     const Vert &v0, const Vert &v1, const Vert &v2, const blitz::Line &localLine) const;
        bool collide(const blitz::Line &line, float radius, Collision *current, const Transform &transform, int id,
                     const Vert &v0, const Vert &v1, const Vert &v2, const Box &localBox) const;
    };
}

#endif
