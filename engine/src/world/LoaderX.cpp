/*
** LoaderX.cpp — see LoaderX.h. Ported from Blitz3D's loader_x.cpp
** (parseFrame/parseMesh/parseMaterial/parseAnim*), walking the xobject
** tree the vendored engine/third_party/xfile parser builds instead of
** streaming through DirectX's own IDirectXFile COM interface - same
** algorithm, same field layout, different front door.
*/
#include "engine/LoaderX.h"
#include "engine/FilePath.h"
#include "engine/MeshModel.h"
#include "engine/Animator.h"
#include "engine/Texture.h"

extern "C"
{
#include "xfile_private.h"
}
#include "xfile_templates.h"

#include <SDL2/SDL_rwops.h>
#include <ct/hashmap.hpp>
#include <ct/vector.hpp>
#include <cstring>
#include <cmath>

namespace engine
{
    namespace
    {
        XFileTemplates g_templates;
        ct::Vector<xobject> g_pool;
        ULONG g_poolUsed;
        ct::Vector<unsigned char> g_stringBuf;
        int g_animLen;
        bool g_flipTris;
        gpu::Device *g_dev;
        ct::HashMap<ct::String, MeshModel *> g_framesByName;
        // Textures loaded while parsing materials. A Brush only stores
        // the GPU handle BrushTexture::fromTexture copies out, not the
        // Texture object itself, and nothing in MeshModel/Brush's
        // destruction path releases one - so once a texture is uploaded
        // it has to stay alive for the rest of the process (same
        // contract LoaderB3D's own g_textures relies on for a script
        // that loads one mesh and keeps it). Unlike LoaderB3D, LoaderX
        // never clears/releases this list: a script routinely calls
        // LoadMesh/LoadAnimMesh more than once, and releasing a
        // previous load's textures here would free GPU resources a
        // still-live earlier mesh is still drawing with (the bug this
        // comment is here to stop someone reintroducing).
        ct::Vector<Texture *> g_textures;
        // Backing storage for parse_buffer::pxo_globals: one entry per
        // top-level object parsed so far in this file, so a nested
        // "{ObjectName}" reference (xfile_parse.c's parse_object_parts,
        // the TOKEN_OBRACE branch) can resolve to any sibling object
        // already parsed - exactly what .x skin/animation files use to
        // point an AnimationSet's Animation blocks at a named Frame
        // parsed earlier in the same file.
        ct::Vector<xobject *> g_globals;

        void resetState()
        {
            g_pool.clear();
            g_pool.resize(MAX_SUBOBJECTS * 8);
            g_poolUsed = 0;
            g_stringBuf.clear();
            g_stringBuf.resize(MAX_STRINGS_BUFFER * 4);
            g_animLen = 0;
            g_framesByName.clear();
            g_globals.clear();
            g_globals.reserve(MAX_OBJECTS);
            // g_textures is deliberately NOT cleared/released here - see
            // its declaration.
        }

        const char *templateNameFor(const GUID &type)
        {
            for (ULONG i = 0; i < g_templates.nb_xtemplates; ++i)
                if (g_templates.xtemplates[i].class_id == type) return g_templates.xtemplates[i].name;
            return nullptr;
        }
        bool isTemplate(const xobject *o, const char *name)
        {
            const char *n = templateNameFor(o->type);
            return n && xfile_stricmp(n, name) == 0;
        }

        // GetData(NULL): the object's own raw byte range, exactly what
        // loader_x.cpp's IDirectXFileData::GetData(0, ...) returned.
        BYTE *rawData(const xobject *o) { return o->root->pdata + o->pos_data; }

        Vector readVector(BYTE *&p)
        {
            float v[3];
            std::memcpy(v, p, sizeof(v));
            p += sizeof(v);
            return Vector(v[0], v[1], v[2]);
        }

