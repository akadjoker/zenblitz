/*
** bb_cmds_meshutil.cpp — FlipMesh, PaintMesh, ScaleMesh, FitMesh,
** LightMesh.
**
** MeshModel::flipTriangles()/paint()/transform() and
** MeshUtil::lightMesh()/scaleMesh()/fitMesh() already carry the real
** logic (ported from the original engine's meshmodel.cpp/meshutil.cpp);
** these are just the Blitz Basic command bindings.
*/
#include "runtime.h"
#include "vm.h"
#include "object.h"
#include "engine/MeshModel.h"
#include "engine/MeshUtil.h"

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
}

using namespace zen;

namespace bb3d
{
    static int c_FlipMesh(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::MeshModel *m = mesh_of(arg_int(args[0]));
        if (m) m->flipTriangles();
        return 0;
    }

    static int c_PaintMesh(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::MeshModel *m = mesh_of(arg_int(args[0]));
        engine::Brush *b = (engine::Brush *)(std::intptr_t)arg_int(args[1]);
        if (m && b) m->paint(*b);
        return 0;
    }

    static int c_ScaleMesh(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::MeshModel *m = mesh_of(arg_int(args[0]));
        if (m) engine::MeshUtil::scaleMesh(m, engine::Vector(arg_float(args[1]), arg_float(args[2]), arg_float(args[3])));
        return 0;
    }

    static int c_FitMesh(VM *vm, Value *args, int nargs)
    {
        (void)vm;
        engine::MeshModel *m = mesh_of(arg_int(args[0]));
        if (m)
        {
            engine::Vector pos(arg_float(args[1]), arg_float(args[2]), arg_float(args[3]));
            engine::Vector size(arg_float(args[4]), arg_float(args[5]), arg_float(args[6]));
            bool uniform = nargs > 7 && arg_int(args[7]) != 0;
            engine::MeshUtil::fitMesh(m, pos, size, uniform);
        }
        return 0;
    }

    static int c_LightMesh(VM *vm, Value *args, int nargs)
    {
        (void)vm;
        engine::MeshModel *m = mesh_of(arg_int(args[0]));
        if (m)
        {
            engine::Vector rgb(arg_float(args[1]) / 255.0f, arg_float(args[2]) / 255.0f, arg_float(args[3]) / 255.0f);
            float range = nargs > 4 ? arg_float(args[4]) : 0.0f;
            engine::Vector pos(nargs > 5 ? arg_float(args[5]) : 0.0f, nargs > 6 ? arg_float(args[6]) : 0.0f,
                                nargs > 7 ? arg_float(args[7]) : 0.0f);
            engine::MeshUtil::lightMesh(m, pos, rgb, range);
        }
        return 0;
    }

    extern const zen::CommandDecl bb3d_cmds_meshutil[] = {
        {"FlipMesh%mesh", c_FlipMesh},
        {"PaintMesh%mesh%brush", c_PaintMesh},
        {"ScaleMesh%mesh#x_scale#y_scale#z_scale", c_ScaleMesh},
        {"FitMesh%mesh#x#y#z#width#height#depth%uniform=0", c_FitMesh},
        {"LightMesh%mesh#red#green#blue#range=0#x=0#y=0#z=0", c_LightMesh},
    };
    extern const int bb3d_cmds_meshutil_count = (int)(sizeof(bb3d_cmds_meshutil) / sizeof(bb3d_cmds_meshutil[0]));
}
