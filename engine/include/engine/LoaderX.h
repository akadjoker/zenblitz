#ifndef ENGINE_LOADERX_H
#define ENGINE_LOADERX_H

#include "engine/MeshLoader.h"
#include "gpu/GPU.h"

namespace engine
{
    // Loads Microsoft's .x format (text, binary, and MSZIP-compressed),
    // ported from Blitz3D's own loader_x.cpp with the DirectX COM parser
    // it called (IDirectXFile/dxfile.h) replaced by the vendored
    // engine/third_party/xfile parser - same file format, same command
    // set, no COM. See third_party/xfile/VERSION for that parser's
    // contract (template registration order, the indexColor slot, why
    // an object's fields are read by walking its raw bytes rather than
    // by name).
    class LoaderX : public MeshLoader
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
