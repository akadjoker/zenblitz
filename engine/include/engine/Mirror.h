#ifndef ENGINE_MIRROR_H
#define ENGINE_MIRROR_H

#include "engine/Object.h"

namespace engine
{
    class Mirror : public Object
    {
    public:
        Entity *clone() override { return new Mirror(*this); }
        Mirror *getMirror() override { return this; }
    };
}

#endif
