#ifndef ENGINE_PLANEMODEL_H
#define ENGINE_PLANEMODEL_H

#include "engine/DynamicMesh.h"
#include "engine/Model.h"

namespace engine
{
    // Ported from Blitz3D's planemodel.cpp. CreatePlane makes a flat,
    // *infinite* ground (the docs' own words), not a quad: there is no
    // mesh to size, because the geometry is rebuilt every frame from the
    // camera frustum's far corners projected down onto y=0 and clipped
    // against it, so the plane always reaches exactly as far as the view
    // does and never shows an edge.
    class PlaneModel : public Model
    {
    public:
        explicit PlaneModel(int subDivs);
        PlaneModel(const PlaneModel &other);
        ~PlaneModel();

        Entity *clone() override { return new PlaneModel(*this); }

        void freeGpu(gpu::Device &dev) override;
        // The per-frame geometry lives in a DynamicMesh, which has to be
        // pushed to the GPU after render() fills it - same override
        // Terrain needs for the same reason.
        void uploadQueue(gpu::Device &dev, int type) override;

        bool render(const RenderContext &context) override;
        bool collide(const blitz::Line &line, float radius, Collision *curr, const Transform &tform) override;

        Plane getRenderPlane() const;

    private:
        struct Rep
        {
            int refCount = 1;
            int subDivs = 1;
            DynamicMesh mesh;
        };
        Rep *mRep;
    };
}

#endif