        // FrameTransformMatrix's Matrix4x4 is 16 floats, row-major D3D
        // style (translation in row 3). blitz::Transform expects
        // (Matrix rows i/j/k, translation v) in the same row-major layout
        // Blitz3D's own D3DMATRIX cast used (loader_x.cpp's parseFrame).
        Transform readFrameTransform(xobject *o)
        {
            BYTE *p = rawData(o); // Matrix4x4 { array FLOAT matrix[16]; }
            float m[16];
            std::memcpy(m, p, sizeof(m));
            return Transform(blitz::Matrix(
                                  Vector(m[0], m[1], m[2]),
                                  Vector(m[4], m[5], m[6]),
                                  Vector(m[8], m[9], m[10])),
                             Vector(m[12], m[13], m[14]));
        }

        Brush readMaterial(xobject *o)
        {
            Brush brush;
            BYTE *p = rawData(o);
            // Material { ColorRGBA faceColor; FLOAT power; ColorRGB specularColor; ColorRGB emissiveColor; [...] }
            float faceColor[4];
            std::memcpy(faceColor, p, sizeof(faceColor));
            p += sizeof(faceColor);
            brush.setColor(Vector(faceColor[0], faceColor[1], faceColor[2]));
            if (faceColor[3]) brush.setAlpha(faceColor[3]);

            for (ULONG i = 0; i < o->nb_children; ++i)
            {
                xobject *child = o->children[i];
                if (isTemplate(child, "TextureFilename"))
                {
                    // STRING filename; - a string member is stored as a
                    // pointer into buf->pstrings, written by parse_object
                    BYTE *tp = rawData(child);
                    // memcpy, not a pointer cast: pdata is a packed byte
                    // stream whose offsets are only 4-byte stepped, so
                    // the stored pointer is not guaranteed to be aligned
                    // for a direct load (parse_object writes it the same
                    // way).
                    const char *filename = nullptr;
                    std::memcpy(&filename, tp, sizeof(filename));
                    Texture *tex = Texture::load(filename, 0);
                    if (tex)
                    {
                        if (g_dev) brush.setTexture(0, BrushTexture::fromTexture(*g_dev, tex, 0));
                        brush.setColor(Vector(1, 1, 1));
                        // kept alive in g_textures (see its declaration) -
                        // not released here, unlike a plain load-and-drop
                        g_textures.push_back(tex);
                    }
                }
            }
            return brush;
        }

        struct FaceX
        {
            DWORD count;
            DWORD *indices;
            int matIndex = 0;
        };

