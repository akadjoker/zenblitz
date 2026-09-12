#ifndef ENGINE_LISTENER_H
#define ENGINE_LISTENER_H

#include "engine/Object.h"

namespace engine
{
    class Listener : public Object
    {
    public:
        Listener(float roll, float dopp, float dist) : mRoll(roll), mDopp(dopp), mDist(dist) {}

        Entity *clone() override { return new Listener(*this); }
        Listener *getListener() override { return this; }

        // Sets the 3D audio listener transform - a no-op until an audio
        // backend (Marco 6) exists to receive it.
        void renderListener() {}

    private:
        float mRoll, mDopp, mDist;
    };
}

#endif
