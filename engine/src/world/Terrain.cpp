#include "engine/Terrain.h"
#include "engine/TerrainRep.h"
#include "engine/DynamicMesh.h"

namespace engine
{
    Terrain::Terrain(int sizeShift) : mRep(new TerrainRep(sizeShift)) {}

    Terrain::Terrain(const Terrain &other) : Model(other), mRep(new TerrainRep(*other.mRep)) {}

    Terrain::~Terrain()
    {
        delete mRep;
    }

    void Terrain::freeGpu(gpu::Device &dev)
    {
        mRep->freeGpu(dev);
    }

    void Terrain::setDetail(int detail, bool morph)
    {
        mRep->setDetail(detail, morph);
    }

    void Terrain::setHeight(int x, int z, float height, bool realtime)
    {
        if (x >= 0 && z >= 0 && x <= mRep->getSize() && z <= mRep->getSize())
            mRep->setHeight(x, z, height, realtime);
    }

    void Terrain::setShading(bool shading)
    {
        mRep->setShading(shading);
    }

    int Terrain::getSize() const
    {
        return mRep->getSize();
    }

    float Terrain::getHeight(int x, int z) const
    {
        return x >= 0 && z >= 0 && x <= mRep->getSize() && z <= mRep->getSize() ? mRep->getHeight(x, z) : 0.0f;
    }

    float Terrain::heightAtPoint(float x, float z) const
    {
        return mRep->heightAtPoint(x, z);
    }

    bool Terrain::render(const RenderContext &context)
    {
        mRep->render(this, context);
        return false;
    }

    void Terrain::uploadQueue(gpu::Device &dev, int type)
    {
        ct::Vector<QueueEntry> &entries = queue(type);
        for (size_t k = 0; k < entries.size(); ++k)
        {
            if (entries[k].dynamicMesh)
            {
                entries[k].dynamicMesh->upload(dev);
                entries[k].geom = entries[k].dynamicMesh->geometry();
            }
        }
    }

    bool Terrain::collide(const Line &line, float radius, Collision *current, const Transform &transform)
    {
        return mRep->collide(line, radius, current, transform);
    }
}
