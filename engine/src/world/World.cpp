#include "engine/World.h"
#include "engine/MeshModel.h"
#include <ct/sort.hpp>
#include <cmath>

namespace engine
{
    void World::enumEnabled()
    {
        mEnabled.clear();
        for (Entity *e = Entity::orphans(); e; e = e->successor())
            e->enumEnabled(mEnabled);
    }

    void World::enumVisible()
    {
        mVisible.clear();
        for (Entity *e = Entity::orphans(); e; e = e->successor())
            e->enumVisible(mVisible);
    }

    ObjCollision *World::allocObjColl(Object *with, const Vector &coords, const Collision &coll)
    {
        ObjCollision *c;
        if (!mFreeColls.empty())
        {
            c = mFreeColls[mFreeColls.size() - 1];
            mFreeColls.pop_back();
        }
        else
        {
            c = new ObjCollision();
        }
        mUsedColls.push_back(c);
        c->with = with;
        c->coords = coords;
        c->collision = coll;
        return c;
    }

    void World::collided(Object *src, Object *dest, const Line &line, const Collision &coll, float yScale)
    {
        Vector coords = line * coll.time - coll.normal * src->getCollisionRadii().x;

        ObjCollision *c = allocObjColl(dest, coords, coll);
        c->coords.y *= yScale;
        src->addCollision(c);

        c = allocObjColl(src, coords, coll);
        c->coords.y *= yScale;
        dest->addCollision(c);
    }

    void World::clearCollisions()
    {
        for (int k = 0; k < kMaxTypes; ++k) mCollInfo[k].clear();
    }

    void World::addCollision(int srcType, int dstType, int method, int response)
    {
        ct::Vector<CollInfo> &info = mCollInfo[srcType];
        for (size_t k = 0; k < info.size(); ++k)
            if (info[k].dstType == dstType) return;

        CollInfo co{dstType, method, response};
        mCollInfo[srcType].push_back(co);
    }

    bool World::hitTest(const Line &line, float radius, Object *obj, const Transform &tf, int method, Collision *currColl)
    {
        switch (method)
        {
        case CollisionMethodSphere:
            return currColl->sphereCollide(line, radius, tf.v, obj->getCollisionRadii().x);
        case CollisionMethodPolygon:
            return obj->collide(line, radius, currColl, tf);
        case CollisionMethodBox:
        {
            Transform t = tf;
            t.m.i.normalize(); t.m.j.normalize(); t.m.k.normalize();
            if (currColl->boxCollide(~t * line, radius, obj->getCollisionBox()))
            {
                currColl->normal = t.m * currColl->normal;
                return true;
            }
            return false;
        }
        }
        return false;
    }

    bool World::checkLOS(Object *src, Object *dest)
    {
        enumEnabled();
        Collision currColl;
        Line line(src->getWorldPosition(), dest->getWorldPosition() - src->getWorldPosition());

        for (size_t k = 0; k < mEnabled.size(); ++k)
        {
            Object *obj = mEnabled[k];
            if (obj == src || obj == dest || !obj->getPickGeometry() || !obj->getObscurer()) continue;
            if (hitTest(line, 0, obj, obj->getWorldTform(), obj->getPickGeometry(), &currColl)) return false;
        }
        return true;
    }

    Object *World::traceRay(const Line &line, float radius, ObjCollision *currColl)
    {
        enumEnabled();
        Object *collObj = nullptr;

        for (size_t k = 0; k < mEnabled.size(); ++k)
        {
            Object *obj = mEnabled[k];
            if (!obj->getPickGeometry()) continue;
            if (hitTest(line, radius, obj, obj->getWorldTform(), obj->getPickGeometry(), &currColl->collision))
                collObj = obj;
        }
        currColl->with = collObj;
        if (collObj)
            currColl->coords = line * currColl->collision.time - currColl->collision.normal * radius;
        return collObj;
    }

