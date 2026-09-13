#include "runtime.h"
#include "vm.h"
#include "object.h"
#include "engine/MeshModel.h"
#include <cstdint>

namespace bb3d { extern engine::Entity *entity_of(long long h); }

namespace
{
    inline long long arg_int(zen::Value v)
    {
        return zen::is_int(v) ? v.as.integer : zen::is_float(v) ? (long long)v.as.number : 0;
    }
    inline float arg_float(zen::Value v)
    {
        return zen::is_float(v) ? (float)v.as.number : zen::is_int(v) ? (float)v.as.integer : 0.0f;
    }

    engine::MeshModel *mesh_of(long long h)
    {
        engine::Entity *e = bb3d::entity_of(h);
        engine::Model *m = e ? e->getModel() : nullptr;
        return m ? m->getMeshModel() : nullptr;
    }

    engine::Surface *surface_of(long long h)
    {
        return (engine::Surface *)(std::intptr_t)h;
    }

    long long handle_of(engine::Surface *s)
    {
        return (long long)(std::intptr_t)s;
    }
}

using namespace zen;

namespace bb3d
{
    static int c_CreateSurface(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::MeshModel *mesh = mesh_of(arg_int(args[0]));
        engine::Brush *brush = (engine::Brush *)(std::intptr_t)arg_int(args[1]);
        args[0] = val_int(handle_of(mesh ? mesh->createSurface(brush ? *brush : engine::Brush()) : nullptr));
        return 1;
    }

    static int c_AddVertex(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Surface *surface = surface_of(arg_int(args[0]));
        if (!surface) { args[0] = val_int(0); return 1; }
        engine::Surface::Vertex vertex;
        vertex.coords = engine::Vector(arg_float(args[1]), arg_float(args[2]), arg_float(args[3]));
        vertex.texCoords[0][0] = arg_float(args[4]);
        vertex.texCoords[0][1] = arg_float(args[5]);
        args[0] = val_int(surface->addVertex(vertex));
        return 1;
    }

    static int c_AddTriangle(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Surface *surface = surface_of(arg_int(args[0]));
        if (!surface) { args[0] = val_int(0); return 1; }
        engine::Surface::Triangle triangle{{(unsigned short)arg_int(args[1]), (unsigned short)arg_int(args[2]), (unsigned short)arg_int(args[3])}};
        args[0] = val_int(surface->addTriangle(triangle));
        return 1;
    }

    static int c_CountSurfaces(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::MeshModel *m = mesh_of(arg_int(args[0]));
        args[0] = val_int(m ? m->getSurfaces().size() : 0);
        return 1;
    }

    static int c_GetSurface(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::MeshModel *m = mesh_of(arg_int(args[0]));
        int index = (int)arg_int(args[1]);
        engine::Surface *s = nullptr;
        if (m && index >= 1 && index <= (int)m->getSurfaces().size())
            s = m->getSurfaces()[index - 1];
        args[0] = val_int(handle_of(s));
        return 1;
    }

    static int c_CountVertices(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Surface *s = surface_of(arg_int(args[0]));
        args[0] = val_int(s ? s->numVertices() : 0);
        return 1;
    }

    static int c_CountTriangles(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Surface *s = surface_of(arg_int(args[0]));
        args[0] = val_int(s ? s->numTriangles() : 0);
        return 1;
    }

    static int c_ClearSurface(VM *vm, Value *args, int nargs)
    {
        (void)vm;
        engine::Surface *s = surface_of(arg_int(args[0]));
        if (s) s->clear(nargs > 1 ? arg_int(args[1]) != 0 : true, nargs > 2 ? arg_int(args[2]) != 0 : true);
        return 0;
    }

    static int c_VertexCoords(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Surface *s = surface_of(arg_int(args[0]));
        if (s) s->setCoords((int)arg_int(args[1]),
                            engine::Vector(arg_float(args[2]), arg_float(args[3]), arg_float(args[4])));
        return 0;
    }

    static int c_VertexNormal(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Surface *s = surface_of(arg_int(args[0]));
        if (s) s->setNormal((int)arg_int(args[1]),
                            engine::Vector(arg_float(args[2]), arg_float(args[3]), arg_float(args[4])));
        return 0;
    }

    static int c_VertexColor(VM *vm, Value *args, int nargs)
    {
        (void)vm;
        engine::Surface *s = surface_of(arg_int(args[0]));
        if (!s) return 0;
        float r = arg_float(args[2]), g = arg_float(args[3]), b = arg_float(args[4]);
        float a = (nargs > 5 ? arg_float(args[5]) : 1.0f) * 255.0f;
        if (r < 0) r = 0; else if (r > 255) r = 255;
        if (g < 0) g = 0; else if (g > 255) g = 255;
        if (b < 0) b = 0; else if (b > 255) b = 255;
        if (a < 0) a = 0; else if (a > 255) a = 255;
        unsigned argb = ((unsigned)a << 24) | ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
        s->setColor((int)arg_int(args[1]), argb);
        return 0;
    }

    static int c_VertexTexCoords(VM *vm, Value *args, int nargs)
    {
        (void)vm;
        engine::Surface *s = surface_of(arg_int(args[0]));
        if (s) s->setTexCoords((int)arg_int(args[1]), arg_float(args[2]), arg_float(args[3]),
                               nargs > 5 ? (int)arg_int(args[5]) : 0);
        return 0;
    }

