/*
** bb_cmds_world.cpp — CreateCube/Sphere/Cylinder/Cone/Camera/Light/Pivot,
** Position/Rotate/Scale/Move/Turn/Translate/PointEntity, EntityColor/
** Alpha/FX/Blend/Order, AmbientLight, RenderWorld, UpdateWorld.
*/
#include "runtime.h"
#include "vm.h"
#include "object.h"
#include "ct/hashmap.hpp"
#include "engine/Platform.h"
#include "engine/World.h"
#include "engine/MeshUtil.h"
#include "engine/Camera.h"
#include "engine/Light.h"
#include "engine/MD2Model.h"

namespace bb3d
{
    extern engine::Platform *platform_for(zen::VM *vm);
    extern engine::World *world_for(zen::VM *vm);
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
    constexpr float kDegToRad = 0.0174532925199432957692369076848861f;
}

using namespace zen;

namespace bb3d
{
    static ct::HashMap<long long, engine::Entity *> g_entities;
    static long long g_next_entity = 0;

    engine::Entity *entity_of(long long h)
    {
        engine::Entity **found = g_entities.find(h);
        return found ? *found : nullptr;
    }

    static long long store_entity(engine::Entity *e)
    {
        long long h = ++g_next_entity;
        g_entities.put(h, e);
        return h;
    }

    static void insert_entity(engine::Entity *e, engine::Entity *parent)
    {
        if (parent) e->setParent(parent);
    }

    static int c_CreateCube(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *parent = entity_of(arg_int(args[0]));
        engine::Brush b;
        engine::Entity *e = engine::MeshUtil::createCube(b);
        insert_entity(e, parent);
        args[0] = val_int(store_entity(e));
        return 1;
    }

    static int c_CreateSphere(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        int segs = (int)arg_int(args[0]);
        engine::Entity *parent = entity_of(arg_int(args[1]));
        engine::Brush b;
        engine::Entity *e = engine::MeshUtil::createSphere(b, segs);
        insert_entity(e, parent);
        args[0] = val_int(store_entity(e));
        return 1;
    }

    static int c_CreateCylinder(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        int segs = (int)arg_int(args[0]);
        bool solid = arg_int(args[1]) != 0;
        engine::Entity *parent = entity_of(arg_int(args[2]));
        engine::Brush b;
        engine::Entity *e = engine::MeshUtil::createCylinder(b, segs, solid);
        insert_entity(e, parent);
        args[0] = val_int(store_entity(e));
        return 1;
    }

    static int c_CreateCone(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        int segs = (int)arg_int(args[0]);
        bool solid = arg_int(args[1]) != 0;
        engine::Entity *parent = entity_of(arg_int(args[2]));
        engine::Brush b;
        engine::Entity *e = engine::MeshUtil::createCone(b, segs, solid);
        insert_entity(e, parent);
        args[0] = val_int(store_entity(e));
        return 1;
    }

    static int c_CreateCamera(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        engine::Entity *parent = entity_of(arg_int(args[0]));
        engine::Camera *cam = new engine::Camera();
        cam->setViewport(0, 0, platform_for(vm)->width(), platform_for(vm)->height());
        insert_entity(cam, parent);
        args[0] = val_int(store_entity(cam));
        return 1;
    }

    static int c_CreateLight(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        int type = (int)arg_int(args[0]);
        engine::Entity *parent = entity_of(arg_int(args[1]));
        engine::Light *light = new engine::Light(type ? type : engine::Light::LightPoint);
        insert_entity(light, parent);
        args[0] = val_int(store_entity(light));
        return 1;
    }

    static int c_CreatePivot(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *parent = entity_of(arg_int(args[0]));
        engine::Object *e = new engine::Object();
        insert_entity(e, parent);
        args[0] = val_int(store_entity(e));
        return 1;
    }

    static int c_FreeEntity(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        if (e) { g_entities.erase(arg_int(args[0])); delete e; }
        return 0;
    }

