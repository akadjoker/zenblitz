#include "engine/PlaneModel.h"
#include "engine/Collision.h"
#include "engine/Frustum.h"

namespace engine
{
    using blitz::Line;
    using blitz::Vector;

    PlaneModel::PlaneModel(int subDivs) : mRep(new Rep())
    {
        // planemodel.cpp's own bound: the original's debug build rejects
        // anything outside 1..20 outright, and its scratch vertex grid is
        // [17][17], so keep the same ceiling rather than trusting the
        // caller.
        if (subDivs < 1) subDivs = 1;
        if (subDivs > 16) subDivs = 16;
        mRep->subDivs = subDivs;
    }

    PlaneModel::PlaneModel(const PlaneModel &other) : Model(other), mRep(other.mRep)
    {
        ++mRep->refCount;
    }

    PlaneModel::~PlaneModel()
    {
        if (!--mRep->refCount) delete mRep;
    }

    void PlaneModel::freeGpu(gpu::Device &dev)
    {
        // The Rep, and so the mesh, is shared with every clone - only the
        // last one may free it (same rule MeshModel/MD2Model follow).
        if (mRep->refCount == 1) mRep->mesh.freeGpu(dev);
    }

    Plane PlaneModel::getRenderPlane() const
    {
        const Transform t = getRenderTform();
        return Plane(t.v, t.m.j.normalized());
    }

    /* planemodel.cpp's Rep::render, unchanged in shape.

       The plane is infinite, so there is no fixed geometry to draw:
       every frame it takes the camera frustum's four far corners in the
       plane's own local space, walks a subDivs x subDivs grid across
       that quad, and for each cell clips the cell against the y>0 half
       space. A corner below the plane is replaced by where the eye ray
       through it actually meets y=0, which is what makes the drawn
       geometry stretch to the horizon instead of stopping at a mesh
       edge. Texture coordinates come straight from world x/z, so the
       texture stays put on the ground as the camera moves. */
    bool PlaneModel::render(const RenderContext &context)
    {
        const int subDivs = mRep->subDivs;
        const Transform worldTform = getRenderTform();
        Frustum frustum(context.getWorldFrustum(), -worldTform);

        const Vector &eye = frustum.getVertex(Frustum::VertEye);
        if (eye.y <= 0) return false; // camera under the plane: nothing to draw

        const Vector &tl = frustum.getVertex(Frustum::VertTLFar);
        const Vector &tr = frustum.getVertex(Frustum::VertTRFar);
        const Vector &br = frustum.getVertex(Frustum::VertBRFar);
        const Vector &bl = frustum.getVertex(Frustum::VertBLFar);

        // vts[x][y] in the original: the far plane quad, subdivided.
        Vector vts[17][17];
        for (int x = 0; x <= subDivs; ++x)
        {
            const float tx = (float)x / (float)subDivs;
            const Vector top = (tr - tl) * tx + tl;
            const Vector bottom = (br - bl) * tx + bl;
            for (int y = 0; y <= subDivs; ++y)
            {
                const float ty = (float)y / (float)subDivs;
                vts[x][y] = (bottom - top) * ty + top;
            }
        }

        const Plane plane(Vector(0, 1, 0), 0);

        mRep->mesh.begin(0, 0);
        for (int x = 0; x < subDivs; ++x)
        {
            for (int y = 0; y < subDivs; ++y)
            {
                Vector inVerts[4], outVerts[5];
                inVerts[0] = vts[x][y];
                inVerts[1] = vts[x + 1][y];
                inVerts[2] = vts[x + 1][y + 1];
                inVerts[3] = vts[x][y + 1];

                int outCount = 0;
                for (int k = 0; k < 4; ++k)
                {
                    const Vector &vert = inVerts[k];
                    const Vector &prev = inVerts[(k - 1) & 3];

                    if (vert.y > 0)
                    {
                        // entering the half space: emit the crossing point
                        if (prev.y <= 0)
                        {
                            const float t = prev.y / (prev.y - vert.y);
                            outVerts[outCount++] = (vert - prev) * t + prev;
                        }
                    }
                    else
                    {
                        if (prev.y > 0)
                        {
                            const float t = prev.y / (prev.y - vert.y);
                            outVerts[outCount++] = (vert - prev) * t + prev;
                        }
                        // below the plane: project along the eye ray onto it
                        outVerts[outCount++] = plane.intersect(Line(eye, vert - eye));
                    }
                }
                if (outCount < 3 || outCount > 5) continue;

                const int first = mRep->mesh.vertexCount();
                for (int k = 0; k < outCount; ++k)
                {
                    DynamicMesh::Vertex v;
                    v.coords = outVerts[k];
                    v.normal = plane.n;
                    v.texCoords[0][0] = v.texCoords[1][0] = outVerts[k].x;
                    v.texCoords[0][1] = v.texCoords[1][1] = outVerts[k].z;
                    mRep->mesh.addVertex(v);
                }
                // Reversed relative to planemodel.cpp's (0,k-1,k) fan:
                // this engine rasterizes FrontFace::Clockwise with back
                // face culling (MeshRenderer), the opposite convention to
                // the original's D3D setup, so the fan has to wind the
                // other way to face the camera.
                for (int k = 2; k < outCount; ++k)
                    mRep->mesh.addTriangle((unsigned short)first,
                                           (unsigned short)(first + k - 1),
                                           (unsigned short)(first + k));
            }
        }

        if (mRep->mesh.triangleCount()) enqueue(&mRep->mesh, getRenderBrush());
        return false;
    }

    void PlaneModel::uploadQueue(gpu::Device &dev, int type)
    {
        ct::Vector<QueueEntry> &entries = queue(type);
        for (size_t k = 0; k < entries.size(); ++k)
        {
            if (entries[k].dynamicMesh)
            {
                entries[k].dynamicMesh->upload(dev);
                entries[k].geom = entries[k].dynamicMesh->geometry();
            }
        }
    }

    /* planemodel.cpp's collide: an infinite plane needs no mesh test,
       just the ray/plane intersection, with the radius pushing the plane
       up so a sphere stops on it rather than in it. */
    bool PlaneModel::collide(const Line &l, float radius, Collision *curr, const Transform &tform)
    {
        const Line line = -tform * l;

        Plane p(Vector(0, 1, 0), 0);
        p.d -= radius;
        const float t = p.t_intersect(line);
        if (t >= curr->time) return false;

        // Back to world space: cofactor (inverse-transpose) so the normal
        // stays perpendicular under non-uniform scale, same as the
        // original.
        const Vector n = (tform.m.cofactor() * p.n).normalized();
        return curr->update(l, t, n);
    }
}
