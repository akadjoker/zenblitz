#ifndef ENGINE_OBJECT_H
#define ENGINE_OBJECT_H

#include "engine/Entity.h"
#include "engine/Collision.h"
#include "engine/Animation.h"
#include <ct/vector.hpp>

namespace engine
{
    using blitz::Box;

    class Object;
    class Animator;

    struct ObjCollision
    {
        Object *with;
        Vector coords;
        Collision collision;
    };

    class Object : public Entity
    {
    public:
        using Collisions = ct::Vector<const ObjCollision *>;

        Object();
        Object(const Object &object);
        ~Object();

        Object *getObject() override { return this; }
        Entity *clone() override { return new Object(*this); }

        Object *copy();

        void reset();
        void setCollisionType(int type) { mCollType = type; }
        void setCollisionRadii(const Vector &radii) { mCollRadii = radii; }
        void setCollisionBox(const Box &box) { mCollBox = box; }
        void setOrder(int n) { mOrder = n; }
        void setPickGeometry(int n) { mPickGeom = n; }
        void setObscurer(bool t) { mObscurer = t; }
        void setAnimation(const Animation &t) { mAnim = t; }
        void setAnimator(Animator *t);

        virtual bool collide(const Line &line, float radius, Collision *currColl, const Transform &t)
        {
            (void)line; (void)radius; (void)currColl; (void)t;
            return false;
        }
        virtual void capture();
        virtual void animate(float elapsed);
        virtual bool beginRender(float tween);
        virtual void endRender() {}

        void beginUpdate(float elapsed);
        void addCollision(const ObjCollision *c) { mColls.push_back(c); }
        void endUpdate();

        int getCollisionType() const { return mCollType; }
        const Vector &getCollisionRadii() const { return mCollRadii; }
        const Box &getCollisionBox() const { return mCollBox; }
        int getOrder() const { return mOrder; }
        const Vector &getVelocity() const { return mVelocity; }
        const Collisions &getCollisions() const { return mColls; }
        const Transform &getRenderTform() const;
        const Transform &getPrevWorldTform() const { return mPrevTform; }
        /* Moves the point this frame's sweep starts from. Only for
           depenetration, which has to shift both ends of the sweep: leave
           mPrevTform behind and the sweep just walks the entity straight
           back down into the geometry it was lifted out of. */
        void setPrevWorldPosition(const Vector &v) { mPrevTform.v = v; }
        int getPickGeometry() const { return mPickGeom; }
        bool getObscurer() const { return mObscurer; }
        Animation getAnimation() const { return mAnim; }
        Animator *getAnimator() const { return mAnimator; }
        Object *getLastCopy() const { return mLastCopy; }

    private:
        int mCollType = 0;
        int mOrder = 0;
        Vector mCollRadii{1, 1, 1};
        Collisions mColls;
        bool mCaptured = false;
        Box mCollBox;
        int mPickGeom = 0;
        bool mObscurer = false;
        float mElapsed = 0.0f;
        Vector mVelocity;
        mutable Object *mLastCopy = nullptr;

        Transform mPrevTform;
        Transform mCapturedTform;
        Transform mTweenTform;
        mutable Transform mRenderTform;
        mutable bool mRenderTformValid = false;

        Animation mAnim;
        Animator *mAnimator = nullptr;
    };
}

#endif
