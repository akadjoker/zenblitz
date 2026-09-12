#include "engine/MeshCollider.h"
#include <ct/sort.hpp>
#include <utility>

namespace engine
{
    static bool triTest(const Vector a[3], const Vector b[3])
    {
        bool pb0 = false, pb1 = false, pb2 = false;
        blitz::Plane p(a[0], a[1], a[2]), p0, p1, p2;
        for (int k = 0; k < 3; ++k)
        {
            Line l(b[k], b[(k + 1) % 3] - b[k]);
            float t = p.t_intersect(l);
            if (t < 0 || t > 1) continue;
            Vector i = l * t;
            if (!pb0) { p0 = blitz::Plane(a[0] + p.n, a[1], a[0]); pb0 = true; }
            if (p0.distance(i) < 0) continue;
            if (!pb1) { p1 = blitz::Plane(a[1] + p.n, a[2], a[1]); pb1 = true; }
            if (p1.distance(i) < 0) continue;
            if (!pb2) { p2 = blitz::Plane(a[2] + p.n, a[0], a[2]); pb2 = true; }
            if (p2.distance(i) < 0) continue;
            return true;
        }
        return false;
    }

    static bool trisIntersect(const Vector a[3], const Vector b[3])
    {
        return triTest(a, b) || triTest(b, a);
    }

    static const int kMaxCollTris = 16;

    MeshCollider::MeshCollider(const ct::Vector<Vertex> &verts, const ct::Vector<Triangle> &tris)
        : mVertices(verts), mTriangles(tris)
    {
        ct::Vector<int> ts;
        mTriCentres.clear();
        for (size_t k = 0; k < mTriangles.size(); ++k)
        {
            const Triangle &t = mTriangles[k];
            const Vector &v0 = mVertices[t.verts[0]].coords;
            const Vector &v1 = mVertices[t.verts[1]].coords;
            const Vector &v2 = mVertices[t.verts[2]].coords;
            mTriCentres.push_back((v0 + v1 + v2) / 3.0f);
            ts.push_back((int)k);
        }
        mTree = createNode(ts);
    }

    MeshCollider::~MeshCollider()
    {
        delete mTree;
    }

    bool MeshCollider::collide(const Line &line, float radius, Collision *currColl, const Transform &t)
    {
        if (!mTree) return false;
        Box box(line);
        box.expand(radius);
        Box localBox = -t * box;
        return collide(localBox, line, radius, t, currColl, mTree);
    }

    bool MeshCollider::collide(const Box &lineBox, const Line &line, float radius, const Transform &tform,
                                Collision *currColl, Node *node)
    {
        if (!lineBox.overlaps(node->box)) return false;

        bool hit = false;
        if (node->triangles.empty())
        {
            if (node->left) hit |= collide(lineBox, line, radius, tform, currColl, node->left);
            if (node->right) hit |= collide(lineBox, line, radius, tform, currColl, node->right);
            return hit;
        }

        for (size_t k = 0; k < node->triangles.size(); ++k)
        {
            const Triangle &tri = mTriangles[node->triangles[k]];
            const Vector &v0 = mVertices[tri.verts[0]].coords;
            const Vector &v1 = mVertices[tri.verts[1]].coords;
            const Vector &v2 = mVertices[tri.verts[2]].coords;

            Box triBox(v0);
            triBox.update(v1);
            triBox.update(v2);
            if (!triBox.overlaps(lineBox)) continue;

            if (!currColl->triangleCollide(line, radius, tform * v0, tform * v1, tform * v2)) continue;

            currColl->surface = tri.surface;
            currColl->index = (unsigned short)tri.index;
            hit = true;
        }
        return hit;
    }

    Box MeshCollider::nodeBox(const ct::Vector<int> &tris)
    {
        Box box;
        for (size_t k = 0; k < tris.size(); ++k)
        {
            const Triangle &t = mTriangles[tris[k]];
            for (int j = 0; j < 3; ++j) box.update(mVertices[t.verts[j]].coords);
        }
        return box;
    }

    MeshCollider::Node *MeshCollider::createLeaf(const ct::Vector<int> &tris)
    {
        Node *c = new Node();
        c->box = nodeBox(tris);
        c->triangles = tris;
        mLeaves.push_back(c);
        return c;
    }

    MeshCollider::Node *MeshCollider::createNode(const ct::Vector<int> &tris)
    {
        if (tris.size() <= (size_t)kMaxCollTris) return createLeaf(tris);

        Node *c = new Node();
        c->box = nodeBox(tris);

        float maxExtent = c->box.width();
        int axis = 0;
        if (c->box.height() > maxExtent) { maxExtent = c->box.height(); axis = 1; }
        if (c->box.depth() > maxExtent) { maxExtent = c->box.depth(); axis = 2; }

        ct::Vector<std::pair<float, int>> axisMap;
        for (size_t k = 0; k < tris.size(); ++k)
            axisMap.push_back(std::make_pair(mTriCentres[tris[k]][axis], tris[k]));
        ct::sort(axisMap.data(), axisMap.data() + axisMap.size(),
                 [](const std::pair<float, int> &a, const std::pair<float, int> &b) { return a.first < b.first; });

        ct::Vector<int> newTris;
        size_t half = axisMap.size() / 2;
        for (size_t k = 0; k < half; ++k) newTris.push_back(axisMap[k].second);
        c->left = createNode(newTris);

        newTris.clear();
        for (size_t k = half; k < axisMap.size(); ++k) newTris.push_back(axisMap[k].second);
        c->right = createNode(newTris);

        return c;
    }

    bool MeshCollider::intersects(const MeshCollider &c, const Transform &t) const
    {
        static Vector a[16][3], b[3];

        if (!(t * mTree->box).overlaps(c.mTree->box)) return false;
        for (size_t k = 0; k < mLeaves.size(); ++k)
        {
            Node *p = mLeaves[k];
            Box box = t * p->box;
            bool tformed = false;
            for (size_t j = 0; j < c.mLeaves.size(); ++j)
            {
                Node *q = c.mLeaves[j];
                if (!box.overlaps(q->box)) continue;
                if (!tformed)
                {
                    for (size_t n = 0; n < p->triangles.size() && n < 16; ++n)
                    {
                        const Triangle &tri = mTriangles[p->triangles[n]];
                        a[n][0] = t * mVertices[tri.verts[0]].coords;
                        a[n][1] = t * mVertices[tri.verts[1]].coords;
                        a[n][2] = t * mVertices[tri.verts[2]].coords;
                    }
                    tformed = true;
                }
                for (size_t n = 0; n < q->triangles.size(); ++n)
                {
                    const Triangle &tri = c.mTriangles[q->triangles[n]];
                    b[0] = c.mVertices[tri.verts[0]].coords;
                    b[1] = c.mVertices[tri.verts[1]].coords;
                    b[2] = c.mVertices[tri.verts[2]].coords;
                    for (size_t ti = 0; ti < p->triangles.size() && ti < 16; ++ti)
                        if (trisIntersect(a[ti], b)) return true;
                }
            }
        }
        return false;
    }
}
