#include "engine/MeshUtil.h"

namespace engine
{
    namespace MeshUtil
    {
        MeshModel *createCube(const Brush &b)
        {
            static Vector norms[] = {
                Vector(0, 0, -1), Vector(1, 0, 0), Vector(0, 0, 1),
                Vector(-1, 0, 0), Vector(0, 1, 0), Vector(0, -1, 0)};
            static Vector texCoords[] = {
                Vector(0, 0, 1), Vector(1, 0, 1), Vector(1, 1, 1), Vector(0, 1, 1)};
            static int verts[] = {
                2, 3, 1, 0, 3, 7, 5, 1, 7, 6, 4, 5, 6, 2, 0, 4, 6, 7, 3, 2, 0, 1, 5, 4};
            Box box(Vector(-1, -1, -1), Vector(1, 1, 1));

            MeshModel *m = new MeshModel();
            Surface *s = m->createSurface(b);
            Surface::Vertex v;
            Surface::Triangle t;
            for (int k = 0; k < 24; k += 4)
            {
                const Vector &normal = norms[k / 4];
                for (int j = 0; j < 4; ++j)
                {
                    v.coords = box.corner(verts[k + j]);
                    v.normal = normal;
                    v.texCoords[0][0] = v.texCoords[1][0] = texCoords[j].x;
                    v.texCoords[0][1] = v.texCoords[1][1] = texCoords[j].y;
                    s->addVertex(v);
                }
                t.verts[0] = k; t.verts[1] = k + 1; t.verts[2] = k + 2; s->addTriangle(t);
                t.verts[1] = k + 2; t.verts[2] = k + 3; s->addTriangle(t);
            }
            return m;
        }

        MeshModel *createSphere(const Brush &b, int segs)
        {
            int hSegs = segs * 2, vSegs = segs;

            MeshModel *m = new MeshModel();
            Surface *s = m->createSurface(b);

            Surface::Vertex v;
            Surface::Triangle t;

            v.coords = v.normal = Vector(0, 1, 0);
            int k;
            for (k = 0; k < hSegs; ++k)
            {
                v.texCoords[0][0] = v.texCoords[1][0] = (k + .5f) / hSegs;
                v.texCoords[0][1] = v.texCoords[1][1] = 0;
                s->addVertex(v);
            }
            for (k = 1; k < vSegs; ++k)
            {
                float pitch = k * blitz::PI / vSegs - blitz::HALFPI;
                for (int j = 0; j <= hSegs; ++j)
                {
                    float yaw = (j % hSegs) * blitz::TWOPI / hSegs;
                    v.coords = v.normal = blitz::rotationMatrix(pitch, yaw, 0).k;
                    v.texCoords[0][0] = v.texCoords[1][0] = float(j) / float(hSegs);
                    v.texCoords[0][1] = v.texCoords[1][1] = float(k) / float(vSegs);
                    s->addVertex(v);
                }
            }
            v.coords = v.normal = Vector(0, -1, 0);
            for (k = 0; k < hSegs; ++k)
            {
                v.texCoords[0][0] = v.texCoords[1][0] = (k + .5f) / hSegs;
                v.texCoords[0][1] = v.texCoords[1][1] = 1;
                s->addVertex(v);
            }
            for (k = 0; k < hSegs; ++k)
            {
                t.verts[0] = (unsigned short)k;
                t.verts[1] = (unsigned short)(t.verts[0] + hSegs + 1);
                t.verts[2] = (unsigned short)(t.verts[1] - 1);
                s->addTriangle(t);
            }
            for (k = 1; k < vSegs - 1; ++k)
            {
                for (int j = 0; j < hSegs; ++j)
                {
                    t.verts[0] = (unsigned short)(k * (hSegs + 1) + j - 1);
                    t.verts[1] = (unsigned short)(t.verts[0] + 1);
                    t.verts[2] = (unsigned short)(t.verts[1] + hSegs + 1);
                    s->addTriangle(t);
                    t.verts[1] = t.verts[2];
                    t.verts[2] = (unsigned short)(t.verts[1] - 1);
                    s->addTriangle(t);
                }
            }
            for (k = 0; k < hSegs; ++k)
            {
                t.verts[0] = (unsigned short)((hSegs + 1) * (vSegs - 1) + k - 1);
                t.verts[1] = (unsigned short)(t.verts[0] + 1);
                t.verts[2] = (unsigned short)(t.verts[1] + hSegs);
                s->addTriangle(t);
            }

            return m;
        }

