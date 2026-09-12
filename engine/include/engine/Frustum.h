#ifndef ENGINE_FRUSTUM_H
#define ENGINE_FRUSTUM_H

#include "engine/Geom.h"

namespace engine
{
    using blitz::Vector;
    using blitz::Plane;
    using blitz::Box;
    using blitz::Transform;

    class Frustum
    {
    public:
        enum
        {
            VertTLNear = 0, VertTRNear, VertBRNear, VertBLNear,
            VertTLFar, VertTRFar, VertBRFar, VertBLFar, VertEye
        };
        enum
        {
            PlaneTop = 0, PlaneLeft, PlaneBottom, PlaneRight, PlaneNear, PlaneFar
        };

        Frustum() {}
        Frustum(float nr, float fr, float w, float h);
        Frustum(const Frustum &f, const Transform &t);

        bool cull(const Box &box) const;
        bool cull(const Vector vecs[], int cnt) const;

        const Plane &getPlane(int n) const { return mPlanes[n]; }
        const Vector &getVertex(int n) const { return mVerts[n]; }

    private:
        Plane mPlanes[6];
        Vector mVerts[9];
        void makePlanes();
    };
}

#endif
