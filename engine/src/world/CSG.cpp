#include "engine/CSG.h"
#include "engine/MeshModel.h"
#include <ct/vector.hpp>

namespace engine
{
    namespace CSG
    {
        namespace
        {
            static constexpr float kEpsilon = 0.00001f;

            struct Plane
            {
                Vector normal;
                float distance = 0.0f;

                Plane() {}
                Plane(const Vector &a, const Vector &b, const Vector &c)
                {
                    normal = (b - a).cross(c - a);
                    float length = normal.length();
                    if (length > kEpsilon) normal /= length;
                    distance = normal.dot(a);
                }

                void flip() { normal = -normal; distance = -distance; }
            };

            static unsigned lerpColor(unsigned a, unsigned b, float t)
            {
                unsigned result = 0;
                for (int shift = 0; shift < 32; shift += 8)
                {
                    float av = (float)((a >> shift) & 255u);
                    float bv = (float)((b >> shift) & 255u);
                    unsigned value = (unsigned)(av + (bv - av) * t + 0.5f);
                    result |= value << shift;
                }
                return result;
            }

            static Surface::Vertex lerpVertex(const Surface::Vertex &a, const Surface::Vertex &b, float t)
            {
                Surface::Vertex result = a;
                result.coords = a.coords + (b.coords - a.coords) * t;
                result.normal = a.normal + (b.normal - a.normal) * t;
                float length = result.normal.length();
                if (length > kEpsilon) result.normal /= length;
                result.color = lerpColor(a.color, b.color, t);
                for (int set = 0; set < 2; ++set)
                    for (int coord = 0; coord < 2; ++coord)
                        result.texCoords[set][coord] = a.texCoords[set][coord] +
                            (b.texCoords[set][coord] - a.texCoords[set][coord]) * t;
                for (int bone = 0; bone < kMaxSurfaceBones; ++bone)
                    result.boneWeights[bone] = a.boneWeights[bone] + (b.boneWeights[bone] - a.boneWeights[bone]) * t;
                return result;
            }

            struct Polygon
            {
                ct::Vector<Surface::Vertex> vertices;
                Brush brush;
                Plane plane;

                Polygon() {}
                Polygon(const Surface::Vertex &a, const Surface::Vertex &b, const Surface::Vertex &c, const Brush &value)
                    : brush(value), plane(a.coords, b.coords, c.coords)
                {
                    vertices.push_back(a);
                    vertices.push_back(b);
                    vertices.push_back(c);
                }

                void flip()
                {
                    const size_t count = vertices.size();
                    for (size_t index = 0; index < count / 2; ++index)
                    {
                        Surface::Vertex swap = vertices[index];
                        vertices[index] = vertices[count - 1 - index];
                        vertices[count - 1 - index] = swap;
                    }
                    for (size_t index = 0; index < count; ++index) vertices[index].normal = -vertices[index].normal;
                    plane.flip();
                }
            };

            enum PolygonType { Coplanar = 0, Front = 1, Back = 2, Spanning = 3 };

