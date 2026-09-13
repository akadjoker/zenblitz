// Ported from the original Blitz3D loader_3ds.cpp (Loader_3DS): reads the
// Autodesk/3D Studio .3ds chunk format - mesh geometry, materials, and a
// keyframer track for hierarchy + animation. Structure and chunk IDs are
// unchanged from the original; only the I/O (SDL_RWops instead of
// filebuf) and containers (ct::Vector/ct::HashMap instead of std::vector/
// std::map) differ, per the engine's cross-platform/no-STL-container
// rules.
#include "engine/LoaderB3DS.h"
#include "engine/FilePath.h"
#include "engine/Animator.h"
#include "engine/Texture.h"
#include <SDL2/SDL_rwops.h>
#include <ct/vector.hpp>
#include <ct/hashmap.hpp>
#include <cstring>
#include <cmath>
#include <utility>

namespace engine
{
    namespace
    {
        SDL_RWops *g_in;
        long g_chunkEnd;
        ct::Vector<long> g_parentEnd;
        unsigned short g_animLen;

        bool g_conv, g_flipTris;
        Transform g_convTform;
        bool g_collapse, g_animOnly;

        gpu::Device *g_dev;

        // the original grouped faces by Brush value inside MeshLoader
        // (map<Brush,Surface*>); ours groups by an integer id
        // (ct::HashMap<int,BrushSurf>), so every face also carries the
        // stable id of whatever material parseFaceMat last assigned it
        // (faces default to the untextured material, id 0, until then).
        struct Face3DS { int verts[3]; Brush brush; int brushId; };
        ct::Vector<Face3DS> g_faces;

        // materials_map/name_map/id_map in the original: keyed by name or
        // node id, so a hash map replaces the map<> directly (O(1) instead
        // of O(log n), same lookup semantics).
        ct::HashMap<ct::String, Brush> g_materials;
        ct::HashMap<ct::String, MeshModel *> g_nameMap;
        ct::HashMap<int, MeshModel *> g_idMap;
        ct::HashMap<ct::String, int> g_materialIds;
        int g_nextMaterialId;

        void clearState()
        {
            g_materials.clear();
            g_nameMap.clear();
            g_idMap.clear();
            g_materialIds.clear();
            g_nextMaterialId = 0;
            g_faces.clear();
            g_parentEnd.clear();
        }

        int brushIdFor(const ct::String &name)
        {
            int *found = g_materialIds.find(name);
            if (found) return *found;
            int id = g_nextMaterialId++;
            g_materialIds.put(name, id);
            return id;
        }

        // --- chunk stream, ported 1:1 from nextChunk/enterChunk/leaveChunk ---

        int nextChunk()
        {
            SDL_RWseek(g_in, g_chunkEnd, RW_SEEK_SET);
            if (g_chunkEnd == g_parentEnd[g_parentEnd.size() - 1]) return 0;
            unsigned short id;
            int len;
            SDL_RWread(g_in, &id, 2, 1);
            SDL_RWread(g_in, &len, 4, 1);
            g_chunkEnd = (long)SDL_RWtell(g_in) + len - 6;
            return id;
        }

        void enterChunk()
        {
            g_parentEnd.push_back(g_chunkEnd);
            g_chunkEnd = (long)SDL_RWtell(g_in);
        }

        void leaveChunk()
        {
            g_chunkEnd = g_parentEnd[g_parentEnd.size() - 1];
            g_parentEnd.pop_back();
        }

        ct::String parseString()
        {
            ct::String t;
            for (;;)
            {
                char c;
                if (SDL_RWread(g_in, &c, 1, 1) != 1 || !c) break;
                t += c;
            }
            return t;
        }

        enum
        {
            CHUNK_RGBF = 0x0010,
            CHUNK_RGBB = 0x0011,
            CHUNK_MAIN = 0x4D4D,
            CHUNK_SCENE = 0x3D3D,
            CHUNK_OBJECT = 0x4000,
            CHUNK_TRIMESH = 0x4100,
            CHUNK_VERTLIST = 0x4110,
            CHUNK_FACELIST = 0x4120,
            CHUNK_FACEMAT = 0x4130,
            CHUNK_MAPLIST = 0x4140,
            CHUNK_TRMATRIX = 0x4160,
            CHUNK_MATERIAL = 0xAFFF,
            CHUNK_MATNAME = 0xA000,
            CHUNK_AMBIENT = 0xA010,
            CHUNK_DIFFUSE = 0xA020,
            CHUNK_SPECULAR = 0xA030,
            CHUNK_TEXTURE = 0xA200,
            CHUNK_MAPFILE = 0xA300,
            CHUNK_KEYFRAMER = 0xB000,
        };