    void World::collide(Object *src)
    {
        static const int MAX_HITS = 10;

        Vector dv = src->getWorldTform().v;
        Vector sv = src->getPrevWorldTform().v;

        if (sv == dv)
        {
            if (dv.x != sv.x || dv.y != sv.y || dv.z != sv.z) src->setWorldPosition(sv);
            return;
        }

        Vector panic = sv;
        Transform yTform;

        const Vector &radii = src->getCollisionRadii();
        float radius = radii.x, invYScale;
        float yScale = invYScale = yTform.m.j.y = 1;

        if (radii.x != radii.y)
        {
            yScale = yTform.m.j.y = radius / radii.y;
            invYScale = 1 / yScale;
            sv.y *= yScale;
            dv.y *= yScale;
        }

        int nHit = 0;
        blitz::Plane planes[2];
        Line collLine(sv, dv - sv);
        Vector dir = collLine.d;

        float td = collLine.d.length();
        float tdXz = Vector(collLine.d.x, 0, collLine.d.z).length();

        const ct::Vector<CollInfo> &collinfos = mCollInfo[src->getCollisionType()];

        int hits = 0;
        for (;;)
        {
            Collision coll;
            Object *collObj = nullptr;
            size_t collInfoIdx = 0;

            for (size_t ci = 0; ci < collinfos.size(); ++ci)
            {
                const ct::Vector<Object *> &dstObjs = mObjsByType[collinfos[ci].dstType];
                for (size_t di = 0; di < dstObjs.size(); ++di)
                {
                    Object *dst = dstObjs[di];
                    if (src == dst) continue;

                    const Transform &dstTform = dst->getPrevWorldTform();

                    bool hit = yScale == 1
                        ? hitTest(collLine, radius, dst, dstTform, collinfos[ci].method, &coll)
                        : hitTest(collLine, radius, dst, yTform * dstTform, collinfos[ci].method, &coll);

                    if (hit) { collObj = dst; collInfoIdx = ci; }
                }
            }
            if (!collObj) break;

            if (++hits == MAX_HITS) break;

            collided(src, collObj, collLine, coll, invYScale);

            blitz::Plane collPlane(collLine * coll.time, coll.normal);
            collPlane.d -= COLLISION_EPSILON;
            coll.time = collPlane.t_intersect(collLine);

            if (coll.time > 0)
            {
                sv = collLine * coll.time;
                td *= 1 - coll.time;
                tdXz *= 1 - coll.time;
            }

            const CollInfo &collInfo = collinfos[collInfoIdx];
            if (collInfo.response == CollisionResponseStop)
            {
                dv = sv;
                break;
            }

            Vector nv = collPlane.nearest(dv);

            if (nHit == 0)
            {
                dv = nv;
            }
            else if (nHit == 1)
            {
                if (planes[0].distance(nv) >= 0) { dv = nv; nHit = 0; }
                else if (std::fabs(planes[0].n.dot(collPlane.n)) < 1 - blitz::EPSILON)
                    dv = collPlane.intersect(planes[0]).nearest(dv);
                else
                {
                    hits = MAX_HITS;
                    break;
                }
            }
            else if (planes[0].distance(nv) >= 0 && planes[1].distance(nv) >= 0)
            {
                dv = nv;
                nHit = 0;
            }
            else
            {
                dv = sv;
                break;
            }

            Vector dd(dv - sv);
            if (dd.dot(dir) <= 0) { dv = sv; break; }

            if (collInfo.response == CollisionResponseSlide)
            {
                float d = dd.length();
                if (d <= blitz::EPSILON) { dv = sv; break; }
                if (d > td) dd *= td / d;
            }
            else if (collInfo.response == CollisionResponseSlideXZ)
            {
                float d = Vector(dd.x, 0, dd.z).length();
                if (d <= blitz::EPSILON) { dv = sv; break; }
                if (d > tdXz) dd *= tdXz / d;
            }

            collLine.o = sv;
            collLine.d = dd;
            dv = sv + dd;
            planes[nHit++] = collPlane;
        }

        if (hits)
        {
            if (hits < MAX_HITS)
            {
                dv.y *= invYScale;
                src->setWorldPosition(dv);
            }
            else
            {
                src->setWorldPosition(panic);
            }
        }
    }

    void World::update(float elapsed)
    {
        for (; !mUsedColls.empty(); mUsedColls.pop_back())
            mFreeColls.push_back(mUsedColls[mUsedColls.size() - 1]);

        enumEnabled();

        for (size_t k = 0; k < mEnabled.size(); ++k)
        {
            Object *o = mEnabled[k];
            if (int n = o->getCollisionType()) mObjsByType[n].push_back(o);
        }

        for (size_t k = 0; k < mEnabled.size(); ++k)
        {
            Object *o = mEnabled[k];
            o->beginUpdate(elapsed);
            if (o->getCollisionType()) collide(o);
            o->endUpdate();
        }

        for (int k = 0; k < kMaxTypes; ++k) mObjsByType[k].clear();
    }

