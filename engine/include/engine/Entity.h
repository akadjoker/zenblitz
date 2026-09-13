#ifndef ENGINE_ENTITY_H
#define ENGINE_ENTITY_H

#include "engine/Geom.h"
#include "gpu/GPU.h"
#include <ct/vector.hpp>
#include <ct/string.hpp>

namespace engine
{
    using blitz::Vector;
    using blitz::Quat;
    using blitz::Transform;

    class Object;
    class Camera;
    class Light;
    class Model;
    class Mirror;
    class Listener;
    class Sprite;

    class Entity
    {
    public:
        Entity();
        Entity(const Entity &e);
        virtual ~Entity();

        virtual Entity *clone() = 0;

        // Releases GPU buffers this entity owns. Destructors can't do it
        // (freeing a buffer needs the device), so the scene owner calls
        // this on the whole tree before deleting it.
        virtual void freeGpu(gpu::Device &dev) { (void)dev; }
        void freeGpuTree(gpu::Device &dev)
        {
            freeGpu(dev);
            for (Entity *c = mChildren; c; c = c->mSucc) c->freeGpuTree(dev);
        }

        virtual Object *getObject() { return nullptr; }
        virtual Camera *getCamera() { return nullptr; }
        virtual Light *getLight() { return nullptr; }
        virtual Model *getModel() { return nullptr; }
        virtual Mirror *getMirror() { return nullptr; }
        virtual Listener *getListener() { return nullptr; }
        virtual Sprite *getSprite() { return nullptr; }

        void setName(const ct::String &t) { mName = t; }
        void setParent(Entity *parent);

        void setVisible(bool vis) { mVisible = vis; }
        void setEnabled(bool ena) { mEnabled = ena; }

        bool visible() const { return mVisible; }
        bool enabled() const { return mEnabled; }

        void enumVisible(ct::Vector<Object *> &out);
        void enumEnabled(ct::Vector<Object *> &out);

        Entity *children() const { return mChildren; }
        Entity *successor() const { return mSucc; }

        ct::String getName() const { return mName; }
        Entity *getParent() const { return mParent; }

        void setLocalPosition(const Vector &v);
        void setLocalScale(const Vector &v);
        void setLocalRotation(const Quat &q);
        void setLocalTform(const Transform &t);

        void setWorldPosition(const Vector &v);
        void setWorldScale(const Vector &v);
        void setWorldRotation(const Quat &q);
        void setWorldTform(const Transform &t);

        const Vector &getLocalPosition() const { return mLocalPos; }
        const Vector &getLocalScale() const { return mLocalScl; }
        const Quat &getLocalRotation() const { return mLocalRot; }
        const Transform &getLocalTform() const;

        const Vector &getWorldPosition() const { return getWorldTform().v; }
        const Vector &getWorldScale() const;
        const Quat &getWorldRotation() const;
        const Transform &getWorldTform() const;

        static Entity *orphans() { return sOrphans; }

    private:
        enum
        {
            InvalidLocal = 1,
            InvalidWorld = 2,
        };

        Entity *mSucc = nullptr, *mPred = nullptr, *mParent = nullptr;
        Entity *mChildren = nullptr, *mLastChild = nullptr;

        static Entity *sOrphans, *sLastOrphan;

        bool mVisible = true, mEnabled = true;
        ct::String mName;
        mutable int mInvalid = 0;

        Quat mLocalRot;
        Vector mLocalPos, mLocalScl{1, 1, 1};
        mutable Transform mLocalTform;

        mutable Quat mWorldRot;
        mutable Vector mWorldPos, mWorldScl;
        mutable Transform mWorldTform;

        void insert();
        void remove();
        void invalidateLocal();
        void invalidateWorld();
    };
}

#endif
