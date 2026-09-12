#ifndef ENGINE_MESHUTIL_H
#define ENGINE_MESHUTIL_H

#include "engine/MeshModel.h"

namespace engine
{
    namespace MeshUtil
    {
        MeshModel *createCube(const Brush &b);
        MeshModel *createSphere(const Brush &b, int segs);
        MeshModel *createCylinder(const Brush &b, int segs, bool solid);
        MeshModel *createCone(const Brush &b, int segs, bool solid);
        void lightMesh(MeshModel *m, const Vector &pos, const Vector &rgb, float range);
        // ScaleMesh/FitMesh: unlike ScaleEntity, these edit the mesh's own
        // vertex data (MeshModel::transform), not the entity's transform.
        void scaleMesh(MeshModel *m, const Vector &scale);
        void fitMesh(MeshModel *m, const Vector &pos, const Vector &size, bool uniform);
    }
}

#endif