        Vector parseColor()
        {
            Vector v;
            unsigned char rgb[3];
            enterChunk();
            while (int id = nextChunk())
            {
                switch (id)
                {
                case CHUNK_RGBF:
                    SDL_RWread(g_in, &v, 12, 1);
                    break;
                case CHUNK_RGBB:
                    SDL_RWread(g_in, rgb, 3, 1);
                    v = Vector(rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f);
                    break;
                }
            }
            leaveChunk();
            return v;
        }

        void parseVertList()
        {
            unsigned short cnt;
            SDL_RWread(g_in, &cnt, 2, 1);
            while (cnt--)
            {
                Surface::Vertex v;
                SDL_RWread(g_in, &v.coords, 12, 1);
                if (g_conv) v.coords = g_convTform * v.coords;
                MeshLoader::addVertex(v);
            }
        }

        void parseFaceMat()
        {
            ct::String name = parseString();
            Brush *mat = g_materials.find(name);
            int brushId = brushIdFor(name);
            unsigned short cnt;
            SDL_RWread(g_in, &cnt, 2, 1);
            while (cnt--)
            {
                unsigned short face;
                SDL_RWread(g_in, &face, 2, 1);
                if (face < g_faces.size())
                {
                    if (mat) g_faces[face].brush = *mat;
                    g_faces[face].brushId = brushId;
                }
            }
        }

        void parseFaceList()
        {
            unsigned short cnt;
            SDL_RWread(g_in, &cnt, 2, 1);
            while (cnt--)
            {
                unsigned short v[4];
                SDL_RWread(g_in, v, 8, 1);
                Face3DS face;
                face.verts[0] = v[0];
                face.verts[1] = v[1];
                face.verts[2] = v[2];
                face.brushId = 0; // no FACEMAT chunk claims it: default/untextured material
                if (g_flipTris) std::swap(face.verts[1], face.verts[2]);
                g_faces.push_back(face);
            }
            enterChunk();
            while (int id = nextChunk())
            {
                if (id == CHUNK_FACEMAT) parseFaceMat();
            }
            leaveChunk();
        }

        void parseMapList()
        {
            unsigned short cnt;
            SDL_RWread(g_in, &cnt, 2, 1);
            for (int k = 0; k < cnt; ++k)
            {
                float uv[2];
                SDL_RWread(g_in, uv, 8, 1);
                Surface::Vertex &v = MeshLoader::refVertex(k);
                v.texCoords[0][0] = v.texCoords[1][0] = uv[0];
                v.texCoords[0][1] = v.texCoords[1][1] = 1 - uv[1];
            }
        }

        void parseTriMesh(MeshModel *mesh)
        {
            enterChunk();
            Transform tform;

            g_faces.clear();
            MeshLoader::beginMesh();

            while (int id = nextChunk())
            {
                switch (id)
                {
                case CHUNK_VERTLIST:
                    if (!g_animOnly) parseVertList();
                    break;
                case CHUNK_MAPLIST:
                    if (!g_animOnly) parseMapList();
                    break;
                case CHUNK_FACELIST:
                    if (!g_animOnly) parseFaceList();
                    break;
                case CHUNK_TRMATRIX:
                    SDL_RWread(g_in, &tform, 48, 1);
                    if (g_conv) tform = g_convTform * tform * -g_convTform;
                    break;
                }
            }
            leaveChunk();

            mesh->setWorldTform(tform);

            if (g_animOnly)
            {
                MeshLoader::endMesh(nullptr);
                return;
            }

            Transform invTform = -tform;
            for (int k = 0; k < MeshLoader::numVertices(); ++k)
            {
                Surface::Vertex &v = MeshLoader::refVertex(k);
                v.coords = invTform * v.coords;
            }

            for (size_t k = 0; k < g_faces.size(); ++k)
            {
                const Face3DS &f = g_faces[k];
                MeshLoader::addTriangle(f.verts, f.brushId, f.brush);
            }

            MeshLoader::endMesh(mesh);
            mesh->updateNormals();

            g_faces.clear();
        }

