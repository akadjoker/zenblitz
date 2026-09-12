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
}
