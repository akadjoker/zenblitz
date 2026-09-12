#ifndef ENGINE_CAMERA_H
#define ENGINE_CAMERA_H

#include "engine/Object.h"
#include "engine/Frustum.h"

namespace engine
{
    class Camera : public Object
    {
    public:
        enum
        {
            ProjNone = 0, ProjPersp = 1, ProjOrtho = 2
        };

        Camera();
        Camera *getCamera() override { return this; }
        Entity *clone() override { return new Camera(*this); }

        void setZoom(float z) { mZoom = z; mLocalValid = false; }
        void setRange(float nr, float fr) { mFrustumNr = nr; mFrustumFr = fr; mLocalValid = false; }
        void setViewport(int x, int y, int w, int h) { mVpX = x; mVpY = y; mVpW = w; mVpH = h; mLocalValid = false; }
        void setClsColor(const Vector &v) { mClsColor = v; }
        void setClsMode(bool clsArgb, bool clsZ) { mClsArgb = clsArgb; mClsZ = clsZ; }
        void setProjMode(int mode) { mProjMode = mode; }
        void setFogColor(const Vector &v) { mFogColor = v; }
        void setFogRange(float nr, float fr) { mFogNr = nr; mFogFr = fr; }
        void setFogMode(int mode) { mFogMode = mode; }

        float getFrustumNear() const { return mFrustumNr; }
        float getFrustumFar() const { return mFrustumFr; }
        float getFrustumWidth() const { getFrustum(); return mFrustumW; }
        float getFrustumHeight() const { getFrustum(); return mFrustumH; }
        const Frustum &getFrustum() const;
        void getViewport(int *x, int *y, int *w, int *h) const { *x = mVpX; *y = mVpY; *w = mVpW; *h = mVpH; }
        int getProjMode() const { return mProjMode; }
        float getZoom() const { return mZoom; }
        const Vector &getClsColor() const { return mClsColor; }
        bool getClsColorEnabled() const { return mClsArgb; }
        bool getClsZEnabled() const { return mClsZ; }
        const Vector &getFogColor() const { return mFogColor; }
        float getFogNear() const { return mFogNr; }
        float getFogFar() const { return mFogFr; }
        int getFogMode() const { return mFogMode; }

    private:
        float mZoom = 1.0f;
        int mVpX = 0, mVpY = 0, mVpW = 0, mVpH = 0;
        Vector mClsColor;
        bool mClsArgb = true, mClsZ = true;
        int mProjMode = ProjPersp;
        Vector mFogColor;
        float mFogNr = 1.0f, mFogFr = 1000.0f;
        int mFogMode = 0;
        float mFrustumNr = 1.0f, mFrustumFr = 1000.0f;
        mutable float mFrustumW = 0, mFrustumH = 0;
        mutable Frustum mLocalFrustum;
        mutable bool mLocalValid = false;
    };
}

#endif