    static int c_HideEntity(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        if (e) { e->setEnabled(false); e->setVisible(false); }
        return 0;
    }

    static int c_ShowEntity(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        if (e) { e->setVisible(true); e->setEnabled(true); if (e->getObject()) e->getObject()->reset(); }
        return 0;
    }

    static int c_EntityParent(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        engine::Entity *p = entity_of(arg_int(args[1]));
        bool global = arg_int(args[2]) != 0;
        if (!e || e->getParent() == p) return 0;
        if (global)
        {
            engine::Transform t = e->getWorldTform();
            e->setParent(p);
            e->setWorldTform(t);
        }
        else
        {
            e->setParent(p);
            if (e->getObject()) e->getObject()->reset();
        }
        return 0;
    }

    static int c_PositionEntity(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        if (!e) return 0;
        engine::Vector v(arg_float(args[1]), arg_float(args[2]), arg_float(args[3]));
        bool global = arg_int(args[4]) != 0;
        global ? e->setWorldPosition(v) : e->setLocalPosition(v);
        return 0;
    }

    static int c_RotateEntity(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        if (!e) return 0;
        engine::Quat q = blitz::rotationQuat(arg_float(args[1]) * kDegToRad, arg_float(args[2]) * kDegToRad, arg_float(args[3]) * kDegToRad);
        bool global = arg_int(args[4]) != 0;
        global ? e->setWorldRotation(q) : e->setLocalRotation(q);
        return 0;
    }

    static int c_ScaleEntity(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        if (!e) return 0;
        engine::Vector v(arg_float(args[1]), arg_float(args[2]), arg_float(args[3]));
        bool global = arg_int(args[4]) != 0;
        global ? e->setWorldScale(v) : e->setLocalScale(v);
        return 0;
    }

    static int c_MoveEntity(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        if (!e) return 0;
        engine::Vector v(arg_float(args[1]), arg_float(args[2]), arg_float(args[3]));
        e->setLocalPosition(e->getLocalPosition() + e->getLocalRotation() * v);
        return 0;
    }

    static int c_TurnEntity(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        if (!e) return 0;
        engine::Quat q = blitz::rotationQuat(arg_float(args[1]) * kDegToRad, arg_float(args[2]) * kDegToRad, arg_float(args[3]) * kDegToRad);
        bool global = arg_int(args[4]) != 0;
        global ? e->setWorldRotation(q * e->getWorldRotation()) : e->setLocalRotation(e->getLocalRotation() * q);
        return 0;
    }

    static int c_TranslateEntity(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        if (!e) return 0;
        engine::Vector v(arg_float(args[1]), arg_float(args[2]), arg_float(args[3]));
        bool global = arg_int(args[4]) != 0;
        global ? e->setWorldPosition(e->getWorldPosition() + v) : e->setLocalPosition(e->getLocalPosition() + v);
        return 0;
    }

    static int c_PointEntity(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        engine::Entity *t = entity_of(arg_int(args[1]));
        float roll = arg_float(args[2]);
        if (!e || !t) return 0;
        engine::Vector v = t->getWorldTform().v - e->getWorldTform().v;
        e->setWorldRotation(blitz::rotationQuat(v.pitch(), v.yaw(), roll * kDegToRad));
        return 0;
    }

    static int c_EntityColor(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        engine::Model *m = e ? e->getModel() : nullptr;
        if (m) m->setColor(engine::Vector(arg_float(args[1]) / 255.0f, arg_float(args[2]) / 255.0f, arg_float(args[3]) / 255.0f));
        return 0;
    }

    static int c_EntityAlpha(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        engine::Model *m = e ? e->getModel() : nullptr;
        if (m) m->setAlpha(arg_float(args[1]));
        return 0;
    }

    static int c_EntityFX(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        engine::Model *m = e ? e->getModel() : nullptr;
        if (m) m->setFX((int)arg_int(args[1]));
        return 0;
    }

    static int c_EntityBlend(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        engine::Model *m = e ? e->getModel() : nullptr;
        if (m) m->setBlend((int)arg_int(args[1]));
        return 0;
    }