        void parseMesh(xobject *o, MeshModel *mesh)
        {
            BYTE *p = rawData(o);

            MeshLoader::beginMesh();

            const DWORD numVerts = *(DWORD *)p; p += sizeof(DWORD);
            const int firstVertex = MeshLoader::numVertices();
            for (DWORD k = 0; k < numVerts; ++k)
            {
                Surface::Vertex v;
                v.coords = readVector(p);
                v.color = ~0u;
                MeshLoader::addVertex(v);
            }

            const DWORD numFaces = *(DWORD *)p; p += sizeof(DWORD);
            ct::Vector<FaceX> faces;
            faces.resize(numFaces);
            for (DWORD k = 0; k < numFaces; ++k)
            {
                faces[k].count = *(DWORD *)p; p += sizeof(DWORD);
                faces[k].indices = (DWORD *)p;
                p += faces[k].count * sizeof(DWORD);
            }

            ct::Vector<Brush> mats;
            bool haveNormals = false;

            for (ULONG i = 0; i < o->nb_children; ++i)
            {
                xobject *child = o->children[i];
                if (isTemplate(child, "MeshMaterialList"))
                {
                    BYTE *mp = rawData(child);
                    // DWORD nMaterials; DWORD nFaceIndexes; array DWORD faceIndexes[nFaceIndexes];
                    mp += sizeof(DWORD); // nMaterials, unused: mats.size() covers it
                    const DWORD nFaceIdx = *(DWORD *)mp; mp += sizeof(DWORD);
                    DWORD *faceIdx = (DWORD *)mp;
                    for (DWORD k = 0; k < nFaceIdx && k < numFaces; ++k)
                        faces[k].matIndex = (int)faceIdx[k];

                    for (ULONG j = 0; j < child->nb_children; ++j)
                        if (isTemplate(child->children[j], "Material"))
                            mats.push_back(readMaterial(child->children[j]));
                }
                else if (isTemplate(child, "MeshTextureCoords"))
                {
                    BYTE *tp = rawData(child);
                    const DWORD nCoords = *(DWORD *)tp; tp += sizeof(DWORD);
                    if (nCoords == numVerts)
                    {
                        float *coords = (float *)tp;
                        for (DWORD k = 0; k < nCoords; ++k)
                        {
                            Surface::Vertex &v = MeshLoader::refVertex(firstVertex + (int)k);
                            v.texCoords[0][0] = v.texCoords[1][0] = coords[k * 2 + 0];
                            v.texCoords[0][1] = v.texCoords[1][1] = coords[k * 2 + 1];
                        }
                    }
                }
                else if (isTemplate(child, "MeshVertexColors"))
                {
                    BYTE *cp = rawData(child);
                    const DWORD nColors = *(DWORD *)cp; cp += sizeof(DWORD);
                    // array IndexedColor vertexColors[nVertexColors]; each
                    // IndexedColor is a nested object (DWORD index; ColorRGBA)
                    for (ULONG j = 0; j < child->nb_children && j < nColors; ++j)
                    {
                        xobject *ic = child->children[j];
                        BYTE *icp = rawData(ic);
                        const DWORD idx = *(DWORD *)icp; icp += sizeof(DWORD);
                        float rgba[4];
                        std::memcpy(rgba, icp, sizeof(rgba));
                        if ((DWORD)firstVertex + idx < (DWORD)MeshLoader::numVertices())
                        {
                            Surface::Vertex &v = MeshLoader::refVertex(firstVertex + (int)idx);
                            v.color = 0xff000000u | ((unsigned)(rgba[0] * 255) << 16) |
                                      ((unsigned)(rgba[1] * 255) << 8) | (unsigned)(rgba[2] * 255);
                        }
                    }
                }
                else if (isTemplate(child, "MeshNormals"))
                {
                    BYTE *np = rawData(child);
                    const DWORD nNormals = *(DWORD *)np; np += sizeof(DWORD);
                    if (nNormals == numVerts)
                    {
                        for (DWORD k = 0; k < nNormals; ++k)
                        {
                            Surface::Vertex &v = MeshLoader::refVertex(firstVertex + (int)k);
                            v.normal = readVector(np);
                            if (g_flipTris) v.normal = -v.normal;
                        }
                        haveNormals = true;
                    }
                }
            }
            if (!mats.size()) mats.push_back(Brush());

            for (DWORD k = 0; k < numFaces; ++k)
            {
                const FaceX &f = faces[k];
                if (f.count < 3) continue;
                const Brush &mat = mats[(size_t)f.matIndex < mats.size() ? (size_t)f.matIndex : 0];
                int tri[3];
                tri[0] = firstVertex + (int)f.indices[0];
                for (DWORD j = 2; j < f.count; ++j)
                {
                    tri[1] = firstVertex + (int)f.indices[g_flipTris ? j : j - 1];
                    tri[2] = firstVertex + (int)f.indices[g_flipTris ? j - 1 : j];
                    MeshLoader::addTriangle(tri, (int)k, mat);
                }
            }

            MeshLoader::endMesh(mesh);
            if (!haveNormals) mesh->updateNormals();
        }

        MeshModel *parseFrame(xobject *o)
        {
            MeshModel *e = new MeshModel();
            if (o->name[0]) e->setName(o->name);
            if (o->name[0]) g_framesByName.put(ct::String(o->name), e);

            for (ULONG i = 0; i < o->nb_children; ++i)
            {
                xobject *child = o->children[i];
                if (isTemplate(child, "FrameTransformMatrix"))
                {
                    e->setLocalTform(readFrameTransform(child));
                }
                else if (isTemplate(child, "Mesh"))
                {
                    parseMesh(child, e);
                }
                else if (isTemplate(child, "Frame"))
                {
                    MeshModel *sub = parseFrame(child);
                    sub->setParent(e);
                }
            }
            return e;
        }

