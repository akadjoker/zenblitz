#ifndef ENGINE_TERRAIN_H
#define ENGINE_TERRAIN_H

#include "engine/Model.h"

namespace engine
{
    class TerrainRep;

    class Terrain : public Model
    {
    public:
        explicit Terrain(int sizeShift);
        Terrain(const Terrain &other);
        ~Terrain();

        Terrain *getTerrain() override { return this; }
        Entity *clone() override { return new Terrain(*this); }

        void freeGpu(gpu::Device &dev) override;
        void setDetail(int detail, bool morph);
        void setHeight(int x, int z, float height, bool realtime);
        void setShading(bool shading);

        int getSize() const;
        float getHeight(int x, int z) const;
        float heightAtPoint(float x, float z) const;

        bool render(const RenderContext &rc) override;
        void uploadQueue(gpu::Device &dev, int type) override;
        bool collide(const Line &line, float radius, Collision *current, const Transform &transform) override;

    private:
        TerrainRep *mRep;
    };
}

#endif
