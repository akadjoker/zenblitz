#include "engine/Sprite.h"
#include "engine/Frustum.h"

namespace engine
{
    void Sprite::setupQuad()
    {
        Surface::Vertex v;
        mSurface.addVertex(v);
        mSurface.addVertex(v);
        mSurface.addVertex(v);
        mSurface.addVertex(v);
        mSurface.setTexCoords(0, 0.0f, 0.0f, 0); mSurface.setTexCoords(0, 0.0f, 0.0f, 1);
        mSurface.setTexCoords(1, 1.0f, 0.0f, 0); mSurface.setTexCoords(1, 1.0f, 0.0f, 1);
        mSurface.setTexCoords(2, 1.0f, 1.0f, 0); mSurface.setTexCoords(2, 1.0f, 1.0f, 1);
        mSurface.setTexCoords(3, 0.0f, 1.0f, 0); mSurface.setTexCoords(3, 0.0f, 1.0f, 1);
        Surface::Triangle t0{{0, 1, 2}}, t1{{0, 2, 3}};
        mSurface.addTriangle(t0);
        mSurface.addTriangle(t1);
    }

    Sprite::Sprite()
    {
        setRenderSpace(RenderSpaceWorld);
        setupQuad();
    }

    Sprite::Sprite(const Sprite &t)
        : Model(t), mXHandle(t.mXHandle), mYHandle(t.mYHandle), mRot(t.mRot),
          mXScale(t.mXScale), mYScale(t.mYScale), mViewMode(t.mViewMode)
    {
        setupQuad();
    }

    void Sprite::capture()
    {
        Model::capture();
        mRRot = mRot;
        mRXScale = mXScale;
        mRYScale = mYScale;
        mCaptured = true;
    }

    bool Sprite::beginRender(float tween)
    {
        Model::beginRender(tween);
        if (tween == 1.0f || !mCaptured)
        {
            mRRot = mRot;
            mRXScale = mXScale;
            mRYScale = mYScale;
        }
        else
        {
            mRRot = (mRot - mRRot) * tween + mRRot;
            mRXScale = (mXScale - mRXScale) * tween + mRXScale;
            mRYScale = (mYScale - mRYScale) * tween + mRYScale;
        }
        return true;
    }

    bool Sprite::render(const RenderContext &rc)
    {
        Transform t = getRenderTform();

        if (mViewMode == ViewModeFree)
        {
            t.m = rc.getCameraTform().m;
        }
        else if (mViewMode == ViewModeUpright)
        {
            t.m.k = rc.getCameraTform().m.k;
            t.m.orthogonalize();
        }
        else if (mViewMode == ViewModeUpright2)
        {
            t.m = blitz::yawMatrix(blitz::matrixYaw(rc.getCameraTform().m)) * t.m;
        }

        t.m = t.m * blitz::rollMatrix(mRRot) * blitz::scaleMatrix(mRXScale, mRYScale, 1.0f);

        Vector verts[4];
        verts[0] = t * Vector(-1.0f - mXHandle,  1.0f - mYHandle, 0.0f);
        verts[1] = t * Vector( 1.0f - mXHandle,  1.0f - mYHandle, 0.0f);
        verts[2] = t * Vector( 1.0f - mXHandle, -1.0f - mYHandle, 0.0f);
        verts[3] = t * Vector(-1.0f - mXHandle, -1.0f - mYHandle, 0.0f);

        if (!rc.getWorldFrustum().cull(verts, 4)) return false;

        for (int k = 0; k < 4; ++k) mSurface.setCoords(k, verts[k]);

        Surface::Triangle t0, t1;
        if (rc.isReflected())
        {
            t0.verts[0] = 0; t0.verts[1] = 2; t0.verts[2] = 1;
            t1.verts[0] = 0; t1.verts[1] = 3; t1.verts[2] = 2;
        }
        else
        {
            t0.verts[0] = 0; t0.verts[1] = 1; t0.verts[2] = 2;
            t1.verts[0] = 0; t1.verts[1] = 2; t1.verts[2] = 3;
        }
        mSurface.setTriangle(0, t0);
        mSurface.setTriangle(1, t1);

        enqueue(&mSurface, getRenderBrush());
        return false;
    }

    void Sprite::uploadQueue(gpu::Device &dev, int type)
    {
        ct::Vector<QueueEntry> &q = queue(type);
        for (size_t k = 0; k < q.size(); ++k)
        {
            q[k].surface->ensureGpu(dev);
            q[k].geom = q[k].surface->geometry();
        }
    }
}
