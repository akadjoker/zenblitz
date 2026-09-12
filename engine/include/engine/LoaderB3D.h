#ifndef ENGINE_LOADERB3D_H
#define ENGINE_LOADERB3D_H

#include "engine/MeshLoader.h"

namespace engine
{
    class LoaderB3D : public MeshLoader
    {
    public:
        MeshModel *load(const std::string &f, const Transform &conv, int hint) override;
    };
}

#endif