    void World::capture()
    {
        enumVisible();
        for (size_t k = 0; k < mVisible.size(); ++k) mVisible[k]->capture();
    }

    void World::prepare(gpu::Device &dev, float tween)
    {
        mOrdMods.clear();
        mUnordMods.clear();
        mVisible.clear();
        mLights.clear();
        mMirrors.clear();
        mListeners.clear();
        mCameras.clear();
        mDrawCalls.clear();

        enumVisible();

        for (size_t k = 0; k < mVisible.size(); ++k)
        {
            Object *o = mVisible[k];
            if (!o->beginRender(tween)) continue;

            if (Light *t = o->getLight()) mLights.push_back(t);
            else if (Camera *t = o->getCamera()) mCameras.push_back(t);
            else if (Mirror *t = o->getMirror()) mMirrors.push_back(t);
            else if (Listener *t = o->getListener()) mListeners.push_back(t);
            else if (Model *t = o->getModel())
            {
                if (t->getOrder()) mOrdMods.push_back(t);
                else mUnordMods.push_back(t);
            }
        }

        ct::sort(mOrdMods.begin(), mOrdMods.end(),
                 [](Model *a, Model *b) { return a->getOrder() < b->getOrder(); });

        mRenderer.setLights(mLights);
        mRenderer.beginFrame();

        for (size_t k = 0; k < mCameras.size(); ++k)
        {
            Camera *cam = mCameras[k];

            // Camera::beginRenderFrame in the original: clear this camera's
            // viewport (colour and/or depth per CameraClsMode) once, before
            // its mirrors and main pass
            int clear = (cam->getClsColorEnabled() ? 1 : 0) | (cam->getClsZEnabled() ? 2 : 0);
            if (clear)
            {
                mRenderer.prepareClear(cam->getClsColor());
                DrawCall dc;
                cam->getViewport(&dc.vpX, &dc.vpY, &dc.vpW, &dc.vpH);
                dc.boneSlot = -1;
                dc.clear = clear;
                mDrawCalls.push_back(dc);
            }

            for (size_t mi = 0; mi < mMirrors.size(); ++mi)
                render(cam, mMirrors[mi], dev);

            render(cam, nullptr, dev);
        }

        mRenderer.flushUniforms(dev);

        for (size_t k = 0; k < mListeners.size(); ++k) mListeners[k]->renderListener();
    }

    void World::render(Camera *cam, Mirror *mirror, gpu::Device &dev)
    {
        if (mirror)
        {
            const Transform &t = mirror->getRenderTform();
            mCamTform = t * Transform(blitz::Matrix(Vector(1, 0, 0), Vector(0, -1, 0), Vector(0, 0, 1))) * -t * cam->getRenderTform();
        }
        else
        {
            mCamTform = cam->getRenderTform();
        }

        const Transform &invCam = -mCamTform;
        // Blitz3D's camera looks down +Z; Matrix4::perspective/ortho assume
        // the OpenGL -Z-forward convention. Flip Z once here rather than
        // rederiving every projection formula for +Z.
        Matrix4 flipZ = Matrix4::scale(Vector(1, 1, -1));
        Matrix4 view = flipZ * Matrix4::fromBlitz(invCam);
        int vpX, vpY, vpW, vpH;
        cam->getViewport(&vpX, &vpY, &vpW, &vpH);
        float aspect = vpH > 0 ? (float)vpW / (float)vpH : 1.0f;
        Matrix4 proj = cam->getProjMode() == Camera::ProjOrtho
            ? Matrix4::ortho(-cam->getFrustumWidth() * 0.5f, cam->getFrustumWidth() * 0.5f,
                              -cam->getFrustumHeight() * 0.5f, cam->getFrustumHeight() * 0.5f,
                              cam->getFrustumNear(), cam->getFrustumFar())
            : Matrix4::perspective(2.0f * std::atan(cam->getFrustumHeight() * 0.5f / cam->getFrustumNear()) * 180.0f / blitz::PI,
                                    aspect, cam->getFrustumNear(), cam->getFrustumFar());

        mPendingCamera.viewProjection = proj * view;
        mPendingCamera.ambient = mAmbient;
        mPendingCamera.fogMode = cam->getFogMode();
        mPendingCamera.fogColor = cam->getFogColor();
        mPendingCamera.fogNear = cam->getFogNear();
        mPendingCamera.fogFar = cam->getFogFar();
        mPendingCamera.vpX = vpX; mPendingCamera.vpY = vpY;
        mPendingCamera.vpW = vpW; mPendingCamera.vpH = vpH;

        RenderContext rc(mCamTform, cam->getFrustum(), mirror != nullptr);

        size_t ord = 0;
        while (ord < mOrdMods.size() && mOrdMods[ord]->getOrder() > 0)
        {
            Model *mod = mOrdMods[ord++];
            if (!mod->doAutoFade(mCamTform.v)) continue;
            render(mod, rc, dev);
            flushTransparent(dev);
        }

        for (size_t k = 0; k < mUnordMods.size(); ++k)
        {
            Model *mod = mUnordMods[k];
            if (!mod->doAutoFade(mCamTform.v)) continue;
            render(mod, rc, dev);
        }
        flushTransparent(dev);

        while (ord < mOrdMods.size())
        {
            Model *mod = mOrdMods[ord++];
            if (!mod->doAutoFade(mCamTform.v)) continue;
            render(mod, rc, dev);
            flushTransparent(dev);
        }
    }

