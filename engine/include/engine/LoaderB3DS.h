#ifndef ENGINE_LOADERB3DS_H
#define ENGINE_LOADERB3DS_H

#include "engine/MeshLoader.h"
#include "gpu/GPU.h"

namespace engine
{
 
    class LoaderB3DS : public MeshLoader
    {
    public:
        // dev may be null: material textures are then skipped (chunks
        // still parsed, meshes come back untextured).
        MeshModel *load(const ct::String &f, const Transform &conv, int hint) override
        {
            return load(f, conv, hint, nullptr);
        }
        MeshModel *load(const ct::String &f, const Transform &conv, int hint, gpu::Device *dev);
    };
}

#endif
