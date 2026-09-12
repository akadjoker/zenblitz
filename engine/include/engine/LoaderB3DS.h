#ifndef ENGINE_LOADERB3DS_H
#define ENGINE_LOADERB3DS_H

#include "engine/MeshLoader.h"
#include "gpu/GPU.h"

namespace engine
{
    // Ported from the original Blitz3D loader_3ds.cpp/.h (Loader_3DS) -
    // reads the Autodesk/3D Studio .3ds chunk format: mesh geometry,
    // materials, and a keyframer track for hierarchy + animation.
    class LoaderB3DS : public MeshLoader
    {
    public:
        // dev may be null: material textures are then skipped (chunks
        // still parsed, meshes come back untextured).
        MeshModel *load(const std::string &f, const Transform &conv, int hint) override
        {
            return load(f, conv, hint, nullptr);
        }
        MeshModel *load(const std::string &f, const Transform &conv, int hint, gpu::Device *dev);
    };
}

#endif
