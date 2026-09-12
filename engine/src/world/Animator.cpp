#include "engine/Animator.h"
#include "engine/Object.h"
#include <cmath>

namespace engine
{
    Animator::Animator(Animator *t) : mSeqs(t->mSeqs)
    {
        mObjs.resize(t->mObjs.size());
        mAnims.resize(t->mAnims.size());

        for (size_t k = 0; k < t->mObjs.size(); ++k)
        {
            mObjs[k] = t->mObjs[k]->getLastCopy();
            mAnims[k].keys = t->mAnims[k].keys;
        }
        reset();
    }

    Animator::Animator(Object *obj, int frames)
    {
        addObjs(obj);
        mAnims.resize(mObjs.size());
        addSeq(frames);
        reset();
    }

    Animator::Animator(const ct::Vector<Object *> &objs, int frames) : mObjs(objs)
    {
        mAnims.resize(mObjs.size());
        addSeq(frames);
        reset();
    }

    void Animator::reset()
    {
        mSeq = mMode = mSeqLen = 0;
        mTime = mSpeed = mTransTime = mTransSpeed = 0;
    }

    void Animator::addObjs(Object *obj)
    {
        mObjs.push_back(obj);
        for (Entity *e = obj->children(); e; e = e->successor())
            addObjs(e->getObject());
    }

    void Animator::addSeq(int frames)
    {
        Seq seq{frames};
        mSeqs.push_back(seq);
        for (size_t k = 0; k < mObjs.size(); ++k)
        {
            Object *obj = mObjs[k];
            mAnims[k].keys.push_back(obj->getAnimation());
            obj->setAnimation(Animation());
        }
    }

    void Animator::addSeqs(Animator *t)
    {
        for (size_t n = 0; n < t->mSeqs.size(); ++n)
        {
            mSeqs.push_back(t->mSeqs[n]);
            for (size_t k = 0; k < mObjs.size(); ++k)
            {
                size_t j = 0;
                for (; j < t->mObjs.size(); ++j)
                    if (mObjs[k]->getName() == t->mObjs[j]->getName()) break;
                if (j == t->mObjs.size())
                {
                    mAnims[k].keys.push_back(Animation());
                    continue;
                }
                mAnims[k].keys.push_back(t->mAnims[j].keys[n]);
            }
        }
    }

    void Animator::extractSeq(int first, int last, int seq)
    {
        Seq sq{last - first};
        mSeqs.push_back(sq);

        for (size_t k = 0; k < mObjs.size(); ++k)
        {
            Animation &keys = mAnims[k].keys[seq];
            mAnims[k].keys.push_back(keys.extract(first, last));
        }
    }

    void Animator::updateAnim()
    {
        for (size_t k = 0; k < mObjs.size(); ++k)
        {
            Object *obj = mObjs[k];
            const Animation &keys = mAnims[k].keys[mSeq];

            if (keys.numPositionKeys()) obj->setLocalPosition(keys.getPosition(mTime));
            if (keys.numScaleKeys()) obj->setLocalScale(keys.getScale(mTime));
            if (keys.numRotationKeys()) obj->setLocalRotation(keys.getRotation(mTime));
        }
    }

    void Animator::updateTrans()
    {
        for (size_t k = 0; k < mObjs.size(); ++k)
        {
            Object *obj = mObjs[k];
            const Anim &anim = mAnims[k];

            if (anim.pos) obj->setLocalPosition((anim.destPos - anim.srcPos) * mTransTime + anim.srcPos);
            if (anim.scl) obj->setLocalScale((anim.destScl - anim.srcScl) * mTransTime + anim.srcScl);
            if (anim.rot) obj->setLocalRotation(anim.srcRot.slerpTo(anim.destRot, mTransTime));
        }
    }

    void Animator::beginTrans()
    {
        for (size_t k = 0; k < mObjs.size(); ++k)
        {
            Object *obj = mObjs[k];
            Anim &anim = mAnims[k];
            const Animation &keys = mAnims[k].keys[mSeq];

            anim.pos = keys.numPositionKeys() != 0;
            if (anim.pos) { anim.srcPos = obj->getLocalPosition(); anim.destPos = keys.getPosition(mTime); }

            anim.scl = keys.numScaleKeys() != 0;
            if (anim.scl) { anim.srcScl = obj->getLocalScale(); anim.destScl = keys.getScale(mTime); }

            anim.rot = keys.numRotationKeys() != 0;
            if (anim.rot) { anim.srcRot = obj->getLocalRotation(); anim.destRot = keys.getRotation(mTime); }
        }
    }

    void Animator::setAnimTime(float time, int seq)
    {
        if (seq < 0 || seq > (int)mSeqs.size()) return;

        mMode = 0;
        mSpeed = 0;
        mSeq = seq;
        mSeqLen = mSeqs[mSeq].frames;

        mTime = std::fmod(time, (float)mSeqLen);
        if (mTime < 0) mTime += mSeqLen;

        updateAnim();
    }

    void Animator::animate(int mode, float speed, int seq, float trans)
    {
        if (!mode && !speed) { mMode = 0; return; }
        if (seq < 0 || seq >= (int)mSeqs.size()) return;

        mSeq = seq;
        mMode = mode;
        mSeqLen = mSeqs[mSeq].frames;
        mSpeed = speed;
        mTime = mSpeed >= 0 ? 0 : (float)mSeqLen;

        if (trans <= 0)
        {
            updateAnim();
            if (!mSpeed) mMode = 0;
            return;
        }

        mMode |= 0x8000;
        mTransTime = 0;
        mTransSpeed = 1.0f / trans;
        beginTrans();
    }

    void Animator::update(float elapsed)
    {
        if (!mMode) return;

        if (mMode & 0x8000)
        {
            mTransTime += mTransSpeed * elapsed;
            if (mTransTime < 1)
            {
                updateTrans();
                return;
            }
            mMode &= 0x7fff;
            if (!mMode || !mSpeed)
            {
                updateAnim();
                mMode = 0;
                return;
            }
        }

        mTime += mSpeed * elapsed;

        switch (mMode)
        {
        case AnimModeLoop:
            mTime = std::fmod(mTime, (float)mSeqLen);
            if (mTime < 0) mTime += mSeqLen;
            break;
        case AnimModePingPong:
            mTime = std::fmod(mTime, (float)mSeqLen * 2);
            if (mTime < 0) mTime += mSeqLen * 2;
            if (mTime >= mSeqLen) { mTime = mSeqLen - (mTime - mSeqLen); mSpeed = -mSpeed; }
            break;
        case AnimModeOneShot:
            if (mTime < 0) { mTime = 0; mMode = 0; }
            else if (mTime >= mSeqLen) { mTime = (float)mSeqLen; mMode = 0; }
            break;
        }

        updateAnim();
    }
}
