/*
** bb_cmds_entityquery.cpp — EntityX/EntityY/EntityZ, ResetEntity,
** AlignToVector, the TForm* coordinate-space family and GetEntityType.
** Ported from bbblitz3d.cpp; this is a separate file (not
** bb_cmds_world.cpp, which is being worked on in parallel) so these
** land without touching anyone else's in-progress edits.
*/
#include "runtime.h"
#include "vm.h"
#include "object.h"
#include "engine/Entity.h"
#include "engine/Object.h"

namespace bb3d
{
    extern engine::Entity *entity_of(long long h);
}

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
}

using namespace zen;

namespace bb3d
{
    static int c_EntityX(VM *vm, Value *args, int)
    {
        (void)vm;
        engine::Entity *e = entity_of(arg_int(args[0]));
        const bool global = arg_int(args[1]) != 0;
        args[0] = val_float(e ? (global ? e->getWorldPosition().x : e->getLocalPosition().x) : 0.0f);
        return 1;
    }
    static int c_EntityY(VM *vm, Value *args, int)
    {
        (void)vm;
        engine::Entity *e = entity_of(arg_int(args[0]));
        const bool global = arg_int(args[1]) != 0;
        args[0] = val_float(e ? (global ? e->getWorldPosition().y : e->getLocalPosition().y) : 0.0f);
        return 1;
    }
    static int c_EntityZ(VM *vm, Value *args, int)
    {
        (void)vm;
        engine::Entity *e = entity_of(arg_int(args[0]));
        const bool global = arg_int(args[1]) != 0;
        args[0] = val_float(e ? (global ? e->getWorldPosition().z : e->getLocalPosition().z) : 0.0f);
        return 1;
    }

    static int c_ResetEntity(VM *vm, Value *args, int)
    {
        (void)vm;
        engine::Entity *e = entity_of(arg_int(args[0]));
        if (e) e->getObject()->reset();
        return 0;
    }

    /* bbAlignToVector: rotates the entity's axis ("axis": 1=i/x, 2=j/y,
       3=k/z) towards the given direction at the given rate (0..1) per
       call, the same incremental turn Blitz3D used for banking a vehicle
       into its own velocity vector frame by frame. */
    static int c_AlignToVector(VM *vm, Value *args, int)
    {
        (void)vm;
        engine::Entity *e = entity_of(arg_int(args[0]));
        if (!e) return 0;

        engine::Vector axisVec(arg_float(args[1]), arg_float(args[2]), arg_float(args[3]));
        const float len = axisVec.length();
        if (len <= blitz::EPSILON) return 0;
        axisVec = axisVec / len;

        const int axis = (int)arg_int(args[4]);
        const float rate = arg_float(args[5]);

        engine::Quat q = e->getWorldRotation();
        const engine::Vector tv = axis == 1 ? q.i() : axis == 2 ? q.j() : q.k();

        const float dp = axisVec.dot(tv);
        if (dp >= 1.0f - blitz::EPSILON) return 0;

        if (dp <= -1.0f + blitz::EPSILON)
        {
            const float an = blitz::PI * rate * 0.5f;
            const engine::Vector cp = axis == 1 ? q.j() : axis == 2 ? q.k() : q.i();
            e->setWorldRotation(engine::Quat(cosf(an), cp * sinf(an)) * q);
            return 0;
        }

        const float an = acosf(dp) * rate * 0.5f;
        const engine::Vector cp = axisVec.cross(tv).normalized();
        e->setWorldRotation(engine::Quat(cosf(an), cp * sinf(an)) * q);
        return 0;
    }


    /* bbTFormPoint/bbTFormVector/bbTFormNormal and the TFormedX/Y/Z
       readbacks (bbblitz3d.cpp:1608-1650). All three write the same
       static "tformed" vector the readbacks return, exactly as the
       original did - the docs describe TFormedX/Y/Z as "the last
       TFormPoint, TFormVector or TFormNormal operation", so one shared
       result is the specified behaviour, not a shortcut.

       src/dest of 0 means "the 3d world", i.e. no transform applied on
       that side; -tform is Transform's inverse (Geom.h's operator-). */
    static engine::Vector g_tformed;

