#ifndef ENGINE_WORLD_H
#define ENGINE_WORLD_H

#include "engine/Model.h"
#include "engine/Camera.h"
#include "engine/Light.h"
#include "engine/Mirror.h"
#include "engine/Listener.h"
#include "engine/MeshRenderer.h"
#include "engine/RenderContext.h"
#include <ct/vector.hpp>

namespace engine
{
    class World
    {
    public:
        enum
        {
            CollisionMethodSphere = 1, CollisionMethodPolygon = 2, CollisionMethodBox = 3
        };
        enum
        {
            CollisionResponseNone = 0, CollisionResponseStop = 1,
            CollisionResponseSlide = 2, CollisionResponseSlideXZ = 3
        };

        struct DrawCall
        {
            Surface *surface;
            Brush brush;
            int vpX, vpY, vpW, vpH;
        };

        bool init(gpu::Device &dev) { return mRenderer.init(dev); }
        void shutdown() { mRenderer.shutdown(); }

        void clearCollisions();
        void addCollision(int srcType, int dstType, int method, int response);

        void update(float elapsed);
        void capture();

        // Phase 1: culling, GPU uploads (Surface::ensureGpu) and uniform
        // staging - call with no render pass open. Fills the draw call
        // list for every visible camera.
        void prepare(gpu::Device &dev, float tween);
        // Phase 2: issues the draw calls prepare() collected - call inside
        // the render pass.
        void draw(gpu::Device &dev);

        bool checkLOS(Object *src, Object *dest);
        bool hitTest(const Line &line, float radius, Object *obj, const Transform &tf, int method, Collision *currColl);
        Object *traceRay(const Line &line, float radius, ObjCollision *currColl);

    private:
        struct CollInfo
        {
            int dstType, method, response;
        };

        static constexpr int kMaxTypes = 1000;
        ct::Vector<CollInfo> mCollInfo[kMaxTypes];
        ct::Vector<Object *> mObjsByType[kMaxTypes];

        ct::Vector<ObjCollision *> mFreeColls, mUsedColls;

        MeshRenderer mRenderer;
        Transform mCamTform;

        ct::Vector<Model *> mOrdMods, mUnordMods;
        ct::Vector<Camera *> mCameras;
        ct::Vector<Light *> mLights;
        ct::Vector<Mirror *> mMirrors;
        ct::Vector<Listener *> mListeners;
        ct::Vector<Model *> mTransparents;
        ct::Vector<Object *> mEnabled, mVisible;
        ct::Vector<DrawCall> mDrawCalls;

        struct PendingCamera
        {
            Matrix4 viewProjection;
            int fogMode; Vector fogColor; float fogNear, fogFar;
            Vector ambient;
            int vpX, vpY, vpW, vpH;
        };
        PendingCamera mPendingCamera;

        void collide(Object *src);
        void enumEnabled();
        void enumVisible();

        void render(Camera *cam, Mirror *mirror, gpu::Device &dev);
        void render(Model *mod, const RenderContext &rc, gpu::Device &dev);
        void flushTransparent(gpu::Device &dev);
        void enqueueModelQueue(Model *mod, int queueType, gpu::Device &dev);

        ObjCollision *allocObjColl(Object *with, const Vector &coords, const Collision &coll);
        void collided(Object *src, Object *dest, const Line &line, const Collision &coll, float yScale);
    };
}

#endif
