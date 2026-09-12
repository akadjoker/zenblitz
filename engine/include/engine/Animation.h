#ifndef ENGINE_ANIMATION_H
#define ENGINE_ANIMATION_H

#include "engine/Geom.h"
#include <ct/vector.hpp>

namespace engine
{
    using blitz::Vector;
    using blitz::Quat;

    class Animation
    {
    public:
        void setScaleKey(int time, const Vector &q) { setKey(mScaleAnim, time, Quat(0, q)); }
        void setPositionKey(int time, const Vector &q) { setKey(mPosAnim, time, Quat(0, q)); }
        void setRotationKey(int time, const Quat &q) { setKey(mRotAnim, time, q); }

        int numScaleKeys() const { return (int)mScaleAnim.size(); }
        int numRotationKeys() const { return (int)mRotAnim.size(); }
        int numPositionKeys() const { return (int)mPosAnim.size(); }

        Vector getScale(float time) const
        {
            if (mScaleAnim.empty()) return Vector(1, 1, 1);
            return getLinearValue(mScaleAnim, time);
        }
        Vector getPosition(float time) const
        {
            if (mPosAnim.empty()) return Vector(0, 0, 0);
            return getLinearValue(mPosAnim, time);
        }
        Quat getRotation(float time) const
        {
            if (mRotAnim.empty()) return Quat();
            return getSlerpValue(mRotAnim, time);
        }

        Animation extract(int first, int last) const
        {
            Animation out;
            extractInto(out.mPosAnim, mPosAnim, first, last);
            extractInto(out.mScaleAnim, mScaleAnim, first, last);
            extractInto(out.mRotAnim, mRotAnim, first, last);
            return out;
        }

    private:
        struct Key
        {
            int time;
            Quat value;
        };
        using KeyList = ct::Vector<Key>;

        KeyList mScaleAnim, mRotAnim, mPosAnim;

        static void setKey(KeyList &keys, int time, const Quat &value)
        {
            size_t i = 0;
            for (; i < keys.size() && keys[i].time < time; ++i) {}
            if (i < keys.size() && keys[i].time == time) { keys[i].value = value; return; }
            Key k{time, value};
            keys.insert(keys.begin() + i, k);
        }

        static void extractInto(KeyList &out, const KeyList &in, int first, int last)
        {
            for (size_t i = 0; i < in.size(); ++i)
                if (in[i].time >= first && in[i].time <= last)
                    setKey(out, in[i].time - first, in[i].value);
        }

        static Vector getLinearValue(const KeyList &keys, float time)
        {
            size_t next = 0;
            while (next < keys.size() && (float)keys[next].time <= time) ++next;

            if (next == 0) return keys[0].value.v;
            if (next == keys.size()) return keys[next - 1].value.v;

            const Key &curr = keys[next - 1];
            const Key &nxt = keys[next];
            float delta = (time - curr.time) / (float)(nxt.time - curr.time);
            return (nxt.value.v - curr.value.v) * delta + curr.value.v;
        }

        static Quat getSlerpValue(const KeyList &keys, float time)
        {
            size_t next = 0;
            while (next < keys.size() && (float)keys[next].time <= time) ++next;

            if (next == 0) return keys[0].value;
            if (next == keys.size()) return keys[next - 1].value;

            const Key &curr = keys[next - 1];
            const Key &nxt = keys[next];
            float delta = (time - curr.time) / (float)(nxt.time - curr.time);
            return curr.value.slerpTo(nxt.value, delta);
        }
    };
}

#endif