    static int c_EntityOrder(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        engine::Object *o = e ? e->getObject() : nullptr;
        if (o) o->setOrder((int)arg_int(args[1]));
        return 0;
    }

    static int c_AmbientLight(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        world_for(vm)->setAmbient(engine::Vector(arg_float(args[0]) / 255.0f, arg_float(args[1]) / 255.0f,
                                                 arg_float(args[2]) / 255.0f));
        return 0;
    }

    static int c_CameraRange(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        engine::Camera *cam = e ? e->getCamera() : nullptr;
        if (cam) cam->setRange(arg_float(args[1]), arg_float(args[2]));
        return 0;
    }

    static int c_CameraZoom(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        engine::Camera *cam = e ? e->getCamera() : nullptr;
        if (cam) cam->setZoom(arg_float(args[1]));
        return 0;
    }

    static int c_CameraViewport(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        engine::Camera *cam = e ? e->getCamera() : nullptr;
        if (cam) cam->setViewport((int)arg_int(args[1]), (int)arg_int(args[2]), (int)arg_int(args[3]), (int)arg_int(args[4]));
        return 0;
    }

    static int c_CameraClsColor(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        engine::Camera *cam = e ? e->getCamera() : nullptr;
        if (cam) cam->setClsColor(engine::Vector(arg_float(args[1]) / 255.0f, arg_float(args[2]) / 255.0f, arg_float(args[3]) / 255.0f));
        return 0;
    }

    static int c_CameraClsMode(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        engine::Camera *cam = e ? e->getCamera() : nullptr;
        if (cam) cam->setClsMode(arg_int(args[1]) != 0, arg_int(args[2]) != 0);
        return 0;
    }

    static int c_LightColor(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        engine::Light *light = e ? e->getLight() : nullptr;
        if (light) light->setColor(engine::Vector(arg_float(args[1]) / 255.0f, arg_float(args[2]) / 255.0f, arg_float(args[3]) / 255.0f));
        return 0;
    }

    static int c_LightRange(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        engine::Light *light = e ? e->getLight() : nullptr;
        if (light) light->setRange(arg_float(args[1]));
        return 0;
    }

    static int c_LightConeAngles(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        engine::Light *light = e ? e->getLight() : nullptr;
        if (light) light->setConeAngles(arg_float(args[1]), arg_float(args[2]));
        return 0;
    }

    static int c_LoadMD2(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        const char *file = zen::is_string(args[0]) ? zen::as_cstring(args[0]) : "";
        engine::Entity *parent = entity_of(arg_int(args[1]));
        engine::MD2Model *m = new engine::MD2Model(file);
        if (!m->getValid()) { delete m; args[0] = val_int(0); return 1; }
        insert_entity(m, parent);
        args[0] = val_int(store_entity(m));
        return 1;
    }

    static engine::MD2Model *md2_of(long long h)
    {
        engine::Entity *e = entity_of(h);
        engine::Model *m = e ? e->getModel() : nullptr;
        return m ? m->getMD2Model() : nullptr;
    }

    static int c_AnimateMD2(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::MD2Model *m = md2_of(arg_int(args[0]));
        if (m) m->startMD2Anim((int)arg_int(args[3]), (int)arg_int(args[4]), (int)arg_int(args[1]),
                               arg_float(args[2]), arg_float(args[5]));
        return 0;
    }

    static int c_MD2AnimTime(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::MD2Model *m = md2_of(arg_int(args[0]));
        args[0] = val_float(m ? m->getMD2AnimTime() : 0.0f);
        return 1;
    }

    static int c_MD2AnimLength(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::MD2Model *m = md2_of(arg_int(args[0]));
        args[0] = val_int(m ? m->getMD2AnimLength() : 0);
        return 1;
    }

    static int c_MD2Animating(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::MD2Model *m = md2_of(arg_int(args[0]));
        args[0] = val_int(m && m->getMD2Animating() ? 1 : 0);
        return 1;
    }

