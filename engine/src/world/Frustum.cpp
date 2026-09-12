#include "engine/Frustum.h"

namespace engine
{
    Frustum::Frustum(float nr, float fr, float w, float h)
    {
        mVerts[VertTLNear] = Vector(w * -.5f, h * +.5f, nr);
        mVerts[VertTRNear] = Vector(w * +.5f, h * +.5f, nr);
        mVerts[VertBRNear] = Vector(w * +.5f, h * -.5f, nr);
        mVerts[VertBLNear] = Vector(w * -.5f, h * -.5f, nr);
        float t = fr / nr;
        mVerts[VertTLFar] = mVerts[VertTLNear] * t;
        mVerts[VertTRFar] = mVerts[VertTRNear] * t;
        mVerts[VertBRFar] = mVerts[VertBRNear] * t;
        mVerts[VertBLFar] = mVerts[VertBLNear] * t;
        mVerts[VertEye] = Vector();
        makePlanes();
    }

    Frustum::Frustum(const Frustum &f, const Transform &t)
    {
        for (int k = 0; k < 9; ++k)
            mVerts[k] = t * f.mVerts[k];
        makePlanes();
    }

    bool Frustum::cull(const Vector v[], int cnt) const
    {
        for (int n = 0; n < 6; ++n)
        {
            int k;
            for (k = 0; k < cnt && mPlanes[n].distance(v[k]) < 0; ++k) {}
            if (k == cnt) return false;
        }
        return true;
    }

    bool Frustum::cull(const Box &b) const
    {
        Vector v[8];
        for (int k = 0; k < 8; ++k) v[k] = b.corner(k);
        return cull(v, 8);
    }

    void Frustum::makePlanes()
    {
        mPlanes[PlaneTop] = Plane(mVerts[VertEye], mVerts[VertTRFar], mVerts[VertTLFar]);
        mPlanes[PlaneLeft] = Plane(mVerts[VertEye], mVerts[VertTLFar], mVerts[VertBLFar]);
        mPlanes[PlaneBottom] = Plane(mVerts[VertEye], mVerts[VertBLFar], mVerts[VertBRFar]);
        mPlanes[PlaneRight] = Plane(mVerts[VertEye], mVerts[VertBRFar], mVerts[VertTRFar]);
        mPlanes[PlaneNear] = Plane(mVerts[VertTRNear], mVerts[VertTLNear], mVerts[VertBLNear]);
        mPlanes[PlaneFar] = Plane(mVerts[VertTLFar], mVerts[VertTRFar], mVerts[VertBRFar]);
        if (mPlanes[PlaneNear].distance(mVerts[VertEye]) > 0)
        {
            for (int k = 0; k < 6; ++k) mPlanes[k] = -mPlanes[k];
        }
    }
}
