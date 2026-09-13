#ifndef ENGINE_LOADERFBX_H
#define ENGINE_LOADERFBX_H

#include "engine/MeshLoader.h"
#include "gpu/GPU.h"

namespace engine
{
    // Loads Autodesk FBX (text and binary, including MZ-deflate-compressed
    // binary geometry blocks) via the vendored OpenFBX parser
    // (engine/third_party/ofbx). Not part of the original Blitz3D command
    // set - added as a new format alongside .b3d/.3ds/.x/.gltf/.glb. See
    // engine/third_party/ofbx/VERSION for the parser's own contract
    // (one vertex per face-corner, the ~i polygon-boundary marker,
    // unnormalized/uncapped skin weights).
    class LoaderFbx : public MeshLoader
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