    static int c_VertexX(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Surface *s = surface_of(arg_int(args[0]));
        args[0] = val_float(s ? s->getVertex((int)arg_int(args[1])).coords.x : 0.0f);
        return 1;
    }
    static int c_VertexY(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Surface *s = surface_of(arg_int(args[0]));
        args[0] = val_float(s ? s->getVertex((int)arg_int(args[1])).coords.y : 0.0f);
        return 1;
    }
    static int c_VertexZ(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Surface *s = surface_of(arg_int(args[0]));
        args[0] = val_float(s ? s->getVertex((int)arg_int(args[1])).coords.z : 0.0f);
        return 1;
    }

    static int c_VertexNX(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Surface *s = surface_of(arg_int(args[0]));
        args[0] = val_float(s ? s->getVertex((int)arg_int(args[1])).normal.x : 0.0f);
        return 1;
    }
    static int c_VertexNY(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Surface *s = surface_of(arg_int(args[0]));
        args[0] = val_float(s ? s->getVertex((int)arg_int(args[1])).normal.y : 0.0f);
        return 1;
    }
    static int c_VertexNZ(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Surface *s = surface_of(arg_int(args[0]));
        args[0] = val_float(s ? s->getVertex((int)arg_int(args[1])).normal.z : 0.0f);
        return 1;
    }

    static int c_VertexRed(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Surface *s = surface_of(arg_int(args[0]));
        unsigned c = s ? s->getVertex((int)arg_int(args[1])).color : 0;
        args[0] = val_float((float)((c & 0xff0000u) >> 16));
        return 1;
    }
    static int c_VertexGreen(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Surface *s = surface_of(arg_int(args[0]));
        unsigned c = s ? s->getVertex((int)arg_int(args[1])).color : 0;
        args[0] = val_float((float)((c & 0xff00u) >> 8));
        return 1;
    }
    static int c_VertexBlue(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Surface *s = surface_of(arg_int(args[0]));
        unsigned c = s ? s->getVertex((int)arg_int(args[1])).color : 0;
        args[0] = val_float((float)(c & 0xffu));
        return 1;
    }
    static int c_VertexAlpha(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Surface *s = surface_of(arg_int(args[0]));
        unsigned c = s ? s->getVertex((int)arg_int(args[1])).color : 0;
        args[0] = val_float(((float)((c & 0xff000000u) >> 24)) / 255.0f);
        return 1;
    }

    static int c_VertexU(VM *vm, Value *args, int nargs)
    {
        (void)vm;
        engine::Surface *s = surface_of(arg_int(args[0]));
        int set = nargs > 2 ? (int)arg_int(args[2]) : 0;
        args[0] = val_float(s ? s->getVertex((int)arg_int(args[1])).texCoords[set][0] : 0.0f);
        return 1;
    }
    static int c_VertexV(VM *vm, Value *args, int nargs)
    {
        (void)vm;
        engine::Surface *s = surface_of(arg_int(args[0]));
        int set = nargs > 2 ? (int)arg_int(args[2]) : 0;
        args[0] = val_float(s ? s->getVertex((int)arg_int(args[1])).texCoords[set][1] : 0.0f);
        return 1;
    }
    static int c_VertexW(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        args[0] = val_float(1.0f);
        return 1;
    }

    static int c_TriangleVertex(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Surface *s = surface_of(arg_int(args[0]));
        args[0] = val_int(s ? s->getTriangle((int)arg_int(args[1])).verts[(int)arg_int(args[2])] : 0);
        return 1;
    }

    extern const zen::CommandDecl bb3d_cmds_surface[] = {
        {"%CreateSurface%mesh%brush=0", c_CreateSurface},
        {"%AddVertex%surface#x#y#z#u=0#v=0#w=1", c_AddVertex},
        {"%AddTriangle%surface%v0%v1%v2", c_AddTriangle},
        {"%CountSurfaces%mesh", c_CountSurfaces},
        {"%GetSurface%mesh%surface_index", c_GetSurface},
        {"%CountVertices%surface", c_CountVertices},
        {"%CountTriangles%surface", c_CountTriangles},
        {"ClearSurface%surface%clear_vertices=1%clear_triangles=1", c_ClearSurface},
        {"VertexCoords%surface%index#x#y#z", c_VertexCoords},
        {"VertexNormal%surface%index#nx#ny#nz", c_VertexNormal},
        {"VertexColor%surface%index#red#green#blue#alpha=1", c_VertexColor},
        {"VertexTexCoords%surface%index#u#v#w=1%coord_set=0", c_VertexTexCoords},
        {"#VertexX%surface%index", c_VertexX},
        {"#VertexY%surface%index", c_VertexY},
        {"#VertexZ%surface%index", c_VertexZ},
        {"#VertexNX%surface%index", c_VertexNX},
        {"#VertexNY%surface%index", c_VertexNY},
        {"#VertexNZ%surface%index", c_VertexNZ},
        {"#VertexRed%surface%index", c_VertexRed},
        {"#VertexGreen%surface%index", c_VertexGreen},
        {"#VertexBlue%surface%index", c_VertexBlue},
        {"#VertexAlpha%surface%index", c_VertexAlpha},
        {"#VertexU%surface%index%coord_set=0", c_VertexU},
        {"#VertexV%surface%index%coord_set=0", c_VertexV},
        {"#VertexW%surface%index%coord_set=0", c_VertexW},
        {"%TriangleVertex%surface%index%vertex", c_TriangleVertex},
    };
    extern const int bb3d_cmds_surface_count = (int)(sizeof(bb3d_cmds_surface) / sizeof(bb3d_cmds_surface[0]));
}
