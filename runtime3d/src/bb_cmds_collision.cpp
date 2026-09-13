/*
** bb_cmds_collision.cpp — EntityType, EntityRadius, Collisions,
** ClearCollisions, CountCollisions, CollisionEntity, CollisionX/Y/Z,
** CollisionNX/NY/NZ, EntityCollided.
**
** World's collision engine (World::collide, addCollision, ObjCollision)
** is already fully ported and wired into UpdateWorld - these are just
** the Blitz Basic command bindings for it.
*/
#include "runtime.h"
#include "vm.h"
#include "object.h"
#include "engine/Platform.h"
#include "engine/World.h"
#include "engine/Object.h"

namespace bb3d
{
    extern engine::Platform *platform_for(zen::VM *vm);
    extern engine::World *world_for(zen::VM *vm);
    extern engine::Entity *entity_of(long long h);
    extern long long handle_of(engine::Entity *e);
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

    engine::Object *object_of(long long h)
    {
        engine::Entity *e = bb3d::entity_of(h);
        return e ? e->getObject() : nullptr;
    }

    // EntityType's optional recursive flag applies the collision type to
    // every child in the subtree too, same as the original.
    void setTypeRecursive(engine::Entity *e, int type)
    {
        if (engine::Object *o = e->getObject()) o->setCollisionType(type);
        for (engine::Entity *c = e->children(); c; c = c->successor()) setTypeRecursive(c, type);
    }
}

using namespace zen;

namespace bb3d
{
    static int c_EntityType(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        int type = (int)arg_int(args[1]);
        bool recursive = arg_int(args[2]) != 0;
        if (!e) return 0;
        if (recursive) setTypeRecursive(e, type);
        else if (engine::Object *o = e->getObject()) o->setCollisionType(type);
        return 0;
    }

    static int c_EntityRadius(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Object *o = object_of(arg_int(args[0]));
        if (!o) return 0;
        float rx = arg_float(args[1]);
        // bbEntityRadius: "y_radius ? y_radius : x_radius" - the default
        // is 0 and a zero y_radius means "use x_radius", whether it was
        // omitted or passed explicitly. radii.z mirrors x since collision
        // is an ellipsoid of revolution around Y (see World::collide's
        // use of radii.x/y).
        const float passed = arg_float(args[2]);
        const float ry = passed != 0.0f ? passed : rx;
        o->setCollisionRadii(engine::Vector(rx, ry, rx));
        return 0;
    }

    static int c_Collisions(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        int srcType = (int)arg_int(args[0]);
        int dstType = (int)arg_int(args[1]);
        int method = (int)arg_int(args[2]);
        int response = (int)arg_int(args[3]);
        world_for(vm)->addCollision(srcType, dstType, method, response);
        return 0;
    }

    static int c_ClearCollisions(VM *vm, Value *args, int nargs)
    {
        (void)args; (void)nargs;
        world_for(vm)->clearCollisions();
        return 0;
    }

    static int c_CountCollisions(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Object *o = object_of(arg_int(args[0]));
        args[0] = val_int(o ? (long long)o->getCollisions().size() : 0);
        return 1;
    }

    static const engine::ObjCollision *collisionAt(long long entityHandle, long long index)
    {
        engine::Object *o = object_of(entityHandle);
        if (!o || index < 1 || (size_t)index > o->getCollisions().size()) return nullptr;
        return o->getCollisions()[(size_t)index - 1];
    }

    static int c_CollisionEntity(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        const engine::ObjCollision *c = collisionAt(arg_int(args[0]), arg_int(args[1]));
        args[0] = val_int(c ? handle_of(c->with) : 0);
        return 1;
    }

    static int c_CollisionX(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        const engine::ObjCollision *c = collisionAt(arg_int(args[0]), arg_int(args[1]));
        args[0] = val_float(c ? c->coords.x : 0.0f);
        return 1;
    }
    static int c_CollisionY(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        const engine::ObjCollision *c = collisionAt(arg_int(args[0]), arg_int(args[1]));
        args[0] = val_float(c ? c->coords.y : 0.0f);
        return 1;
    }
    static int c_CollisionZ(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        const engine::ObjCollision *c = collisionAt(arg_int(args[0]), arg_int(args[1]));
        args[0] = val_float(c ? c->coords.z : 0.0f);
        return 1;
    }
    static int c_CollisionNX(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        const engine::ObjCollision *c = collisionAt(arg_int(args[0]), arg_int(args[1]));
        args[0] = val_float(c ? c->collision.normal.x : 0.0f);
        return 1;
    }
    static int c_CollisionNY(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        const engine::ObjCollision *c = collisionAt(arg_int(args[0]), arg_int(args[1]));
        args[0] = val_float(c ? c->collision.normal.y : 0.0f);
        return 1;
    }
    static int c_CollisionNZ(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        const engine::ObjCollision *c = collisionAt(arg_int(args[0]), arg_int(args[1]));
        args[0] = val_float(c ? c->collision.normal.z : 0.0f);
        return 1;
    }

    static int c_EntityCollided(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Object *o = object_of(arg_int(args[0]));
        int type = (int)arg_int(args[1]);
        long long result = 0;
        if (o)
        {
            for (const engine::ObjCollision *c : o->getCollisions())
            {
                if (c->with && c->with->getCollisionType() == type) { result = handle_of(c->with); break; }
            }
        }
        args[0] = val_int(result);
        return 1;
    }

    extern const zen::CommandDecl bb3d_cmds_collision[] = {
        {"EntityType%entity%collision_type%recursive=0", c_EntityType},
        {"EntityRadius%entity#x_radius#y_radius=0", c_EntityRadius},
        {"Collisions%source_type%destination_type%method%response", c_Collisions},
        {"ClearCollisions", c_ClearCollisions},
        {"%CountCollisions%entity", c_CountCollisions},
        {"%CollisionEntity%entity%collision_index", c_CollisionEntity},
        {"#CollisionX%entity%collision_index", c_CollisionX},
        {"#CollisionY%entity%collision_index", c_CollisionY},
        {"#CollisionZ%entity%collision_index", c_CollisionZ},
        {"#CollisionNX%entity%collision_index", c_CollisionNX},
        {"#CollisionNY%entity%collision_index", c_CollisionNY},
        {"#CollisionNZ%entity%collision_index", c_CollisionNZ},
        {"%EntityCollided%entity%type", c_EntityCollided},
    };
    extern const int bb3d_cmds_collision_count = (int)(sizeof(bb3d_cmds_collision) / sizeof(bb3d_cmds_collision[0]));
}