    void World::enqueueModelQueue(Model *mod, int queueType, gpu::Device &dev)
    {
        mod->uploadQueue(dev, queueType);

        Matrix4 model = mod->getRenderSpace() == Model::RenderSpaceLocal
            ? Matrix4::fromBlitz(mod->getRenderTform())
            : Matrix4::identity();

        mRenderer.setViewProjection(mPendingCamera.viewProjection);
        mRenderer.setAmbient(mPendingCamera.ambient);
        mRenderer.setFog(mPendingCamera.fogMode, mPendingCamera.fogColor, mPendingCamera.fogNear, mPendingCamera.fogFar);

        ct::Vector<Model::QueueEntry> &q = mod->queue(queueType);
        for (size_t k = 0; k < q.size(); ++k)
        {
            mRenderer.prepare(q[k].brush, model, q[k].geom.morph);

            DrawCall dc;
            dc.geom = q[k].geom;
            dc.brush = q[k].brush;
            dc.vpX = mPendingCamera.vpX; dc.vpY = mPendingCamera.vpY;
            dc.vpW = mPendingCamera.vpW; dc.vpH = mPendingCamera.vpH;
            dc.boneSlot = mod->boneSlot();
            dc.clear = 0;
            mDrawCalls.push_back(dc);
        }
        q.clear();
    }

    void World::render(Model *mod, const RenderContext &rc, gpu::Device &dev)
    {
        bool trans = mod->render(rc);

        // one bone block per skinned model per camera pass; the slot rides
        // on the model so the transparent queue (flushed later) finds it
        int boneCount = mod->gpuBoneCount();
        mod->setBoneSlot(boneCount > 0 ? mRenderer.stageBones(mod->gpuBoneMatrices(), boneCount) : -1);

        if (mod->queueSize(Model::QueueOpaque))
            enqueueModelQueue(mod, Model::QueueOpaque, dev);

        if (trans || mod->queueSize(Model::QueueTransparent))
            mTransparents.push_back(mod);
    }

    void World::flushTransparent(gpu::Device &dev)
    {
        for (size_t k = 0; k < mTransparents.size(); ++k)
        {
            Model *mod = mTransparents[k];
            mod->renderQueue(Model::QueueTransparent);
            if (mod->queueSize(Model::QueueTransparent))
                enqueueModelQueue(mod, Model::QueueTransparent, dev);
        }
        mTransparents.clear();
    }

    void World::draw(gpu::Device &dev)
    {
        int lastVpX = -1, lastVpY = -1, lastVpW = -1, lastVpH = -1;
        for (size_t k = 0; k < mDrawCalls.size(); ++k)
        {
            const DrawCall &dc = mDrawCalls[k];
            if (dc.vpX != lastVpX || dc.vpY != lastVpY || dc.vpW != lastVpW || dc.vpH != lastVpH)
            {
                gpu::Viewport vp;
                vp.x = (float)dc.vpX; vp.y = (float)dc.vpY;
                vp.width = (float)dc.vpW; vp.height = (float)dc.vpH;
                dev.setViewport(vp);
                lastVpX = dc.vpX; lastVpY = dc.vpY; lastVpW = dc.vpW; lastVpH = dc.vpH;
            }
            if (dc.clear)
                mRenderer.drawClear(dev, (int)k, (dc.clear & 1) != 0, (dc.clear & 2) != 0);
            else
                mRenderer.draw(dev, (int)k, dc.geom, dc.brush, dc.boneSlot);
        }
    }
}
