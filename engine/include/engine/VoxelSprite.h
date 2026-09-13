#ifndef ENGINE_VOXELSPRITE_H
#define ENGINE_VOXELSPRITE_H

#include "engine/Model.h"
#include "engine/DynamicMesh.h"

namespace engine
{
    class VoxelSprite : public Model
    {
    public:
        explicit VoxelSprite(int slices = 64);
        VoxelSprite(const VoxelSprite &other);

        Entity *clone() override { return new VoxelSprite(*this); }

        void setAtlas(int columns, int rows, int firstFrame = 0, int frameCount = 0);
        int slices() const { return mSlices; }
        bool render(const RenderContext &rc) override;
        void uploadQueue(gpu::Device &dev, int type) override;
        void freeGpu(gpu::Device &dev) override;

    private:
        void rebuild();
        void buildAxis(int axis, bool positiveEye);

        int mSlices = 64;
        int mColumns = 1, mRows = 1;
        int mFirstFrame = 0, mFrameCount = 1;
        DynamicMesh mMeshes[6];
    };
}

#endif
