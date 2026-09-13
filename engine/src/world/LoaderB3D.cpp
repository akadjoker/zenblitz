#include "engine/LoaderB3D.h"
#include "engine/FilePath.h"
#include "engine/Animator.h"
#include "engine/Texture.h"
#include <SDL2/SDL_rwops.h>
#include <ct/vector.hpp>
#include <cstring>

namespace engine
{
    namespace
    {
        SDL_RWops *g_in;
        gpu::Device *g_dev;
        ct::Vector<long> g_chunkStack;
        ct::Vector<Texture *> g_textures;
        ct::Vector<Brush> g_brushes;
        ct::Vector<Object *> g_bones;

        int swapEndian(int n)
        {
            return ((n & 0xff) << 24) | ((n & 0xff00) << 8) | ((n & 0xff0000) >> 8) | ((unsigned)(n & 0xff000000) >> 24);
        }

        void clearState()
        {
            for (size_t k = 0; k < g_textures.size(); ++k) Texture::release(g_textures[k]);
            g_textures.clear();
            g_bones.clear();
            g_brushes.clear();
            g_chunkStack.clear();
        }

        int readChunk()
        {
            int header[2];
            if (SDL_RWread(g_in, header, 8, 1) < 1) return 0;
            g_chunkStack.push_back((long)SDL_RWtell(g_in) + header[1]);
            return swapEndian(header[0]);
        }

        void exitChunk()
        {
            SDL_RWseek(g_in, g_chunkStack[g_chunkStack.size() - 1], RW_SEEK_SET);
            g_chunkStack.pop_back();
        }

        long chunkSize()
        {
            return g_chunkStack[g_chunkStack.size() - 1] - (long)SDL_RWtell(g_in);
        }

        void readBytes(void *buf, int n) { size_t r = SDL_RWread(g_in, buf, n, 1); (void)r; }
        void skipBytes(int n) { SDL_RWseek(g_in, n, RW_SEEK_CUR); }
        int readInt() { int n; readBytes(&n, 4); return n; }
        void readIntArray(int t[], int n) { readBytes(t, n * 4); }
        float readFloat() { float n; readBytes(&n, 4); return n; }
        void readFloatArray(float t[], int n) { readBytes(t, n * 4); }

        unsigned readColor()
        {
            float r = readFloat(); if (r < 0) r = 0; else if (r > 1) r = 1;
            float g = readFloat(); if (g < 0) g = 0; else if (g > 1) g = 1;
            float b = readFloat(); if (b < 0) b = 0; else if (b > 1) b = 1;
            float a = readFloat(); if (a < 0) a = 0; else if (a > 1) a = 1;
            return ((unsigned)(a * 255) << 24) | ((unsigned)(r * 255) << 16) | ((unsigned)(g * 255) << 8) | (unsigned)(b * 255);
        }

        ct::String readString()
        {
            ct::String t;
            for (;;)
            {
                char c;
                readBytes(&c, 1);
                if (!c) return t;
                t += c;
            }
        }

        void readTextures()
        {
            while (chunkSize())
            {
                ct::String name = readString();
                int flags = readInt();
                int blend = readInt();
                float pos[2], scl[2];
                readFloatArray(pos, 2);
                readFloatArray(scl, 2);
                float rot = readFloat();

                Texture *tex = g_dev ? Texture::load(name, flags & 0xffff) : nullptr;
                if (tex)
                {
                    tex->setBlend(blend);
                    if (pos[0] != 0 || pos[1] != 0) tex->setPosition(pos[0], pos[1]);
                    if (scl[0] != 1 || scl[1] != 1) tex->setScale(scl[0], scl[1]);
                    if (rot != 0) tex->setRotation(rot);
                }
                g_textures.push_back(tex);
            }
        }

        void readBrushes()
        {
            int nTexs = readInt();
            int texId[8] = {-1, -1, -1, -1, -1, -1, -1, -1};

            while (chunkSize())
            {
                readString();
                float col[4];
                readFloatArray(col, 4);
                float shi = readFloat();
                int blend = readInt();
                int fx = readInt();
                readIntArray(texId, nTexs);

                Brush bru;
                bru.setColor(Vector(col[0], col[1], col[2]));
                bru.setAlpha(col[3]);
                bru.setShininess(shi);
                bru.setBlend(blend);
                bru.setFX(fx);

                if (g_dev)
                {
                    for (int k = 0; k < 8; ++k)
                    {
                        if (texId[k] < 0 || (size_t)texId[k] >= g_textures.size() || !g_textures[texId[k]]) continue;
                        bru.setTexture(k, BrushTexture::fromTexture(*g_dev, g_textures[texId[k]], 0));
                    }
                }

                g_brushes.push_back(bru);
            }
        }

        int readVertices()
        {
            int flags = readInt();
            int tcSets = readInt();
            int tcSize = readInt();
            float tc[4] = {0, 0, 0, 0};

            Surface::Vertex t;
            while (chunkSize())
            {
                float coords[3];
                readFloatArray(coords, 3);
                t.coords = Vector(coords[0], coords[1], coords[2]);
                if (flags & 1)
                {
                    float n[3];
                    readFloatArray(n, 3);
                    t.normal = Vector(n[0], n[1], n[2]);
                }
                if (flags & 2) t.color = readColor();
                for (int k = 0; k < tcSets; ++k)
                {
                    readFloatArray(tc, tcSize);
                    if (k < 2) { t.texCoords[k][0] = tc[0]; t.texCoords[k][1] = tc[1]; }
                }
                MeshLoader::addVertex(t);
            }
            return flags;
        }

