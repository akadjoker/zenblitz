#ifndef ENGINE_LOADERB3D_H
#define ENGINE_LOADERB3D_H

#include "engine/MeshLoader.h"
#include "gpu/GPU.h"

namespace engine
{
    class LoaderB3D : public MeshLoader
    {
    public:
  
        MeshModel *load(const ct::String &f, const Transform &conv, int hint) override
        {
            return load(f, conv, hint, nullptr);
        }
        MeshModel *load(const ct::String &f, const Transform &conv, int hint, gpu::Device *dev);
    };
}

#endif