            static void splitPolygon(const Plane &plane, const Polygon &polygon,
                                     ct::Vector<Polygon> &coplanarFront, ct::Vector<Polygon> &coplanarBack,
                                     ct::Vector<Polygon> &front, ct::Vector<Polygon> &back)
            {
                const size_t count = polygon.vertices.size();
                ct::Vector<int> types;
                types.reserve(count);
                int polygonType = Coplanar;
                for (size_t index = 0; index < count; ++index)
                {
                    const float value = plane.normal.dot(polygon.vertices[index].coords) - plane.distance;
                    const int type = value < -kEpsilon ? Back : value > kEpsilon ? Front : Coplanar;
                    polygonType |= type;
                    types.push_back(type);
                }

                if (polygonType == Coplanar)
                {
                    (plane.normal.dot(polygon.plane.normal) >= 0.0f ? coplanarFront : coplanarBack).push_back(polygon);
                    return;
                }
                if (polygonType == Front) { front.push_back(polygon); return; }
                if (polygonType == Back) { back.push_back(polygon); return; }

                Polygon f, b;
                f.brush = b.brush = polygon.brush;
                for (size_t index = 0; index < count; ++index)
                {
                    const size_t next = (index + 1) % count;
                    const int type = types[index], nextType = types[next];
                    const Surface::Vertex &first = polygon.vertices[index];
                    const Surface::Vertex &second = polygon.vertices[next];
                    if (type != Back) f.vertices.push_back(first);
                    if (type != Front) b.vertices.push_back(first);
                    if ((type | nextType) != Spanning) continue;
                    const Vector edge = second.coords - first.coords;
                    const float denominator = plane.normal.dot(edge);
                    if (denominator > -kEpsilon && denominator < kEpsilon) continue;
                    const float t = (plane.distance - plane.normal.dot(first.coords)) / denominator;
                    Surface::Vertex vertex = lerpVertex(first, second, t);
                    f.vertices.push_back(vertex);
                    b.vertices.push_back(vertex);
                }
                if (f.vertices.size() >= 3)
                {
                    f.plane = Plane(f.vertices[0].coords, f.vertices[1].coords, f.vertices[2].coords);
                    front.push_back(f);
                }
                if (b.vertices.size() >= 3)
                {
                    b.plane = Plane(b.vertices[0].coords, b.vertices[1].coords, b.vertices[2].coords);
                    back.push_back(b);
                }
            }

            class Node
            {
            public:
                ~Node() { delete mFront; delete mBack; }

                void invert()
                {
                    for (size_t index = 0; index < mPolygons.size(); ++index) mPolygons[index].flip();
                    if (mHasPlane) mPlane.flip();
                    if (mFront) mFront->invert();
                    if (mBack) mBack->invert();
                    Node *swap = mFront; mFront = mBack; mBack = swap;
                }

                ct::Vector<Polygon> clipPolygons(const ct::Vector<Polygon> &polygons) const
                {
                    if (!mHasPlane) return polygons;
                    ct::Vector<Polygon> front, back, empty;
                    for (size_t index = 0; index < polygons.size(); ++index)
                        splitPolygon(mPlane, polygons[index], empty, empty, front, back);
                    if (mFront) front = mFront->clipPolygons(front);
                    if (mBack) back = mBack->clipPolygons(back);
                    else back.clear();
                    for (size_t index = 0; index < back.size(); ++index) front.push_back(back[index]);
                    return front;
                }

                void clipTo(const Node &other) { mPolygons = other.clipPolygons(mPolygons); if (mFront) mFront->clipTo(other); if (mBack) mBack->clipTo(other); }

                void allPolygons(ct::Vector<Polygon> &out) const
                {
                    for (size_t index = 0; index < mPolygons.size(); ++index) out.push_back(mPolygons[index]);
                    if (mFront) mFront->allPolygons(out);
                    if (mBack) mBack->allPolygons(out);
                }

                void build(const ct::Vector<Polygon> &polygons)
                {
                    if (polygons.empty()) return;
                    if (!mHasPlane) { mPlane = polygons[0].plane; mHasPlane = true; }
                    ct::Vector<Polygon> front, back;
                    for (size_t index = 0; index < polygons.size(); ++index)
                        splitPolygon(mPlane, polygons[index], mPolygons, mPolygons, front, back);
                    if (!front.empty())
                    {
                        if (!mFront) mFront = new Node;
                        mFront->build(front);
                    }
                    if (!back.empty())
                    {
                        if (!mBack) mBack = new Node;
                        mBack->build(back);
                    }
                }

            private:
                Plane mPlane;
                bool mHasPlane = false;
                ct::Vector<Polygon> mPolygons;
                Node *mFront = nullptr, *mBack = nullptr;
            };