        MeshModel *createCylinder(const Brush &b, int segs, bool solid)
        {
            MeshModel *m = new MeshModel();
            Surface::Vertex v;
            Surface::Triangle t;

            Surface *s = m->createSurface(b);
            int k;
            for (k = 0; k <= segs; ++k)
            {
                float yaw = (k % segs) * blitz::TWOPI / segs;
                v.coords = blitz::rotationMatrix(0, yaw, 0).k;
                v.coords.y = 1;
                v.normal = Vector(v.coords.x, 0, v.coords.z);
                v.texCoords[0][0] = v.texCoords[1][0] = float(k) / segs;
                v.texCoords[0][1] = v.texCoords[1][1] = 0;
                s->addVertex(v);
                v.coords.y = -1;
                v.texCoords[0][0] = v.texCoords[1][0] = float(k) / segs;
                v.texCoords[0][1] = v.texCoords[1][1] = 1;
                s->addVertex(v);
            }
            for (k = 0; k < segs; ++k)
            {
                t.verts[0] = (unsigned short)(k * 2);
                t.verts[1] = (unsigned short)(t.verts[0] + 2);
                t.verts[2] = (unsigned short)(t.verts[1] + 1);
                s->addTriangle(t);
                t.verts[1] = t.verts[2];
                t.verts[2] = (unsigned short)(t.verts[1] - 2);
                s->addTriangle(t);
            }

            if (!solid) return m;

            s = m->createSurface(b);

            for (k = 0; k < segs; ++k)
            {
                float yaw = k * blitz::TWOPI / segs;
                v.coords = blitz::rotationMatrix(0, yaw, 0).k;
                v.coords.y = 1; v.normal = Vector(0, 1, 0);
                v.texCoords[0][0] = v.texCoords[1][0] = v.coords.x * .5f + .5f;
                v.texCoords[0][1] = v.texCoords[1][1] = v.coords.z * .5f + .5f;
                s->addVertex(v);
                v.coords.y = -1; v.normal = Vector(0, -1, 0);
                s->addVertex(v);
            }
            for (k = 2; k < segs; ++k)
            {
                t.verts[0] = 0;
                t.verts[1] = (unsigned short)(k * 2);
                t.verts[2] = (unsigned short)((k - 1) * 2);
                s->addTriangle(t);
                t.verts[0] = 1;
                t.verts[1] = (unsigned short)((k - 1) * 2 + 1);
                t.verts[2] = (unsigned short)(k * 2 + 1);
                s->addTriangle(t);
            }

            return m;
        }

        MeshModel *createCone(const Brush &b, int segs, bool solid)
        {
            MeshModel *m = new MeshModel();
            Surface::Vertex v;
            Surface::Triangle t;

            Surface *s = m->createSurface(b);
            int k;
            v.coords = v.normal = Vector(0, 1, 0);
            for (k = 0; k < segs; ++k)
            {
                v.texCoords[0][0] = v.texCoords[1][0] = (k + .5f) / segs;
                v.texCoords[0][1] = v.texCoords[1][1] = 0;
                s->addVertex(v);
            }
            for (k = 0; k <= segs; ++k)
            {
                float yaw = (k % segs) * blitz::TWOPI / segs;
                v.coords = blitz::yawMatrix(yaw).k; v.coords.y = -1;
                v.normal = Vector(v.coords.x, 0, v.coords.z);
                v.texCoords[0][0] = v.texCoords[1][0] = float(k) / segs;
                v.texCoords[0][1] = v.texCoords[1][1] = 1;
                s->addVertex(v);
            }
            for (k = 0; k < segs; ++k)
            {
                t.verts[0] = (unsigned short)k;
                t.verts[1] = (unsigned short)(k + segs + 1);
                t.verts[2] = (unsigned short)(k + segs);
                s->addTriangle(t);
            }
            if (!solid) return m;
            s = m->createSurface(b);
            for (k = 0; k < segs; ++k)
            {
                float yaw = k * blitz::TWOPI / segs;
                v.coords = blitz::yawMatrix(yaw).k; v.coords.y = -1;
                v.normal = Vector(v.coords.x, 0, v.coords.z);
                v.texCoords[0][0] = v.texCoords[1][0] = v.coords.x * .5f + .5f;
                v.texCoords[0][1] = v.texCoords[1][1] = v.coords.z * .5f + .5f;
                s->addVertex(v);
            }
            t.verts[0] = 0;
            for (k = 2; k < segs; ++k)
            {
                t.verts[1] = (unsigned short)(k - 1);
                t.verts[2] = (unsigned short)k;
                s->addTriangle(t);
            }
            return m;
        }

        void lightMesh(MeshModel *m, const Vector &pos, const Vector &rgb, float range)
        {
            const MeshModel::SurfaceList &surfs = m->getSurfaces();
            if (range)
            {
                float att = 1.0f / range;
                for (size_t k = 0; k < surfs.size(); ++k)
                {
                    Surface *s = surfs[k];
                    for (int j = 0; j < s->numVertices(); ++j)
                    {
                        const Surface::Vertex &v = s->getVertex(j);
                        Vector lv = pos - v.coords;
                        float dp = v.normal.normalized().dot(lv);
                        if (dp <= 0) continue;
                        float d = lv.length();
                        float i = 1 / (d * att) * (dp / d);
                        s->setColor(j, s->getColor(j) + rgb * i);
                    }
                }
            }
            else
            {
                for (size_t k = 0; k < surfs.size(); ++k)
                {
                    Surface *s = surfs[k];
                    for (int j = 0; j < s->numVertices(); ++j)
                        s->setColor(j, s->getColor(j) + rgb);
                }
            }
        }
    }
}
