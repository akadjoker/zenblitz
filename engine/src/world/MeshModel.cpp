#include "engine/MeshModel.h"
#include "engine/Animator.h"
#include <utility>

namespace engine
{
    MeshModel::MeshModel() : mRep(new Rep()) {}

    MeshModel::MeshModel(const MeshModel &t) : Model(t), mRep(t.mRep)
    {
        ++mRep->refCount;
        mSurfBones.resize(t.mSurfBones.size());
    }

    MeshModel::~MeshModel()
    {
        if (!--mRep->refCount) delete mRep;
    }

    void MeshModel::freeGpu(gpu::Device &dev)
    {
        // The Rep (and so every Surface's vertex/index buffer) is shared
        // by every model cloned from it, so only the last one may free
        // them - same rule MD2Model::freeGpu already follows. Without
        // this, freeing one CopyEntity() clone destroys buffers its
        // siblings are still binding, and the next frame's draw fails
        // with "invalid resource handle" (castle.bb, which frees a
        // bullet/spark copy every few frames).
        if (mRep->refCount != 1) return;
        for (size_t k = 0; k < mRep->surfaces.size(); ++k) mRep->surfaces[k]->freeGpu(dev);
    }

    Surface *MeshModel::createSurface(const Brush &b)
    {
        Surface *s = new Surface(&mRep->mon);
        mRep->surfaces.push_back(s);
        s->setBrush(b);
        ++mRep->geomChanges;
        ++mRep->brushChanges;
        return s;
    }

    Surface *MeshModel::findSurface(const Brush &b) const
    {
        for (size_t k = 0; k < mRep->surfaces.size(); ++k)
        {
            Surface *s = mRep->surfaces[k];
            if (!s->getBrush().sameAs(b)) continue;
            return s;
        }
        return nullptr;
    }

    void MeshModel::paint(const Brush &b)
    {
        for (size_t k = 0; k < mRep->surfaces.size(); ++k) mRep->surfaces[k]->setBrush(b);
        ++mRep->brushChanges;
    }

    void MeshModel::add(const MeshModel &t)
    {
        if (mRep->cullBox.empty() && !t.mRep->cullBox.empty()) setCullBox(t.mRep->cullBox);

        for (size_t k = 0; k < t.mRep->surfaces.size(); ++k)
        {
            Surface *src = t.mRep->surfaces[k];
            Surface *dst = findSurface(src->getBrush());
            if (!dst) dst = createSurface(src->getBrush());
            int base = dst->numVertices();
            for (int j = 0; j < src->numTriangles(); ++j)
            {
                Surface::Triangle tri = src->getTriangle(j);
                tri.verts[0] += (unsigned short)base;
                tri.verts[1] += (unsigned short)base;
                tri.verts[2] += (unsigned short)base;
                dst->addTriangle(tri);
            }
            for (int j = 0; j < src->numVertices(); ++j) dst->addVertex(src->getVertex(j));
        }
        ++mRep->geomChanges;
    }

    void MeshModel::transform(const Transform &t)
    {
        blitz::Matrix co = t.m.cofactor();
        for (size_t k = 0; k < mRep->surfaces.size(); ++k)
        {
            Surface *s = mRep->surfaces[k];
            for (int j = 0; j < s->numVertices(); ++j)
            {
                const Vector &v = s->getVertex(j).coords;
                const Vector &n = s->getVertex(j).normal;
                s->setCoords(j, t * v);
                s->setNormal(j, co * n);
            }
        }
        ++mRep->geomChanges;
    }

    void MeshModel::flipTriangles()
    {
        for (size_t k = 0; k < mRep->surfaces.size(); ++k)
        {
            Surface *s = mRep->surfaces[k];
            for (int j = 0; j < s->numVertices(); ++j)
                s->setNormal(j, -s->getVertex(j).normal);
            for (int j = 0; j < s->numTriangles(); ++j)
            {
                Surface::Triangle t = s->getTriangle(j);
                std::swap(t.verts[1], t.verts[2]);
                s->setTriangle(j, t);
            }
        }
        ++mRep->geomChanges;
    }

    void MeshModel::updateNormals()
    {
        if (mRep->normsValid != mRep->geomChanges)
        {
            for (size_t k = 0; k < mRep->surfaces.size(); ++k) mRep->surfaces[k]->updateNormals();
            mRep->normsValid = mRep->geomChanges;
        }
    }

    const Box &MeshModel::getBox() const
    {
        if (mRep->boxValid != mRep->geomChanges)
        {
            mRep->box.clear();
            for (size_t k = 0; k < mRep->surfaces.size(); ++k)
            {
                Surface *s = mRep->surfaces[k];
                for (int j = 0; j < s->numVertices(); ++j)
                    mRep->box.update(s->getVertex(j).coords);
            }
            mRep->boxValid = mRep->geomChanges;
        }
        return mRep->box;
    }