    static int c_UpdateWorld(VM *vm, Value *args, int nargs)
    {
        (void)args; (void)nargs;
        world_for(vm)->update(1.0f / 60.0f);
        return 0;
    }

    static int c_RenderWorld(VM *vm, Value *args, int nargs)
    {
        (void)args; (void)nargs;
        engine::Platform *p = platform_for(vm);
        engine::World *w = world_for(vm);

        p->flushCanvasPass();
        w->prepare(p->device(), 1.0f);
        if (p->beginWorldRender())
        {
            w->draw(p->device());
            p->endWorldRender();
        }
        return 0;
    }

    static int c_CaptureWorld(VM *vm, Value *args, int nargs)
    {
        (void)args; (void)nargs;
        world_for(vm)->capture();
        return 0;
    }

    extern const zen::CommandDecl bb3d_cmds_world[] = {
        {"%CreateCube%parent=0", c_CreateCube},
        {"%CreateSphere%segments=8%parent=0", c_CreateSphere},
        {"%CreateCylinder%segments=8%solid=1%parent=0", c_CreateCylinder},
        {"%CreateCone%segments=8%solid=1%parent=0", c_CreateCone},
        {"%CreateCamera%parent=0", c_CreateCamera},
        {"%CreateLight%type=1%parent=0", c_CreateLight},
        {"%CreatePivot%parent=0", c_CreatePivot},

        {"FreeEntity%entity", c_FreeEntity},
        {"HideEntity%entity", c_HideEntity},
        {"ShowEntity%entity", c_ShowEntity},
        {"EntityParent%entity%parent%global=0", c_EntityParent},

        {"PositionEntity%entity#x#y#z%global=0", c_PositionEntity},
        {"RotateEntity%entity#pitch#yaw#roll%global=0", c_RotateEntity},
        {"ScaleEntity%entity#x#y#z%global=0", c_ScaleEntity},
        {"MoveEntity%entity#x#y#z", c_MoveEntity},
        {"TurnEntity%entity#pitch#yaw#roll%global=0", c_TurnEntity},
        {"TranslateEntity%entity#x#y#z%global=0", c_TranslateEntity},
        {"PointEntity%entity%target#roll=0", c_PointEntity},

        {"EntityColor%entity%red%green%blue", c_EntityColor},
        {"EntityAlpha%entity#alpha", c_EntityAlpha},
        {"EntityFX%entity%fx", c_EntityFX},
        {"EntityBlend%entity%blend", c_EntityBlend},
        {"EntityOrder%entity%order", c_EntityOrder},

        {"AmbientLight%red%green%blue", c_AmbientLight},
        {"CameraRange%camera#near#far", c_CameraRange},
        {"CameraZoom%camera#zoom", c_CameraZoom},
        {"CameraViewport%camera%x%y%width%height", c_CameraViewport},
        {"CameraClsColor%camera%red%green%blue", c_CameraClsColor},
        {"CameraClsMode%camera%cls_color%cls_zbuffer", c_CameraClsMode},

        {"LightColor%light%red%green%blue", c_LightColor},
        {"LightRange%light#range", c_LightRange},
        {"LightConeAngles%light#inner#outer", c_LightConeAngles},

        {"%LoadMD2$file%parent=0", c_LoadMD2},
        {"AnimateMD2%md2%mode=1#speed=1%first_frame=0%last_frame=9999#transition=0", c_AnimateMD2},
        {"#MD2AnimTime%md2", c_MD2AnimTime},
        {"%MD2AnimLength%md2", c_MD2AnimLength},
        {"%MD2Animating%md2", c_MD2Animating},

        {"UpdateWorld#elapsed_time=1", c_UpdateWorld},
        {"RenderWorld#tween=1", c_RenderWorld},
        {"CaptureWorld", c_CaptureWorld},
    };
    extern const int bb3d_cmds_world_count = (int)(sizeof(bb3d_cmds_world) / sizeof(bb3d_cmds_world[0]));
}
