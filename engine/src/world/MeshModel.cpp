#include "engine/MeshModel.h"
#include "engine/Animator.h"

namespace engine
{
    MeshModel::MeshModel() {}

    MeshModel::MeshModel(const MeshModel &t) : Model(t)
    {
        for (size_t k = 0; k < t.mSurfaces.size(); ++k)
        {
            Surface *src = t.mSurfaces[k];
            Surface *dst = new Surface();
            dst->setName(src->getName());
            dst->setBrush(src->getBrush());
            for (int j = 0; j < src->numVertices(); ++j) dst->addVertex(src->getVertex(j));
            for (int j = 0; j < src->numTriangles(); ++j) dst->addTriangle(src->getTriangle(j));
            mSurfaces.push_back(dst);
        }
        mCullBox = t.mCullBox;
        mSurfBones.resize(t.mSurfBones.size());
    }

    MeshModel::~MeshModel()
    {
        delete mCollider;
        for (size_t k = 0; k < mSurfaces.size(); ++k) delete mSurfaces[k];
    }

    Surface *MeshModel::createSurface(const Brush &b)
    {
        Surface *s = new Surface();
        mSurfaces.push_back(s);
        s->setBrush(b);
        ++mGeomChanges;
        ++mBrushChanges;
        return s;
    }

    Surface *MeshModel::findSurface(const Brush &b) const
    {
        for (size_t k = 0; k < mSurfaces.size(); ++k)
        {
            Surface *s = mSurfaces[k];
            if (s->getBrush().getColor() != b.getColor()) continue;
            return s;
        }
        return nullptr;
    }

    void MeshModel::paint(const Brush &b)
    {
        for (size_t k = 0; k < mSurfaces.size(); ++k) mSurfaces[k]->setBrush(b);
        ++mBrushChanges;
    }

    void MeshModel::add(const MeshModel &t)
    {
        if (mCullBox.empty() && !t.mCullBox.empty()) setCullBox(t.mCullBox);

        for (size_t k = 0; k < t.mSurfaces.size(); ++k)
        {
            Surface *src = t.mSurfaces[k];
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
        ++mGeomChanges;
    }

    void MeshModel::transform(const Transform &t)
    {
        blitz::Matrix co = t.m.cofactor();
        for (size_t k = 0; k < mSurfaces.size(); ++k)
        {
            Surface *s = mSurfaces[k];
            for (int j = 0; j < s->numVertices(); ++j)
            {
                const Vector &v = s->getVertex(j).coords;
                const Vector &n = s->getVertex(j).normal;
                s->setCoords(j, t * v);
                s->setNormal(j, co * n);
            }
        }
        ++mGeomChanges;
    }

    void MeshModel::flipTriangles()
    {
        for (size_t k = 0; k < mSurfaces.size(); ++k)
        {
            Surface *s = mSurfaces[k];
            for (int j = 0; j < s->numVertices(); ++j)
                s->setNormal(j, -s->getVertex(j).normal);
            for (int j = 0; j < s->numTriangles(); ++j)
            {
                Surface::Triangle t = s->getTriangle(j);
                std::swap(t.verts[1], t.verts[2]);
                s->setTriangle(j, t);
            }
        }
        ++mGeomChanges;
    }

    void MeshModel::updateNormals()
    {
        if (mNormsValid != mGeomChanges)
        {
            for (size_t k = 0; k < mSurfaces.size(); ++k) mSurfaces[k]->updateNormals();
            mNormsValid = mGeomChanges;
        }
    }

    const Box &MeshModel::getBox() const
    {
        if (mBoxValid != mGeomChanges)
        {
            mBox.clear();
            for (size_t k = 0; k < mSurfaces.size(); ++k)
            {
                Surface *s = mSurfaces[k];
                for (int j = 0; j < s->numVertices(); ++j)
                    mBox.update(s->getVertex(j).coords);
            }
            mBoxValid = mGeomChanges;
        }
        return mBox;
    }

    MeshCollider *MeshModel::getCollider() const
    {
        if (mCollValid != mGeomChanges)
        {
            delete mCollider;
            ct::Vector<MeshCollider::Vertex> verts;
            ct::Vector<MeshCollider::Triangle> tris;
            for (size_t k = 0; k < mSurfaces.size(); ++k)
            {
                Surface *s = mSurfaces[k];
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
            mCollider = new MeshCollider(verts, tris);
            mCollValid = mGeomChanges;
        }
        return mCollider;
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
        ++mBrushChanges;
        Model::setRenderBrush(b);
    }

    void MeshModel::createBones()
    {
        setRenderSpace(RenderSpaceWorld);
        const ct::Vector<Object *> &bones = getAnimator()->getObjects();

        mSurfBones.resize(bones.size());
        mBoneTforms.resize(bones.size());

        for (size_t k = 0; k < bones.size(); ++k)
            mBoneTforms[k] = -bones[k]->getWorldTform();
    }

    bool MeshModel::render(const RenderContext &rc)
    {
        const Box &b = getCullBox();
        if (b.empty()) return false;

        Frustum modelFrustum(rc.getWorldFrustum(), -getRenderTform());
        if (!modelFrustum.cull(b)) return false;

        if (mLocalBrushChanges != mBrushChanges)
        {
            mBrushes.clear();
            for (size_t k = 0; k < mSurfaces.size(); ++k)
            {
                Surface *s = mSurfaces[k];
                mBrushes.push_back(Brush(s->getBrush(), getRenderBrush()));
            }
            mLocalBrushChanges = mBrushChanges;
        }

        if (mSurfBones.empty())
        {
            for (size_t k = 0; k < mSurfaces.size(); ++k)
            {
                Surface *s = mSurfaces[k];
                if (s->numTriangles())
                    enqueue(s, 0, s->numVertices(), 0, s->numTriangles(), mBrushes[k]);
            }
            return false;
        }

        const ct::Vector<Object *> &bones = getAnimator()->getObjects();
        for (size_t k = 0; k < bones.size(); ++k)
        {
            Transform t = bones[k]->getRenderTform() * mBoneTforms[k];
            mSurfBones[k].coordTform = t;
            mSurfBones[k].normalTform = t.m.cofactor();
        }

        bool trans = false;
        for (size_t k = 0; k < mSurfaces.size(); ++k)
        {
            Surface *s = mSurfaces[k];
            if (mBrushes[k].getBlend() == BlendReplace)
            {
                if (s->numTriangles())
                    enqueue(s, 0, s->numVertices(), 0, s->numTriangles(), mBrushes[k]);
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
            for (size_t k = 0; k < mSurfaces.size(); ++k)
            {
                Surface *s = mSurfaces[k];
                if (mBrushes[k].getBlend() != BlendReplace)
                {
                    if (s->numTriangles())
                        enqueue(s, 0, s->numVertices(), 0, s->numTriangles(), mBrushes[k]);
                }
            }
        }
    }

    void MeshModel::uploadQueue(gpu::Device &dev, int type)
    {
        ct::Vector<QueueEntry> &q = queue(type);
        for (size_t k = 0; k < q.size(); ++k)
        {
            if (mSurfBones.empty())
                q[k].surface->ensureGpu(dev);
            else
                q[k].surface->ensureGpuSkinned(dev, mSurfBones);
        }
    }
}
