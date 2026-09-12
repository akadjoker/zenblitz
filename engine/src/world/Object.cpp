#include "engine/Object.h"

namespace engine
{
    Object::Object() : mCollBox(Box(Vector(-1, -1, -1), Vector(1, 1, 1)))
    {
        reset();
    }

    Object::Object(const Object &o)
        : Entity(o), mCollType(o.mCollType), mOrder(o.mOrder), mCollRadii(o.mCollRadii),
          mCollBox(o.mCollBox), mPickGeom(o.mPickGeom), mObscurer(o.mObscurer)
    {
        reset();
    }

    Object::~Object()
    {
        mVelocity = Vector();
    }

    void Object::reset()
    {
        mColls.clear();
        mVelocity = Vector();
        mPrevTform = getWorldTform();
    }

    void Object::beginUpdate(float e)
    {
        mElapsed = e;
        mColls.clear();
        animate(e);
    }

    void Object::endUpdate()
    {
        mVelocity = (getWorldTform().v - mPrevTform.v) / mElapsed;
        mPrevTform = getWorldTform();
    }

    void Object::capture()
    {
        mCapturedTform = getLocalTform();
        mCaptured = true;
    }

    bool Object::beginRender(float tween)
    {
        if (tween == 1 || !mCaptured)
        {
            mRenderTform = getWorldTform();
            mRenderTformValid = true;
        }
        else
        {
            Vector capPos = mCapturedTform.v;
            Vector capScl(mCapturedTform.m.i.length(), mCapturedTform.m.j.length(), mCapturedTform.m.k.length());
            Quat capRot = blitz::matrixQuat(mCapturedTform.m);

            Vector pos = (getLocalPosition() - capPos) * tween + capPos;
            Vector scl = (getLocalScale() - capScl) * tween + capScl;
            Quat rot = capRot.slerpTo(getLocalRotation(), tween);
            mTweenTform.m = blitz::Matrix(rot);
            mTweenTform.m.i *= scl.x;
            mTweenTform.m.j *= scl.y;
            mTweenTform.m.k *= scl.z;
            mTweenTform.v = pos;
            mRenderTformValid = false;
        }
        return true;
    }

    const Transform &Object::getRenderTform() const
    {
        if (mRenderTformValid) return mRenderTform;

        Object *parent = (Object *)getParent();
        mRenderTform = parent ? parent->getRenderTform() * mTweenTform : mTweenTform;
        mRenderTformValid = true;

        return mRenderTform;
    }
}
