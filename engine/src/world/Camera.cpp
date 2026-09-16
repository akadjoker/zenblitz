#include "engine/Camera.h"

namespace engine
{
    Camera::Camera()
    {
        setZoom(1);
        setRange(1, 1000);
        setViewport(0, 0, 0, 0);
        setClsColor(Vector());
        setClsMode(true, true);
        setProjMode(ProjPersp);
        setFogRange(1, 1000);
        setFogColor(Vector());
        setFogMode(0);
    }

    const Frustum &Camera::getFrustum() const
    {
        if (!mLocalValid)
        {
            float ar = mVpW ? (float)mVpH / mVpW : 1.0f;
            mFrustumW = mFrustumNr * 2 / mZoom;
            mFrustumH = mFrustumNr * 2 / mZoom * ar;
            mLocalFrustum = Frustum(mFrustumNr, mFrustumFr, mFrustumW, mFrustumH);
            mLocalValid = true;
        }
        return mLocalFrustum;
    }

    bool Camera::project(const Vector &world, Vector &out) const
    {
        /* Blitz3D cameras look down +Z, so the inverse world transform alone
           puts the point in view space; there x/y scale by the near plane's
           frustum size. This is the exact inverse of CameraPick's maths (and
           of the view-projection World::render feeds the rasteriser), so a
           flare drawn at ProjectedX/Y sits on the thing it was projected
           from. Same shape as runtime3d's own bbCameraProject. */
        const Vector v = -getWorldTform() * world;

        int vpX, vpY, vpW, vpH;
        getViewport(&vpX, &vpY, &vpW, &vpH);
        const float nr = getFrustumNear();
        const float nrW = getFrustumWidth();
        const float nrH = getFrustumHeight();

        if (mProjMode == ProjOrtho)
        {
            out = Vector((v.x / nrW + 0.5f) * vpW, (0.5f - v.y / nrH) * vpH, nr);
            return true;
        }
        if (v.z > 0.0f && v.z <= mFrustumFr)
        {
            out = Vector((v.x * nr / v.z / nrW + 0.5f) * vpW,
                         (0.5f - v.y * nr / v.z / nrH) * vpH, nr);
            return true;
        }
        out = Vector();
        return false;
    }
}
