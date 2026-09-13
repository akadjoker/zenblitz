#ifndef ENGINE_MESHMODEL_H
#define ENGINE_MESHMODEL_H

#include "engine/Model.h"
#include "engine/MeshCollider.h"
#include "engine/RenderContext.h"
#include "engine/MeshRenderer.h"
#include <ct/vector.hpp>
#include <ct/hashmap.hpp>

namespace engine
{
    class MeshModel : public Model
    {
    public:
        using SurfaceList = ct::Vector<Surface *>;

        MeshModel();
        MeshModel(const MeshModel &t);
        ~MeshModel();

        MeshModel *getMeshModel() override { return this; }
        Entity *clone() override { return new MeshModel(*this); }

        void freeGpu(gpu::Device &dev) override;

        bool collide(const Line &line, float radius, Collision *currColl, const Transform &t) override;

        void setRenderBrush(const Brush &b) override;
        bool render(const RenderContext &rc) override;
        void renderQueue(int type) override;
        void uploadQueue(gpu::Device &dev, int type) override;

        void createBones();

        Surface *createSurface(const Brush &b);
        void setCullBox(const Box &box) { mRep->cullBox = box; }
        void updateNormals();
        void flipTriangles();
        void transform(const Transform &t);
        void paint(const Brush &b);
        void add(const MeshModel &t);

        const SurfaceList &getSurfaces() const { return mRep->surfaces; }
        Surface *findSurface(const Brush &b) const;
        bool intersects(const MeshModel &m) const;
        MeshCollider *getCollider() const;
        const Box &getBox() const;

    private:
        struct Rep
        {
            int refCount = 1;
            SurfaceList surfaces;
            // Bumped by the surfaces themselves on any geometry edit
            // (Surface::Monitor), so a mesh grown or moved after its
            // first render revalidates its box/normals/collider.
            Surface::Monitor mon;
            int &geomChanges = mon.geomChanges;
            int &brushChanges = mon.brushChanges;
            mutable int boxValid = -1, collValid = -1, normsValid = -1;
            mutable Box box, cullBox;
            mutable MeshCollider *collider = nullptr;
            ct::Vector<Transform> boneTforms;

            ~Rep()
            {
                delete collider;
                for (size_t k = 0; k < surfaces.size(); ++k) delete surfaces[k];
            }
        };
        Rep *mRep;

        int mLocalBrushChanges = -1;
        ct::Vector<Brush> mBrushes;

        ct::Vector<Surface::Bone> mSurfBones;
        ct::Vector<Matrix4> mBoneMats;

        bool gpuSkinned() const { return !mSurfBones.empty() && (int)mSurfBones.size() <= kMaxGpuBones; }


    public:
        /* public so World's broad-phase can read it; MeshModel::render
           still does the authoritative per-model frustum test with it */
        const Box &getCullBox() const { return mRep->cullBox.empty() ? getBox() : mRep->cullBox; }

        int gpuBoneCount() const override { return gpuSkinned() ? (int)mBoneMats.size() : 0; }
        const Matrix4 *gpuBoneMatrices() const override { return gpuSkinned() ? mBoneMats.data() : nullptr; }
    };
}

#endif