        MeshModel *parseObject(MeshModel *root)
        {
            ct::String name = parseString();
            MeshModel *mesh = nullptr;

            enterChunk();
            while (int id = nextChunk())
            {
                if (id == CHUNK_TRIMESH)
                {
                    mesh = new MeshModel();
                    mesh->setName(name);
                    mesh->setParent(root);
                    g_nameMap.put(name, mesh);
                    parseTriMesh(mesh);
                }
            }
            leaveChunk();
            return mesh;
        }

        void parseMaterial()
        {
            Brush mat;
            ct::String name, texName;
            enterChunk();
            while (int id = nextChunk())
            {
                switch (id)
                {
                case CHUNK_MATNAME:
                    name = parseString();
                    break;
                case CHUNK_DIFFUSE:
                    mat.setColor(parseColor());
                    break;
                case CHUNK_TEXTURE:
                    enterChunk();
                    while (int tid = nextChunk())
                        if (tid == CHUNK_MAPFILE) texName = parseString();
                    leaveChunk();
                    break;
                }
            }
            if (!texName.empty())
            {
                mat.setColor(Vector(1, 1, 1));
                if (g_dev)
                {
                    Texture *tex = Texture::load(texName, 0);
                    if (tex)
                    {
                        mat.setTexture(0, BrushTexture::fromTexture(*g_dev, tex, 0));
                        Texture::release(tex);
                    }
                }
            }
            if (!name.empty()) g_materials.put(name, mat);
            leaveChunk();
        }

        void parseScene(MeshModel *root)
        {
            enterChunk();
            while (int id = nextChunk())
            {
                switch (id)
                {
                case CHUNK_OBJECT:
                    parseObject(root);
                    break;
                case CHUNK_MATERIAL:
                    if (!g_animOnly) parseMaterial();
                    break;
                }
            }
            leaveChunk();
        }

        void parseAnimKeys(Animation &anim, int type)
        {
            int cnt = 0;
            short tFlags;
            SDL_RWread(g_in, &tFlags, 2, 1);
            SDL_RWseek(g_in, 8, RW_SEEK_CUR);
            SDL_RWread(g_in, &cnt, 2, 1);
            SDL_RWseek(g_in, 2, RW_SEEK_CUR);

            Vector pos, axis, scale;
            float angle;
            Quat quat;
            for (int k = 0; k < cnt; ++k)
            {
                int time;
                short flags;
                SDL_RWread(g_in, &time, 4, 1);
                SDL_RWread(g_in, &flags, 2, 1);
                float tens = 0, cont = 0, bias = 0, easeTo = 0, easeFrom = 0;
                if (flags & 1) SDL_RWread(g_in, &tens, 4, 1);
                if (flags & 2) SDL_RWread(g_in, &cont, 4, 1);
                if (flags & 4) SDL_RWread(g_in, &bias, 4, 1);
                if (flags & 8) SDL_RWread(g_in, &easeTo, 4, 1);
                if (flags & 16) SDL_RWread(g_in, &easeFrom, 4, 1);
                (void)tFlags; (void)tens; (void)cont; (void)bias; (void)easeTo; (void)easeFrom;

                switch (type)
                {
                case 0xb020: // POS_TRACK_TAG
                    SDL_RWread(g_in, &pos, 12, 1);
                    if (g_conv) pos = g_convTform * pos;
                    if (time <= g_animLen) anim.setPositionKey(time, pos);
                    break;
                case 0xb021: // ROT_TRACK_TAG
                    SDL_RWread(g_in, &angle, 4, 1);
                    SDL_RWread(g_in, &axis, 12, 1);
                    if (axis.length() > blitz::EPSILON)
                    {
                        if (g_flipTris) angle = -angle;
                        if (g_conv) axis = g_convTform.m * axis;
                        quat = Quat(std::cos(angle / 2), axis.normalized() * std::sin(angle / 2)) * quat;
                        quat = quat.normalized();
                    }
                    if (time <= g_animLen) anim.setRotationKey(time, quat);
                    break;
                case 0xb022: // SCL_TRACK_TAG
                    SDL_RWread(g_in, &scale, 12, 1);
                    if (g_conv) scale = g_convTform.m * scale;
                    if (time <= g_animLen) anim.setScaleKey(time, scale);
                    break;
                }
            }
        }

