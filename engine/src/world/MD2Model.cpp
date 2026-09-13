#include "engine/MD2Model.h"
#include "engine/Animator.h"
#include "engine/Frustum.h"
#include <cmath>
#include <utility>

namespace engine
{
    MD2Model::MD2Model(const ct::String &f) : mRep(new MD2Rep(f)) {}

    MD2Model::MD2Model(const MD2Model &t) : Model(t), mRep(t.mRep)
    {
        ++mRep->refCount;
    }

    MD2Model::~MD2Model()
    {
        if (!--mRep->refCount) delete mRep;
    }

    void MD2Model::freeGpu(gpu::Device &dev)
    {
        // the Rep's frame buffers are shared by every model cloned from
        // it, so only the last one may free them
        if (mRep->refCount == 1) mRep->freeGpu(dev);
        if (mTransVb.valid()) { dev.destroy(mTransVb); mTransVb = gpu::BufferHandle(); }
        mTransDevice = nullptr;
        mTransDirty = true;
    }

    void MD2Model::captureCurrentPose()
    {
        const int n = mRep->numVertices();
        mTransVerts.resize(n);
        const Md2Vert *a = mRep->frameVerts(mRenderA);
        const Md2Vert *b = mRep->frameVerts(mRenderB);
        for (int k = 0; k < n; ++k)
        {
            Md2Vert &v = mTransVerts[k];
            v.x = (b[k].x - a[k].x) * mRenderT + a[k].x;
            v.y = (b[k].y - a[k].y) * mRenderT + a[k].y;
            v.z = (b[k].z - a[k].z) * mRenderT + a[k].z;
            v.nx = (b[k].nx - a[k].nx) * mRenderT + a[k].nx;
            v.ny = (b[k].ny - a[k].ny) * mRenderT + a[k].ny;
            v.nz = (b[k].nz - a[k].nz) * mRenderT + a[k].nz;
        }
    }

    void MD2Model::lerpTransToward(int frame, float t)
    {
        const int n = mRep->numVertices();
        mTransVerts.resize(n);
        const Md2Vert *b = mRep->frameVerts(frame);
        for (int k = 0; k < n; ++k)
        {
            Md2Vert &v = mTransVerts[k];
            v.x += (b[k].x - v.x) * t;
            v.y += (b[k].y - v.y) * t;
            v.z += (b[k].z - v.z) * t;
            v.nx += (b[k].nx - v.nx) * t;
            v.ny += (b[k].ny - v.ny) * t;
            v.nz += (b[k].nz - v.nz) * t;
        }
    }

    void MD2Model::startMD2Anim(int first, int last, int mode, float speed, float trans)
    {
        if (!mRep->valid()) return;
        if (last < first) std::swap(first, last);

        const int maxFrame = mRep->numFrames() - 1;
        if (first < 0) first = 0; else if (first > maxFrame) first = maxFrame;
        if (last < 0) last = 0; else if (last > maxFrame) last = maxFrame;

        if (trans > 0)
        {
            if (mAnimMode & 0x8000) lerpTransToward((int)mAnimTime, mTransTime);
            else captureCurrentPose();
            mTransSpeed = 1.0f / trans;
            mTransTime = 0;
            mTransDirty = true;
            mode |= 0x8000;
        }

        mAnimFirst = first;
        mAnimLast = last;
        mAnimLen = last - first;
        mAnimSpeed = speed;
        mAnimTime = (float)(((mode & 0x7fff) == Animator::AnimModeLoop || mAnimSpeed >= 0) ? mAnimFirst : mAnimLast);
        mAnimMode = mode;

        if (!mAnimSpeed || !mAnimLen)
        {
            mRenderA = mRenderB = (int)mAnimTime;
            mRenderT = 0;
            mAnimMode &= 0x8000;
        }
    }

    void MD2Model::animate(float e)
    {
        Object::animate(e);
        if (!mAnimMode) return;
        if (mAnimMode & 0x8000)
        {
            // the original advances the transition per call, not per second
            mTransTime += mTransSpeed;
            if (mTransTime < 1) return;
            mAnimMode &= ~0x8000;
            if (!mAnimMode) return;
        }
        mAnimTime = mAnimTime + mAnimSpeed * e;
        if (mAnimTime < mAnimFirst)
        {
            switch (mAnimMode)
            {
            case Animator::AnimModeLoop:
                mAnimTime += mAnimLen;
                break;
            case Animator::AnimModePingPong:
                mAnimTime = mAnimFirst + (mAnimFirst - mAnimTime);
                mAnimSpeed = -mAnimSpeed;
                break;
            default:
                mAnimTime = (float)mAnimFirst;
                mAnimMode = 0;
                break;
            }
        }
        else if (mAnimTime >= mAnimLast)
        {
            switch (mAnimMode)
            {
            case Animator::AnimModeLoop:
                mAnimTime -= mAnimLen;
                break;
            case Animator::AnimModePingPong:
                mAnimTime = mAnimLast - (mAnimTime - mAnimLast);
                mAnimSpeed = -mAnimSpeed;
                break;
            default:
                mAnimTime = (float)mAnimLast;
                mAnimMode = 0;
                break;
            }
        }
        mRenderA = (int)std::floor(mAnimTime);
        mRenderB = mRenderA + 1;
        if (mAnimMode == Animator::AnimModeLoop && mRenderB == mAnimLast) mRenderB = mAnimFirst;
        if (mRenderB > mRep->numFrames() - 1) mRenderB = mRep->numFrames() - 1;
        mRenderT = mAnimTime - mRenderA;
    }

    GpuGeometry MD2Model::currentGeometry() const
    {
        if (mAnimMode & 0x8000)
        {
            int frame = (int)mAnimTime;
            if (frame > mRep->numFrames() - 1) frame = mRep->numFrames() - 1;
            return mRep->geometryFrom(mTransVb, frame, mTransTime);
        }
        return mRep->geometry(mRenderA, mRenderB, mRenderT);
    }

    bool MD2Model::render(const RenderContext &rc)
    {
        if (!mRep->valid()) return false;
        Frustum f(rc.getWorldFrustum(), -getRenderTform());
        if (!f.cull(mRep->getBox())) return false;

        enqueue(currentGeometry(), getRenderBrush());
        return false;
    }

    void MD2Model::uploadQueue(gpu::Device &dev, int type)
    {
        if (!mRep->ensureGpu(dev)) return;

        if (mTransDirty && !mTransVerts.empty())
        {
            if (!mTransVb.valid())
            {
                gpu::BufferDesc desc;
                desc.size = mTransVerts.size() * sizeof(Md2Vert);
                desc.usage = gpu::BufferUsageVertex;
                desc.debugName = "md2.transition";
                mTransVb = dev.createBuffer(desc);
                mTransDevice = &dev;
            }
            if (mTransVb.valid())
                dev.updateBuffer(mTransVb, 0, {mTransVerts.data(), mTransVerts.size() * sizeof(Md2Vert)});
            mTransDirty = false;
        }

        ct::Vector<QueueEntry> &q = queue(type);
        for (size_t k = 0; k < q.size(); ++k) q[k].geom = currentGeometry();
    }
}