    MeshCollider *MeshModel::getCollider() const
    {
        if (mRep->collValid != mRep->geomChanges)
        {
            delete mRep->collider;
            ct::Vector<MeshCollider::Vertex> verts;
            ct::Vector<MeshCollider::Triangle> tris;
            for (size_t k = 0; k < mRep->surfaces.size(); ++k)
            {
                Surface *s = mRep->surfaces[k];
                for (int j = 0; j < s->numTriangles(); ++j)
                {
                    MeshCollider::Triangle q;
                    q.verts[0] = s->getTriangle(j).verts[0] + (int)verts.size();
                    q.verts[1] = s->getTriangle(j).verts[1] + (int)verts.size();
                    q.verts[2] = s->getTriangle(j).verts[2] + (int)verts.size();
                    q.surface = s;
                    q.index = j;
                    tris.push_back(q);
                }
                for (int j = 0; j < s->numVertices(); ++j)
                {
                    MeshCollider::Vertex q;
                    q.coords = s->getVertex(j).coords;
                    verts.push_back(q);
                }
            }
            mRep->collider = new MeshCollider(verts, tris);
            mRep->collValid = mRep->geomChanges;
        }
        return mRep->collider;
    }

    bool MeshModel::collide(const Line &line, float radius, Collision *currColl, const Transform &t)
    {
        return getCollider()->collide(line, radius, currColl, t);
    }

    bool MeshModel::intersects(const MeshModel &m) const
    {
        return getCollider()->intersects(*m.getCollider(), -m.getWorldTform() * getWorldTform());
    }

    void MeshModel::setRenderBrush(const Brush &b)
    {
        ++mRep->brushChanges;
        Model::setRenderBrush(b);
    }

    void MeshModel::createBones()
    {
        setRenderSpace(RenderSpaceWorld);
        const ct::Vector<Object *> &bones = getAnimator()->getObjects();

        mSurfBones.resize(bones.size());
        mRep->boneTforms.resize(bones.size());

        for (size_t k = 0; k < bones.size(); ++k)
            mRep->boneTforms[k] = -bones[k]->getWorldTform();
    }

    bool MeshModel::render(const RenderContext &rc)
    {
        const Box &b = getCullBox();
        if (b.empty()) return false;

        Frustum modelFrustum(rc.getWorldFrustum(), -getRenderTform());
        if (!modelFrustum.cull(b)) return false;

        if (mLocalBrushChanges != mRep->brushChanges)
        {
            mBrushes.clear();
            for (size_t k = 0; k < mRep->surfaces.size(); ++k)
            {
                Surface *s = mRep->surfaces[k];
                mBrushes.push_back(Brush(s->getBrush(), getRenderBrush()));
            }
            mLocalBrushChanges = mRep->brushChanges;
        }

        if (mSurfBones.empty())
        {
            for (size_t k = 0; k < mRep->surfaces.size(); ++k)
            {
                Surface *s = mRep->surfaces[k];
                if (s->numTriangles())
                    enqueue(s, mBrushes[k]);
            }
            return false;
        }

        const ct::Vector<Object *> &bones = getAnimator()->getObjects();
        mBoneMats.resize(bones.size());
        for (size_t k = 0; k < bones.size(); ++k)
        {
            Transform t = bones[k]->getRenderTform() * mRep->boneTforms[k];
            mSurfBones[k].coordTform = t;
            mSurfBones[k].normalTform = t.m.cofactor();
            mBoneMats[k] = Matrix4::fromBlitz(t);
        }

        bool trans = false;
        for (size_t k = 0; k < mRep->surfaces.size(); ++k)
        {
            Surface *s = mRep->surfaces[k];
            if (mBrushes[k].getBlend() == BlendReplace)
            {
                if (s->numTriangles())
                    enqueue(s, mBrushes[k]);
            }
            else
            {
                trans = true;
            }
        }
        return trans;
    }

    void MeshModel::renderQueue(int type)
    {
        if (type == QueueTransparent && !mSurfBones.empty())
        {
            for (size_t k = 0; k < mRep->surfaces.size(); ++k)
            {
                Surface *s = mRep->surfaces[k];
                if (mBrushes[k].getBlend() != BlendReplace)
                {
                    if (s->numTriangles())
                        enqueue(s, mBrushes[k]);
                }
            }
        }
    }

    void MeshModel::uploadQueue(gpu::Device &dev, int type)
    {
        ct::Vector<QueueEntry> &q = queue(type);
        for (size_t k = 0; k < q.size(); ++k)
        {
            if (mSurfBones.empty() || gpuSkinned())
                q[k].surface->ensureGpu(dev);
            else
                q[k].surface->ensureGpuSkinned(dev, mSurfBones);
            q[k].geom = q[k].surface->geometry();
        }
    }
}