        void parseMeshInfo(MeshModel *root)
        {
            enterChunk();
            ct::String name, inst;
            Vector pivot;
            Animation anim;
            unsigned short id = 65535, parent = 65535, flags1, flags2;
            Box box{Vector(), Vector()};
            Vector boxCentre;
            (void)box; (void)boxCentre; (void)flags1; (void)flags2;

            while (int chunkId = nextChunk())
            {
                switch (chunkId)
                {
                case 0xb030: // NODE_ID
                    SDL_RWread(g_in, &id, 2, 1);
                    break;
                case 0xb010: // NODE_HDR
                    name = parseString();
                    SDL_RWread(g_in, &flags1, 2, 1);
                    SDL_RWread(g_in, &flags2, 2, 1);
                    SDL_RWread(g_in, &parent, 2, 1);
                    break;
                case 0xb011: // INSTANCE_NAME
                    inst = parseString();
                    break;
                case 0xb013: // PIVOT
                    SDL_RWread(g_in, &pivot, 12, 1);
                    if (g_conv) pivot = g_convTform * pivot;
                    break;
                case 0xb014: // BOUNDBOX
                    SDL_RWread(g_in, &box.a, 12, 1);
                    SDL_RWread(g_in, &box.b, 12, 1);
                    boxCentre = box.centre();
                    if (g_conv) boxCentre = g_convTform * boxCentre;
                    break;
                case 0xb020: // POS_TRACK_TAG
                case 0xb021: // ROT_TRACK_TAG
                case 0xb022: // SCALE_TRACK_TAG
                    if (!g_collapse) parseAnimKeys(anim, chunkId);
                    break;
                }
            }
            leaveChunk();

            MeshModel *p = root;
            if (parent != 65535)
            {
                MeshModel **found = g_idMap.find((int)parent);
                if (!found) return;
                p = *found;
            }

            MeshModel *mesh = nullptr;
            if (name == "$$$DUMMY")
            {
                mesh = new MeshModel();
                mesh->setName(inst);
                mesh->setParent(p);
            }
            else
            {
                MeshModel **found = g_nameMap.find(name);
                if (!found) return;
                mesh = *found;
                g_nameMap.erase(name);
                if (pivot != Vector()) mesh->transform(-pivot);
                Transform t = mesh->getWorldTform();
                mesh->setParent(p);
                mesh->setWorldTform(t);
            }

            mesh->setAnimation(anim);
            if (id != 65535) g_idMap.put((int)id, mesh);
        }

        void parseKeyFramer(MeshModel *root)
        {
            enterChunk();
            ct::String file3ds;
            unsigned short rev, currTime = 0;
            (void)file3ds; (void)currTime;
            while (int id = nextChunk())
            {
                switch (id)
                {
                case 0xb009: // CURR_TIME
                    SDL_RWread(g_in, &currTime, 2, 1);
                    break;
                case 0xb00a: // KFHDR
                    SDL_RWread(g_in, &rev, 2, 1);
                    file3ds = parseString();
                    SDL_RWread(g_in, &g_animLen, 2, 1);
                    break;
                case 0xb002: // object keyframer data
                    parseMeshInfo(root);
                    break;
                }
            }
            if (!g_collapse) root->setAnimator(new Animator(root, g_animLen));
            leaveChunk();
        }

        MeshModel *parseFile()
        {
            unsigned short id;
            int len;
            SDL_RWread(g_in, &id, 2, 1);
            SDL_RWread(g_in, &len, 4, 1);
            if (id != CHUNK_MAIN) return nullptr;
            g_chunkEnd = (long)SDL_RWtell(g_in) + len - 6;

            enterChunk();
            MeshModel *root = new MeshModel();
            while (int cid = nextChunk())
            {
                switch (cid)
                {
                case CHUNK_SCENE:
                    parseScene(root);
                    break;
                case CHUNK_KEYFRAMER:
                    parseKeyFramer(root);
                    break;
                }
            }
            leaveChunk();
            return root;
        }
    }

    MeshModel *LoaderB3DS::load(const ct::String &filename, const Transform &t, int hint, gpu::Device *dev)
    {
        g_convTform = t;
        g_conv = g_flipTris = false;
        if (g_convTform != Transform())
        {
            g_conv = true;
            if (g_convTform.m.i.cross(g_convTform.m.j).dot(g_convTform.m.k) < 0) g_flipTris = true;
        }

        g_collapse = (hint & MeshLoader::HintCollapse) != 0;
        g_animOnly = (hint & MeshLoader::HintAnimOnly) != 0;
        g_dev = dev;

        g_in = SDL_RWFromFile(resolveCaseInsensitive(filename).c_str(), "rb");
        if (!g_in) return nullptr;

        clearState();
        MeshModel *root = parseFile();
        SDL_RWclose(g_in);

        clearState();
        return root;
    }
}