        void readTriangles()
        {
            int brushId = readInt();
            Brush b = brushId >= 0 && (size_t)brushId < g_brushes.size() ? g_brushes[brushId] : Brush();
            while (chunkSize())
            {
                int verts[3];
                readIntArray(verts, 3);
                MeshLoader::addTriangle(verts, brushId, b);
            }
        }

        int readMesh()
        {
            int flags = 0;
            while (chunkSize())
            {
                switch (readChunk())
                {
                case 'VRTS': flags = readVertices(); break;
                case 'TRIS': readTriangles(); break;
                }
                exitChunk();
            }
            return flags;
        }

        Object *readBone()
        {
            Object *bone = new Object();
            g_bones.push_back(bone);

            while (chunkSize())
            {
                int vert = readInt();
                float weight = readFloat();
                MeshLoader::addBone(vert, weight, (int)g_bones.size());
            }
            return bone;
        }

        void readKeys(Animation &anim)
        {
            int flags = readInt();
            while (chunkSize())
            {
                int frame = readInt();
                if (flags & 1)
                {
                    float pos[3]; readFloatArray(pos, 3);
                    anim.setPositionKey(frame, Vector(pos[0], pos[1], pos[2]));
                }
                if (flags & 2)
                {
                    float scl[3]; readFloatArray(scl, 3);
                    anim.setScaleKey(frame, Vector(scl[0], scl[1], scl[2]));
                }
                if (flags & 4)
                {
                    float rot[4]; readFloatArray(rot, 4);
                    anim.setRotationKey(frame, Quat(rot[0], Vector(rot[1], rot[2], rot[3])));
                }
            }
        }

        Object *readObject(Object *parent)
        {
            Object *obj = nullptr;

            ct::String name = readString();
            float pos[3], scl[3], rot[4];
            readFloatArray(pos, 3);
            readFloatArray(scl, 3);
            readFloatArray(rot, 4);

            Animation keys;
            int animLen = 0;
            MeshModel *mesh = nullptr;
            int meshFlags = 0, meshBrush = -1;

            while (chunkSize())
            {
                switch (readChunk())
                {
                case 'MESH':
                    MeshLoader::beginMesh();
                    obj = mesh = new MeshModel();
                    meshBrush = readInt();
                    meshFlags = readMesh();
                    break;
                case 'BONE':
                    obj = readBone();
                    break;
                case 'KEYS':
                    readKeys(keys);
                    break;
                case 'ANIM':
                    readInt();
                    animLen = readInt();
                    readFloat();
                    break;
                case 'NODE':
                    if (!obj) obj = new MeshModel();
                    readObject(obj);
                    break;
                }
                exitChunk();
            }

            if (!obj) obj = new MeshModel();

            obj->setName(name);
            obj->setLocalPosition(Vector(pos[0], pos[1], pos[2]));
            obj->setLocalScale(Vector(scl[0], scl[1], scl[2]));
            obj->setLocalRotation(Quat(rot[0], Vector(rot[1], rot[2], rot[3])));
            obj->setAnimation(keys);

            if (mesh)
            {
                MeshLoader::endMesh(mesh);
                if (!(meshFlags & 1)) mesh->updateNormals();
                if (meshBrush != -1 && (size_t)meshBrush < g_brushes.size()) mesh->setBrush(g_brushes[meshBrush]);
            }

            if (mesh && !g_bones.empty())
            {
                g_bones.insert(g_bones.begin(), mesh);
                mesh->setAnimator(new Animator(g_bones, animLen));
                mesh->createBones();
                g_bones.clear();
            }
            else if (animLen)
            {
                obj->setAnimator(new Animator(obj, animLen));
            }

            if (parent) obj->setParent(parent);

            return obj;
        }
    }

    MeshModel *LoaderB3D::load(const ct::String &f, const Transform &conv, int hint, gpu::Device *dev)
    {
        (void)conv; (void)hint;

        const ct::String path = resolveCaseInsensitive(f);
        g_in = SDL_RWFromFile(path.c_str(), "rb");
        if (!g_in) return nullptr;

        g_dev = dev;
        clearState();

        size_t slash = path.find_last_of("/\\");
        setTexturePath(slash == ct::String::npos ? ct::String() : path.substr(0, slash));

        int tag = readChunk();
        if (tag != 'BB3D')
        {
            SDL_RWclose(g_in);
            return nullptr;
        }

        int version = readInt();
        if (version > 1)
        {
            SDL_RWclose(g_in);
            return nullptr;
        }

        Object *obj = nullptr;
        while (chunkSize())
        {
            switch (readChunk())
            {
            case 'TEXS': readTextures(); break;
            case 'BRUS': readBrushes(); break;
            case 'NODE': obj = readObject(nullptr); break;
            }
            exitChunk();
        }
        SDL_RWclose(g_in);

        setTexturePath(ct::String());
        clearState();

        return obj ? obj->getModel()->getMeshModel() : nullptr;
    }
}
