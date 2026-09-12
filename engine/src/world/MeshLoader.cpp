#include "engine/MeshLoader.h"
#include <ct/vector.hpp>
#include <ct/hashmap.hpp>

namespace engine
{
    namespace
    {
        struct Tri { int verts[3]; };
        struct BrushSurf
        {
            Brush brush;
            ct::Vector<Tri> tris;
        };
        struct MLMesh
        {
            ct::HashMap<int, BrushSurf> brushSurfs;
            ct::Vector<Surface::Vertex> verts;
        };

        MLMesh *g_mesh = nullptr;
        ct::Vector<MLMesh *> g_meshStack;
    }

    void MeshLoader::beginMesh()
    {
        g_meshStack.push_back(g_mesh);
        g_mesh = new MLMesh();
    }

    int MeshLoader::numVertices()
    {
        return (int)g_mesh->verts.size();
    }

    void MeshLoader::addVertex(const Surface::Vertex &v)
    {
        g_mesh->verts.push_back(v);
    }

    void MeshLoader::addTriangle(const int verts[3], int brushId, const Brush &b)
    {
        addTriangle(verts[0], verts[1], verts[2], brushId, b);
    }

    void MeshLoader::addBone(int n, float w, int b)
    {
        Surface::Vertex &v = g_mesh->verts[n];
        int i;
        for (i = 0; i < kMaxSurfaceBones; ++i)
            if (v.boneBones[i] == 255 || w > v.boneWeights[i]) break;
        if (i == kMaxSurfaceBones) return;
        for (int k = kMaxSurfaceBones - 1; k > i; --k)
        {
            v.boneBones[k] = v.boneBones[k - 1];
            v.boneWeights[k] = v.boneWeights[k - 1];
        }
        v.boneBones[i] = (unsigned char)b;
        v.boneWeights[i] = w;
    }

    Surface::Vertex &MeshLoader::refVertex(int n)
    {
        return g_mesh->verts[n];
    }

    void MeshLoader::addTriangle(int v0, int v1, int v2, int brushId, const Brush &b)
    {
        BrushSurf *surf = g_mesh->brushSurfs.find(brushId);
        if (!surf)
        {
            BrushSurf bs;
            bs.brush = b;
            g_mesh->brushSurfs.put(brushId, bs);
            surf = g_mesh->brushSurfs.find(brushId);
        }
        Tri tri{{v0, v1, v2}};
        surf->tris.push_back(tri);
    }

    void MeshLoader::endMesh(MeshModel *mesh)
    {
        if (mesh)
        {
            for (size_t k = 0; k < g_mesh->verts.size(); ++k)
            {
                Surface::Vertex &v = g_mesh->verts[k];
                if (v.boneBones[0] == 255) continue;
                int j;
                float t = 0;
                for (j = 0; j < kMaxSurfaceBones; ++j)
                {
                    if (v.boneBones[j] == 255) break;
                    t += v.boneWeights[j];
                }
                if (t > 0) t = 1.0f / t;
                for (j = 0; j < kMaxSurfaceBones; ++j) v.boneWeights[j] *= t;
            }

            for (auto &entry : g_mesh->brushSurfs)
            {
                BrushSurf &bs = entry.value;
                Surface *surf = mesh->findSurface(bs.brush);
                if (!surf) surf = mesh->createSurface(bs.brush);

                ct::HashMap<int, int> vertMap;
                for (size_t k = 0; k < bs.tris.size(); ++k)
                {
                    Surface::Triangle tri;
                    for (int j = 0; j < 3; ++j)
                    {
                        int n = bs.tris[k].verts[j];
                        int *found = vertMap.find(n);
                        int id;
                        if (found) id = *found;
                        else
                        {
                            id = surf->numVertices();
                            surf->addVertex(g_mesh->verts[n]);
                            vertMap.put(n, id);
                        }
                        tri.verts[j] = (unsigned short)id;
                    }
                    surf->addTriangle(tri);
                }
            }
        }
        delete g_mesh;
        g_mesh = g_meshStack[g_meshStack.size() - 1];
        g_meshStack.pop_back();
    }
}
