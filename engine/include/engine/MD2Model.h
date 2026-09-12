#ifndef ENGINE_MD2MODEL_H
#define ENGINE_MD2MODEL_H

#include "engine/Model.h"
#include "engine/MD2Rep.h"
#include <string>

namespace engine
{
    class MD2Model : public Model
    {
    public:
        MD2Model(const std::string &filename);
        MD2Model(const MD2Model &t);
        ~MD2Model();

        Entity *clone() override { return new MD2Model(*this); }
        MD2Model *getMD2Model() override { return this; }

        void animate(float elapsed) override;
        bool render(const RenderContext &rc) override;
        void uploadQueue(gpu::Device &dev, int type) override;

        void startMD2Anim(int first, int last, int mode, float speed, float trans);

        int getMD2AnimLength() const { return mRep->numFrames(); }
        bool getMD2Animating() const { return mAnimMode != 0; }
        float getMD2AnimTime() const { return mAnimTime; }
        bool getValid() const { return mRep->valid(); }

    private:
        MD2Rep *mRep;

        int mAnimMode = 0;
        float mAnimTime = 0, mAnimSpeed = 0;
        int mAnimFirst = 0, mAnimLast = 0, mAnimLen = 0;

        float mRenderT = 0;
        int mRenderA = 0, mRenderB = 0;

        // transition: the pose captured on the CPU when Animate is called
        // with a transition time, lerped on the GPU toward the new frame
        float mTransTime = 0, mTransSpeed = 0;
        ct::Vector<Md2Vert> mTransVerts;
        bool mTransDirty = false;
        gpu::BufferHandle mTransVb;
        gpu::Device *mTransDevice = nullptr;

        GpuGeometry currentGeometry() const;
        void captureCurrentPose();
        void lerpTransToward(int frame, float t);
    };
}

#endif