    static int c_TFormPoint(VM *vm, Value *args, int)
    {
        (void)vm;
        engine::Entity *src = entity_of(arg_int(args[3]));
        engine::Entity *dest = entity_of(arg_int(args[4]));
        g_tformed = engine::Vector(arg_float(args[0]), arg_float(args[1]), arg_float(args[2]));
        if (src) g_tformed = src->getWorldTform() * g_tformed;
        if (dest) g_tformed = -dest->getWorldTform() * g_tformed;
        return 0;
    }

    /* A vector differs from a point in ignoring translation: the
       original multiplies by the Transform's rotation/scale matrix
       (.m) only. */
    static int c_TFormVector(VM *vm, Value *args, int)
    {
        (void)vm;
        engine::Entity *src = entity_of(arg_int(args[3]));
        engine::Entity *dest = entity_of(arg_int(args[4]));
        g_tformed = engine::Vector(arg_float(args[0]), arg_float(args[1]), arg_float(args[2]));
        if (src) g_tformed = src->getWorldTform().m * g_tformed;
        if (dest) g_tformed = (-dest->getWorldTform()).m * g_tformed;
        return 0;
    }

    /* A normal needs the cofactor (inverse-transpose) matrix so it stays
       perpendicular to the surface under non-uniform scaling, and is
       renormalized afterwards - both straight from the original. */
    static int c_TFormNormal(VM *vm, Value *args, int)
    {
        (void)vm;
        engine::Entity *src = entity_of(arg_int(args[3]));
        engine::Entity *dest = entity_of(arg_int(args[4]));
        g_tformed = engine::Vector(arg_float(args[0]), arg_float(args[1]), arg_float(args[2]));
        if (src) g_tformed = src->getWorldTform().m.cofactor() * g_tformed;
        if (dest) g_tformed = (-dest->getWorldTform()).m.cofactor() * g_tformed;
        const float len = g_tformed.length();
        if (len > blitz::EPSILON) g_tformed = g_tformed / len;
        return 0;
    }

    static int c_TFormedX(VM *vm, Value *args, int)
    {
        (void)vm;
        args[0] = val_float(g_tformed.x);
        return 1;
    }
    static int c_TFormedY(VM *vm, Value *args, int)
    {
        (void)vm;
        args[0] = val_float(g_tformed.y);
        return 1;
    }
    static int c_TFormedZ(VM *vm, Value *args, int)
    {
        (void)vm;
        args[0] = val_float(g_tformed.z);
        return 1;
    }

    /* bbGetEntityType (bbblitz3d.cpp:1716): the collision type set by
       EntityType. */
    static int c_GetEntityType(VM *vm, Value *args, int)
    {
        (void)vm;
        engine::Entity *e = entity_of(arg_int(args[0]));
        args[0] = val_int(e && e->getObject() ? e->getObject()->getCollisionType() : 0);
        return 1;
    }

    extern const zen::CommandDecl bb3d_cmds_entityquery[] = {
        {"#EntityX%entity%global=0", c_EntityX},
        {"#EntityY%entity%global=0", c_EntityY},
        {"#EntityZ%entity%global=0", c_EntityZ},
        {"ResetEntity%entity", c_ResetEntity},
        {"AlignToVector%entity#vector_x#vector_y#vector_z%axis#rate=1", c_AlignToVector},
        {"TFormPoint#x#y#z%source_entity%dest_entity", c_TFormPoint},
        {"TFormVector#x#y#z%source_entity%dest_entity", c_TFormVector},
        {"TFormNormal#x#y#z%source_entity%dest_entity", c_TFormNormal},
        {"#TFormedX", c_TFormedX},
        {"#TFormedY", c_TFormedY},
        {"#TFormedZ", c_TFormedZ},
        {"%GetEntityType%entity", c_GetEntityType},
    };
    extern const int bb3d_cmds_entityquery_count =
        (int)(sizeof(bb3d_cmds_entityquery) / sizeof(bb3d_cmds_entityquery[0]));
}
