#ifndef ENGINE_CSG_H
#define ENGINE_CSG_H

namespace engine
{
    class MeshModel;

    namespace CSG
    {
        enum Method
        {
            Subtract = 0,
            Union = 1,
            Intersect = 2,
        };

        MeshModel *meshCSG(const MeshModel &a, const MeshModel &b, int method = Union);
    }
}

#endif
