#ifndef ENGINE_LOADERGLTF_H
#define ENGINE_LOADERGLTF_H

#include "engine/MeshLoader.h"
#include "gpu/GPU.h"

namespace engine
{
    // Loads glTF 2.0 (.gltf JSON + external/embedded buffers, and binary
    // .glb) via the vendored cgltf parser (engine/third_party/cgltf). Not
    // part of the original Blitz3D command set - glTF postdates it - added
    // as a new format alongside LoadMesh/LoadAnimMesh's existing .b3d/.3ds/
    // .x support. See engine/third_party/cgltf/VERSION for the parser's
    // own contract (SDL_RWops file callbacks, accessor unpacking).
    class LoaderGltf : public MeshLoader
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
