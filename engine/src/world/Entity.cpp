#include "engine/Entity.h"
#include "engine/Object.h"

namespace engine
{
    Entity *Entity::sOrphans = nullptr;
    Entity *Entity::sLastOrphan = nullptr;

    void Entity::remove()
    {
        if (mParent)
        {
            if (mParent->mChildren == this) mParent->mChildren = mSucc;
            if (mParent->mLastChild == this) mParent->mLastChild = mPred;
        }
        else
        {
            if (sOrphans == this) sOrphans = mSucc;
            if (sLastOrphan == this) sLastOrphan = mPred;
        }
        if (mSucc) mSucc->mPred = mPred;
        if (mPred) mPred->mSucc = mSucc;
    }

    void Entity::insert()
    {
        mSucc = nullptr;
        if (mParent)
        {
            mPred = mParent->mLastChild;
            if (mPred) mPred->mSucc = this;
            else mParent->mChildren = this;
            mParent->mLastChild = this;
        }
        else
        {
            mPred = sLastOrphan;
            if (mPred) mPred->mSucc = this;
            else sOrphans = this;
            sLastOrphan = this;
        }
    }

    Entity::Entity()
    {
        insert();
    }

    Entity::Entity(const Entity &e)
        : mName(e.mName), mVisible(e.mVisible), mEnabled(e.mEnabled),
          mLocalPos(e.mLocalPos), mLocalScl(e.mLocalScl), mLocalRot(e.mLocalRot),
          mInvalid(InvalidLocal | InvalidWorld)
    {
        insert();
    }

    Entity::~Entity()
    {
        while (children()) delete children();
        remove();
    }

    void Entity::invalidateWorld()
    {
        if (mInvalid & InvalidWorld) return;
        mInvalid |= InvalidWorld;
        for (Entity *e = mChildren; e; e = e->mSucc)
            e->invalidateWorld();
    }

    void Entity::invalidateLocal()
    {
        mInvalid |= InvalidLocal;
        invalidateWorld();
    }

    const Transform &Entity::getLocalTform() const
    {
        if (mInvalid & InvalidLocal)
        {
            mLocalTform.m = blitz::Matrix(mLocalRot);
            mLocalTform.m.i *= mLocalScl.x;
            mLocalTform.m.j *= mLocalScl.y;
            mLocalTform.m.k *= mLocalScl.z;
            mLocalTform.v = mLocalPos;
            mInvalid &= ~InvalidLocal;
        }
        return mLocalTform;
    }

    const Transform &Entity::getWorldTform() const
    {
        if (mInvalid & InvalidWorld)
        {
            mWorldTform = mParent ? mParent->getWorldTform() * getLocalTform() : getLocalTform();
            mInvalid &= ~InvalidWorld;
        }
        return mWorldTform;
    }

    void Entity::setParent(Entity *p)
    {
        if (mParent == p) return;
        remove();
        mParent = p;
        insert();
        invalidateWorld();
    }

    void Entity::enumVisible(ct::Vector<Object *> &out)
    {
        if (!mVisible) return;
        if (Object *o = getObject()) out.push_back(o);
        for (Entity *e = mChildren; e; e = e->mSucc)
            e->enumVisible(out);
    }

    void Entity::enumEnabled(ct::Vector<Object *> &out)
    {
        if (!mEnabled) return;
        if (Object *o = getObject()) out.push_back(o);
        for (Entity *e = mChildren; e; e = e->mSucc)
            e->enumEnabled(out);
    }

    void Entity::setLocalPosition(const Vector &v) { mLocalPos = v; invalidateLocal(); }
    void Entity::setLocalScale(const Vector &v) { mLocalScl = v; invalidateLocal(); }
    void Entity::setLocalRotation(const Quat &q) { mLocalRot = q.normalized(); invalidateLocal(); }

    void Entity::setLocalTform(const Transform &t)
    {
        mLocalPos = t.v;
        mLocalScl = Vector(t.m.i.length(), t.m.j.length(), t.m.k.length());
        mLocalRot = blitz::matrixQuat(t.m);
        invalidateLocal();
    }

    void Entity::setWorldPosition(const Vector &v)
    {
        setLocalPosition(mParent ? -mParent->getWorldTform() * v : v);
    }

    void Entity::setWorldScale(const Vector &v)
    {
        setLocalScale(mParent ? v / mParent->getWorldScale() : v);
    }

    void Entity::setWorldRotation(const Quat &q)
    {
        setLocalRotation(mParent ? -mParent->getWorldRotation() * q : q);
    }

    void Entity::setWorldTform(const Transform &t)
    {
        setLocalTform(mParent ? -mParent->getWorldTform() * t : t);
    }

    const Vector &Entity::getWorldScale() const
    {
        mWorldScl = mParent ? mParent->getWorldScale() * mLocalScl : mLocalScl;
        return mWorldScl;
    }

    const Quat &Entity::getWorldRotation() const
    {
        mWorldRot = mParent ? mParent->getWorldRotation() * mLocalRot : mLocalRot;
        return mWorldRot;
    }
}
