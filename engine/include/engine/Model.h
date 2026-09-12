#ifndef ENGINE_MODEL_H
#define ENGINE_MODEL_H

#include "engine/Object.h"
#include "engine/Brush.h"
#include "engine/Surface.h"
#include "engine/RenderContext.h"
#include <ct/vector.hpp>

namespace engine
{
    class MeshModel;

    class Model : public Object
    {
    public:
        enum
        {
            RenderSpaceLocal = 0, RenderSpaceWorld = 1
        };
        enum
        {
            CollisionGeomDefault = 0, CollisionGeomTris = 1, CollisionGeomBox = 2, CollisionGeomSphere = 3
        };
        enum
        {
            QueueOpaque = 0, QueueTransparent = 1
        };

        struct QueueEntry
        {
            Surface *surface = nullptr;
            int firstVertex = 0, vertexCount = 0, firstTri = 0, triCount = 0;
            Brush brush;
        };

        Model() {}
        Model(const Model &m)
            : Object(m), mSpace(m.mSpace), mBrush(m.mBrush), mAutoFade(m.mAutoFade),
              mAutoFadeNr(m.mAutoFadeNr), mAutoFadeFr(m.mAutoFadeFr), mCapturedAlpha(m.mCapturedAlpha) {}

        Model *getModel() override { return this; }
        Entity *clone() override { return new Model(*this); }

        virtual MeshModel *getMeshModel() { return nullptr; }

        virtual void setRenderBrush(const Brush &b) { mRenderBrush = b; }
        virtual bool render(const RenderContext &rc) { (void)rc; return false; }
        virtual void renderQueue(int type) { (void)type; }
        // Uploads whatever geometry queue(type) references to the GPU
        // (Surface::ensureGpu/ensureGpuSkinned) - call with no render pass
        // open, after render()/renderQueue() have filled the queue.
        virtual void uploadQueue(gpu::Device &dev, int type) { (void)dev; (void)type; }

        void capture() override
        {
            Object::capture();
            mCapturedAlpha = mBrush.getAlpha();
        }

        bool beginRender(float t) override
        {
            Object::beginRender(t);
            mTweenedAlpha = mBrush.getAlpha();
            if (t != 1 && mTweenedAlpha != mCapturedAlpha)
                mTweenedAlpha = (mTweenedAlpha - mCapturedAlpha) * t + mCapturedAlpha;
            return mTweenedAlpha > 0;
        }

        bool doAutoFade(const Vector &eye)
        {
            float alpha = mTweenedAlpha;
            if (mAutoFade)
            {
                float d = eye.distance(getRenderTform().v);
                if (d >= mAutoFadeFr) return false;
                if (d >= mAutoFadeNr)
                {
                    float t = 1 - (d - mAutoFadeNr) / (mAutoFadeFr - mAutoFadeNr);
                    alpha *= t;
                    if (alpha <= 0) return false;
                }
            }
            if (mWBrush) mRenderBrush = mBrush;
            if (alpha != mRenderBrush.getAlpha())
                mRenderBrush.setAlpha(alpha);
            else if (!mWBrush)
                return true;

            mWBrush = false;
            return true;
        }

        virtual void setBrush(const Brush &b) { mBrush = b; mWBrush = true; }
        void setColor(const Vector &c) { mBrush.setColor(c); mWBrush = true; }
        void setAlpha(float a) { mBrush.setAlpha(a); mWBrush = true; }
        void setShininess(float s) { mBrush.setShininess(s); mWBrush = true; }
        void setTexture(int i, const BrushTexture &t) { mBrush.setTexture(i, t); mWBrush = true; }
        void setBlend(int n) { mBrush.setBlend(n); mWBrush = true; }
        void setFX(int n) { mBrush.setFX(n); mWBrush = true; }

        const Brush &getBrush() const { return mBrush; }
        const Brush &getRenderBrush() const { return mRenderBrush; }

        void setRenderSpace(int n) { mSpace = n; }
        int getRenderSpace() const { return mSpace; }

        void setAutoFade(float nr, float fr) { mAutoFadeNr = nr; mAutoFadeFr = fr; mAutoFade = true; }

        void enqueue(Surface *surface, int fv, int vc, int ft, int tc)
        {
            enqueue(surface, fv, vc, ft, tc, mRenderBrush);
        }
        void enqueue(Surface *surface, int fv, int vc, int ft, int tc, const Brush &brush)
        {
            QueueEntry e{surface, fv, vc, ft, tc, brush};
            int type = brush.getBlend() == BlendReplace ? QueueOpaque : QueueTransparent;
            mQueues[type].push_back(e);
        }

        ct::Vector<QueueEntry> &queue(int type) { return mQueues[type]; }
        int queueSize(int type) const { return (int)mQueues[type].size(); }

    private:
        int mSpace = RenderSpaceLocal;
        Brush mBrush, mRenderBrush;
        mutable bool mWBrush = true;
        float mCapturedAlpha = 1.0f, mTweenedAlpha = 1.0f;
        bool mAutoFade = false;
        float mAutoFadeNr = 0.0f, mAutoFadeFr = 0.0f;
        ct::Vector<QueueEntry> mQueues[2];
    };
}

#endif
