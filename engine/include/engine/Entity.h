#ifndef ENGINE_ENTITY_H
#define ENGINE_ENTITY_H

#include "engine/Geom.h"
#include <ct/vector.hpp>
#include <string>

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

    class Entity
    {
    public:
        Entity();
        Entity(const Entity &e);
        virtual ~Entity();

        virtual Entity *clone() = 0;

        virtual Object *getObject() { return nullptr; }
        virtual Camera *getCamera() { return nullptr; }
        virtual Light *getLight() { return nullptr; }
        virtual Model *getModel() { return nullptr; }
        virtual Mirror *getMirror() { return nullptr; }
        virtual Listener *getListener() { return nullptr; }

        void setName(const std::string &t) { mName = t; }
        void setParent(Entity *parent);

        void setVisible(bool vis) { mVisible = vis; }
        void setEnabled(bool ena) { mEnabled = ena; }

        bool visible() const { return mVisible; }
        bool enabled() const { return mEnabled; }

        void enumVisible(ct::Vector<Object *> &out);
        void enumEnabled(ct::Vector<Object *> &out);

        Entity *children() const { return mChildren; }
        Entity *successor() const { return mSucc; }

        std::string getName() const { return mName; }
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
        std::string mName;
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