            static ct::Vector<Polygon> polygonsFromMesh(const MeshModel &mesh)
            {
                ct::Vector<Polygon> polygons;
                const Transform &transform = mesh.getWorldTform();
                const blitz::Matrix normalTransform = transform.m.cofactor();
                const MeshModel::SurfaceList &surfaces = mesh.getSurfaces();
                for (size_t surfaceIndex = 0; surfaceIndex < surfaces.size(); ++surfaceIndex)
                {
                    const Surface *surface = surfaces[surfaceIndex];
                    const Brush brush(surface->getBrush(), mesh.getBrush());
                    for (int triangleIndex = 0; triangleIndex < surface->numTriangles(); ++triangleIndex)
                    {
                        const Surface::Triangle triangle = surface->getTriangle(triangleIndex);
                        Surface::Vertex vertices[3];
                        for (int vertex = 0; vertex < 3; ++vertex)
                        {
                            vertices[vertex] = surface->getVertex(triangle.verts[vertex]);
                            vertices[vertex].coords = transform * vertices[vertex].coords;
                            vertices[vertex].normal = normalTransform * vertices[vertex].normal;
                            const float length = vertices[vertex].normal.length();
                            if (length > kEpsilon) vertices[vertex].normal /= length;
                        }
                        Polygon polygon(vertices[0], vertices[1], vertices[2], brush);
                        if (polygon.plane.normal.length() > kEpsilon) polygons.push_back(polygon);
                    }
                }
                return polygons;
            }

            static MeshModel *meshFromPolygons(const ct::Vector<Polygon> &polygons)
            {
                MeshModel *mesh = new MeshModel;
                for (size_t polygonIndex = 0; polygonIndex < polygons.size(); ++polygonIndex)
                {
                    const Polygon &polygon = polygons[polygonIndex];
                    if (polygon.vertices.size() < 3) continue;
                    Surface *surface = mesh->findSurface(polygon.brush);
                    const int needed = (int)((polygon.vertices.size() - 2) * 3);
                    if (!surface || surface->numVertices() + needed > 65535) surface = mesh->createSurface(polygon.brush);
                    for (size_t vertex = 1; vertex + 1 < polygon.vertices.size(); ++vertex)
                    {
                        const unsigned short base = (unsigned short)surface->numVertices();
                        surface->addVertex(polygon.vertices[0]);
                        surface->addVertex(polygon.vertices[vertex]);
                        surface->addVertex(polygon.vertices[vertex + 1]);
                        surface->addTriangle(Surface::Triangle{{base, (unsigned short)(base + 1), (unsigned short)(base + 2)}});
                    }
                }
                return mesh;
            }
        }

        MeshModel *meshCSG(const MeshModel &a, const MeshModel &b, int method)
        {
            ct::Vector<Polygon> aPolygons = polygonsFromMesh(a);
            ct::Vector<Polygon> bPolygons = polygonsFromMesh(b);
            if (aPolygons.empty() || bPolygons.empty()) return nullptr;

            Node aTree, bTree;
            aTree.build(aPolygons);
            bTree.build(bPolygons);

            if (method == Subtract)
            {
                aTree.invert();
                aTree.clipTo(bTree);
                bTree.clipTo(aTree);
                bTree.invert();
                bTree.clipTo(aTree);
                bTree.invert();
                ct::Vector<Polygon> polygons;
                bTree.allPolygons(polygons);
                aTree.build(polygons);
                aTree.invert();
            }
            else if (method == Intersect)
            {
                aTree.invert();
                bTree.clipTo(aTree);
                bTree.invert();
                aTree.clipTo(bTree);
                bTree.clipTo(aTree);
                ct::Vector<Polygon> polygons;
                bTree.allPolygons(polygons);
                aTree.build(polygons);
                aTree.invert();
            }
            else
            {
                aTree.clipTo(bTree);
                bTree.clipTo(aTree);
                bTree.invert();
                bTree.clipTo(aTree);
                bTree.invert();
                ct::Vector<Polygon> polygons;
                bTree.allPolygons(polygons);
                aTree.build(polygons);
            }

            ct::Vector<Polygon> polygons;
            aTree.allPolygons(polygons);
            return meshFromPolygons(polygons);
        }
    }
}
