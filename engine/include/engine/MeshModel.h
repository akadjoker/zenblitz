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
        void setCullBox(const Box &box) { mCullBox = box; }
        void updateNormals();
        void flipTriangles();
        void transform(const Transform &t);
        void paint(const Brush &b);
        void add(const MeshModel &t);

        const SurfaceList &getSurfaces() const { return mSurfaces; }
        Surface *findSurface(const Brush &b) const;
        bool intersects(const MeshModel &m) const;
        MeshCollider *getCollider() const;
        const Box &getBox() const;

    private:
        SurfaceList mSurfaces;
        int mGeomChanges = 0, mBrushChanges = 0;
        mutable int mBoxValid = -1, mCollValid = -1, mNormsValid = -1;
        mutable Box mBox, mCullBox;
        mutable MeshCollider *mCollider = nullptr;

        int mLocalBrushChanges = -1;
        ct::Vector<Brush> mBrushes;

        ct::Vector<Surface::Bone> mSurfBones;
        ct::Vector<Transform> mBoneTforms;
        ct::Vector<Matrix4> mBoneMats;

        bool gpuSkinned() const { return !mSurfBones.empty() && (int)mSurfBones.size() <= kMaxGpuBones; }

        const Box &getCullBox() const { return mCullBox.empty() ? getBox() : mCullBox; }

    public:
        int gpuBoneCount() const override { return gpuSkinned() ? (int)mBoneMats.size() : 0; }
        const Matrix4 *gpuBoneMatrices() const override { return gpuSkinned() ? mBoneMats.data() : nullptr; }
    };
}

#endif