        // AnimationKey: DWORD keyType (0=rotation quat, 1=scale, 2=position);
        // array TimedFloatKeys keys[nKeys], each { DWORD time; FloatKeys tfkeys; }
        void parseAnimKey(xobject *o, MeshModel *e)
        {
            BYTE *p = rawData(o);
            const DWORD keyType = *(DWORD *)p; p += sizeof(DWORD);
            const DWORD nKeys = *(DWORD *)p; p += sizeof(DWORD);

            Animation anim = e->getAnimation();
            for (DWORD k = 0; k < nKeys; ++k)
            {
                const int time = (int)*(DWORD *)p; p += sizeof(DWORD);
                const DWORD n = *(DWORD *)p; p += sizeof(DWORD);
                if (time > g_animLen) g_animLen = time;

                switch (keyType)
                {
                case 0:
                    if (n == 4)
                    {
                        float q[4];
                        std::memcpy(q, p, sizeof(q));
                        // D3DRM stores rotation keys as (w, x, y, z)
                        Quat rot(q[0], Vector(q[1], q[2], q[3]));
                        anim.setRotationKey(time, rot);
                    }
                    break;
                case 1:
                    if (n == 3)
                    {
                        Vector scl = readVector(p);
                        scl.x = std::fabs(scl.x); scl.y = std::fabs(scl.y); scl.z = std::fabs(scl.z);
                        anim.setScaleKey(time, scl);
                    }
                    break;
                case 2:
                    if (n == 3)
                        anim.setPositionKey(time, readVector(p));
                    break;
                }
                p += n * sizeof(float);
            }
            e->setAnimation(anim);
        }

        void parseAnim(xobject *o)
        {
            MeshModel *frame = nullptr;
            for (ULONG i = 0; i < o->nb_children; ++i)
            {
                xobject *child = o->children[i];
                if (isTemplate(child, "Frame"))
                {
                    MeshModel **found = g_framesByName.find(ct::String(child->name));
                    if (found) frame = *found;
                }
                else if (isTemplate(child, "AnimationKey") && frame)
                {
                    parseAnimKey(child, frame);
                }
            }
        }

        void parseAnimSet(xobject *o)
        {
            for (ULONG i = 0; i < o->nb_children; ++i)
                if (isTemplate(o->children[i], "Animation")) parseAnim(o->children[i]);
        }

        MeshModel *parseTopLevel(parse_buffer &buf, bool collapse, bool animOnly)
        {
            MeshModel *root = new MeshModel();

            while (xfile_check_token(&buf) != XFILE_TOKEN_NONE)
            {
                if (!xfile_parse_templates(&buf, TRUE)) break;
                if (xfile_check_token(&buf) == XFILE_TOKEN_NONE) break;
                if (g_poolUsed + 1 >= g_pool.size()) break;

                buf.pxo = buf.pxo_tab = &g_pool[g_poolUsed];
                buf.pxo->nb_subobjects = 1;
                buf.pdata = NULL;
                buf.capacity = 0;
                buf.cur_pos_data = 0;

                // pxo_globals[0..nb_pxo_globals) are the completed
                // top-level objects parsed before this one; slot
                // nb_pxo_globals itself is this object's own root, so a
                // "{name}" reference inside it can also resolve to one of
                // its own already-parsed siblings/children.
                if (g_globals.size() >= MAX_OBJECTS) break;
                g_globals.push_back(buf.pxo_tab);
                buf.pxo_globals = g_globals.data();
                buf.nb_pxo_globals = (ULONG)g_globals.size() - 1;

                if (!xfile_parse_object(&buf))
                {
                    free(buf.pdata);
                    g_globals.pop_back();
                    break;
                }

                xobject *top = &g_pool[g_poolUsed];
                g_poolUsed += top->nb_subobjects;

                if (isTemplate(top, "Frame"))
                {
                    if (!animOnly)
                    {
                        MeshModel *sub = parseFrame(top);
                        sub->setParent(root);
                    }
                }
                else if (isTemplate(top, "Mesh"))
                {
                    if (!animOnly) parseMesh(top, root);
                }
                else if (isTemplate(top, "AnimationSet"))
                {
                    if (!collapse) parseAnimSet(top);
                }

                // every field this top-level object's subtree held has
                // now been copied out (into Surface::Vertex data or an
                // Animation's keys) - its raw byte buffer is done
                free(top->pdata);
            }
            return root;
        }

