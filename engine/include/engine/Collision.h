#ifndef ENGINE_COLLISION_H
#define ENGINE_COLLISION_H

#include "engine/Geom.h"

namespace engine
{
    using blitz::Vector;
    using blitz::Line;
    using blitz::Box;

    extern const float COLLISION_EPSILON;

    struct Collision
    {
        float time = 1.0f;
        Vector normal;
        void *surface = nullptr;
        unsigned short index = (unsigned short)~0;

        bool update(const Line &line, float t, const Vector &n);

        bool sphereCollide(const Line &line, float radius, const Vector &dest, float destRadius);
        bool triangleCollide(const Line &line, float radius, const Vector &v0, const Vector &v1, const Vector &v2);
        bool boxCollide(const Line &line, float radius, const Box &box);
    };
}

#endif
