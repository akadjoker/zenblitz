#ifndef ENGINE_MESHCOLLIDER_H
#define ENGINE_MESHCOLLIDER_H

#include "engine/Collision.h"
#include <ct/vector.hpp>

namespace engine
{
    class MeshCollider
    {
    public:
        struct Vertex
        {
            Vector coords;
        };
        struct Triangle
        {
            void *surface;
            int verts[3];
            int index;
        };

        MeshCollider(const ct::Vector<Vertex> &verts, const ct::Vector<Triangle> &tris);
        ~MeshCollider();

        bool collide(const Line &line, float radius, Collision *currColl, const Transform &tform);
        bool intersects(const MeshCollider &c, const Transform &t) const;

    private:
        ct::Vector<Vertex> mVertices;
        ct::Vector<Triangle> mTriangles;

        struct Node
        {
            Box box;
            Node *left = nullptr, *right = nullptr;
            ct::Vector<int> triangles;
            ~Node() { delete left; delete right; }
        };

        Node *mTree = nullptr;
        ct::Vector<Node *> mLeaves;
        ct::Vector<Vector> mTriCentres;

        Box nodeBox(const ct::Vector<int> &tris);
        Node *createLeaf(const ct::Vector<int> &tris);
        Node *createNode(const ct::Vector<int> &tris);
        bool collide(const Box &box, const Line &line, float radius, const Transform &tform,
                     Collision *currColl, Node *node);
    };
}

#endif
