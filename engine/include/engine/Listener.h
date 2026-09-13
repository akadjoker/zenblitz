#ifndef ENGINE_LISTENER_H
#define ENGINE_LISTENER_H

#include "engine/Object.h"
#include "engine/Sound.h"

namespace engine
{
    class Listener : public Object
    {
    public:
        Listener(float roll, float dopp, float dist) : mRoll(roll), mDopp(dopp), mDist(dist)
        {
            AudioSystem::get().setListenerParams(mRoll, mDopp, mDist);
        }

        Entity *clone() override { return new Listener(*this); }
        Listener *getListener() override { return this; }

        void renderListener()
        {
            AudioSystem &audio = AudioSystem::get();
            audio.setListenerPosition(getWorldTform().v);
            audio.setListenerVelocity(getVelocity());
            audio.setListenerOrientation(getWorldTform().m.k.normalized(), getWorldTform().m.j.normalized());
        }

    private:
        float mRoll, mDopp, mDist;
    };
}

#endif
