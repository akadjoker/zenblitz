#ifndef ENGINE_ANIMATOR_H
#define ENGINE_ANIMATOR_H

#include "engine/Animation.h"
#include <ct/vector.hpp>

namespace engine
{
    class Object;

    class Animator
    {
    public:
        enum
        {
            AnimModeLoop = 1, AnimModePingPong = 2, AnimModeOneShot = 3
        };

        Animator(Animator *animator);
        Animator(Object *tree, int frames);
        Animator(const ct::Vector<Object *> &objs, int frames);

        void addSeq(int frames);
        void addSeqs(Animator *t);
        void extractSeq(int first, int last, int seq);

        void setAnimTime(float time, int seq);
        void animate(int mode, float speed, int seq, float trans);
        void update(float elapsed);

        int animSeq() const { return mSeq; }
        int animLen() const { return mSeqLen; }
        float animTime() const { return mTime; }
        bool animating() const { return mMode != 0; }

        int numSeqs() const { return (int)mSeqs.size(); }
        const ct::Vector<Object *> &getObjects() const { return mObjs; }

    private:
        struct Seq { int frames; };

        struct Anim
        {
            ct::Vector<Animation> keys;
            bool pos = false, scl = false, rot = false;
            Vector srcPos, destPos;
            Vector srcScl, destScl;
            Quat srcRot, destRot;
        };

        ct::Vector<Seq> mSeqs;
        ct::Vector<Anim> mAnims;
        ct::Vector<Object *> mObjs;

        int mSeq = 0, mMode = 0, mSeqLen = 0;
        float mTime = 0, mSpeed = 0, mTransTime = 0, mTransSpeed = 0;

        void reset();
        void addObjs(Object *obj);
        void updateAnim();
        void beginTrans();
        void updateTrans();
    };
}

#endif
