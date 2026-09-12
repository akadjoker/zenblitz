#ifndef ENGINE_MESHLOADER_H
#define ENGINE_MESHLOADER_H

#include "engine/MeshModel.h"
#include <string>

namespace engine
{
    class MeshLoader
    {
    public:
        enum
        {
            HintCollapse = 1, HintAnimOnly = 2
        };

        virtual ~MeshLoader() = default;
        virtual MeshModel *load(const std::string &f, const Transform &conv, int hint) = 0;

        static void beginMesh();
        static void addVertex(const Surface::Vertex &v);
        // brushId keys the internal brush->triangles grouping directly
        // (O(1) hash lookup) - callers that don't already have a stable
        // id (a loader without brush indices) can pass a per-brush
        // incrementing counter instead.
        static void addTriangle(const int verts[3], int brushId, const Brush &b);
        static void addTriangle(int v0, int v1, int v2, int brushId, const Brush &b);
        static void addBone(int vert, float weight, int bone);
        static Surface::Vertex &refVertex(int vert);
        static int numVertices();
        static void endMesh(MeshModel *mesh);
    };
}

#endif
