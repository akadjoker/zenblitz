#ifndef ENGINE_SPRITE_H
#define ENGINE_SPRITE_H

#include "engine/Model.h"
#include "engine/Surface.h"

namespace engine
{
    class Sprite : public Model
    {
    public:
        enum
        {
            ViewModeFree = 1,
            ViewModeFixed = 2,
            ViewModeUpright = 3,
            ViewModeUpright2 = 4,
        };

        Sprite();
        Sprite(const Sprite &t);

        Sprite *getSprite() override { return this; }
        Entity *clone() override { return new Sprite(*this); }

        void capture() override;
        bool beginRender(float tween) override;

        void setRotation(float angle) { mRot = angle; }
        void setScale(float xScale, float yScale) { mXScale = xScale; mYScale = yScale; }
        void setHandle(float x, float y) { mXHandle = x; mYHandle = y; }
        void setViewmode(int mode) { mViewMode = mode; }

        float getRotation() const { return mRot; }
        void getScale(float &x, float &y) const { x = mXScale; y = mYScale; }
        void getHandle(float &x, float &y) const { x = mXHandle; y = mYHandle; }
        int getViewmode() const { return mViewMode; }

        bool render(const RenderContext &rc) override;
        void uploadQueue(gpu::Device &dev, int type) override;
        void freeGpu(gpu::Device &dev) override { mSurface.freeGpu(dev); }

    private:
        void setupQuad();

        float mXHandle = 0.0f, mYHandle = 0.0f;
        float mRot = 0.0f, mXScale = 1.0f, mYScale = 1.0f;
        float mRRot = 0.0f, mRXScale = 1.0f, mRYScale = 1.0f;
        int mViewMode = ViewModeFree;
        bool mCaptured = false;

        Surface mSurface;
    };
}

#endif