        // bbLoadMesh's collapseMesh: LoadMesh (unlike LoadAnimMesh) walks
        // the whole Frame hierarchy depth-first, bakes each child's world
        // transform into its own vertices, and folds every child's
        // surfaces into one destination MeshModel - because a plain
        // LoadMesh handle is expected to be a single flat mesh a script
        // can ScaleMesh/FlipMesh/FitMesh directly, not a Frame tree only
        // LoadAnimMesh callers know how to walk. Root itself keeps
        // whatever surfaces LoadMesh already gave it (a top-level Mesh
        // with no Frame around it never has children to fold).
        void collapseMesh(MeshModel *dest, Entity *e)
        {
            while (e->children()) collapseMesh(dest, e->children());
            if (Model *m = e->getModel())
                if (MeshModel *t = m->getMeshModel())
                {
                    t->transform(e->getWorldTform());
                    dest->add(*t);
                }
            delete e;
        }
    }

    MeshModel *LoaderX::load(const ct::String &f, const Transform &conv, int hint, gpu::Device *dev)
    {
        const ct::String path = resolveCaseInsensitive(f);
        SDL_RWops *rw = SDL_RWFromFile(path.c_str(), "rb");
        if (!rw) return nullptr;

        // TextureFilename fields inside the .x file (readMaterial) are
        // relative to the model's own directory, same convention as
        // LoaderB3D::load - set it for the duration of this parse.
        size_t slash = path.find_last_of("/\\");
        setTexturePath(slash == ct::String::npos ? ct::String() : path.substr(0, slash));

        const Sint64 sz = SDL_RWsize(rw);
        if (sz <= 0) { SDL_RWclose(rw); setTexturePath(ct::String()); return nullptr; }

        ct::Vector<unsigned char> fileBuf((size_t)sz, 0);
        Sint64 got = 0;
        while (got < sz)
        {
            size_t n = SDL_RWread(rw, fileBuf.data() + got, 1, (size_t)(sz - got));
            if (n == 0) break;
            got += (Sint64)n;
        }
        SDL_RWclose(rw);
        if (got != sz) { setTexturePath(ct::String()); return nullptr; }

        g_dev = dev;
        resetState();
        xfile_reserve_index_color(&g_templates);

        {
            parse_buffer tplBuf;
            std::memset(&tplBuf, 0, sizeof(tplBuf));
            tplBuf.buffer = (BYTE *)kXFileStandardTemplates;
            tplBuf.rem_bytes = (DWORD)(sizeof(kXFileStandardTemplates) - 1);
            tplBuf.pdxf = &g_templates;
            BYTE *decomp = nullptr;
            if (xfile_parse_header(&tplBuf, &decomp) != XFILE_OK) { setTexturePath(ct::String()); return nullptr; }
            if (!xfile_parse_templates(&tplBuf, FALSE)) { setTexturePath(ct::String()); return nullptr; }
        }

        parse_buffer buf;
        std::memset(&buf, 0, sizeof(buf));
        buf.buffer = fileBuf.data();
        buf.rem_bytes = (DWORD)fileBuf.size();
        buf.pdxf = &g_templates;

        BYTE *decomp = nullptr;
        if (xfile_parse_header(&buf, &decomp) != XFILE_OK) { setTexturePath(ct::String()); return nullptr; }

        buf.pstrings = buf.cur_pstrings = g_stringBuf.data();

        // conv_tform in loader_x.cpp: an identity conversion means the
        // caller (LoadMesh, no .x default in this runtime yet) wants the
        // file's own coordinates untouched.
        (void)conv;

        const bool collapse = (hint & MeshLoader::HintCollapse) != 0;
        const bool animOnly = (hint & MeshLoader::HintAnimOnly) != 0;
        g_flipTris = false;

        MeshModel *root = parseTopLevel(buf, collapse, animOnly);

        if (!collapse) root->setAnimator(new Animator(root, g_animLen));
        else
        {
            // fold every Frame child into root, exactly as bbLoadMesh did
            // via collapseMesh - LoadMesh's caller (bb_cmds_world.cpp)
            // passes HintCollapse for this; LoadAnimMesh does not, and
            // keeps the Frame hierarchy this branch would otherwise erase.
            while (root->children()) collapseMesh(root, root->children());
        }

        free(decomp);
        g_framesByName.clear();
        setTexturePath(ct::String());
        return root;
    }
}
