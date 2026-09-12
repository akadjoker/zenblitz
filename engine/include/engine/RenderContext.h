#ifndef ENGINE_RENDERCONTEXT_H
#define ENGINE_RENDERCONTEXT_H

#include "engine/Frustum.h"

namespace engine
{
    class RenderContext
    {
    public:
        RenderContext(const Transform &t, const Frustum &f, bool r)
            : mCameraTform(t), mCameraFrustum(f), mWorldFrustum(f, t), mRef(r) {}

        bool isReflected() const { return mRef; }
        const Transform &getCameraTform() const { return mCameraTform; }
        const Frustum &getWorldFrustum() const { return mWorldFrustum; }
        const Frustum &getCameraFrustum() const { return mCameraFrustum; }

    private:
        Transform mCameraTform;
        Frustum mCameraFrustum, mWorldFrustum;
        bool mRef;
    };
}

#endif
