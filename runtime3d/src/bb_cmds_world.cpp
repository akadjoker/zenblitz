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
#include "engine/MeshModel.h"
#include "engine/Camera.h"
#include "engine/Light.h"
#include "engine/MD2Model.h"
#include "engine/Terrain.h"
#include "engine/VoxelSprite.h"
#include "engine/CSG.h"
#include "engine/Animator.h"
#include "engine/LoaderB3D.h"
#include "engine/LoaderB3DS.h"
#include "engine/LoaderX.h"
#include "engine/LoaderGltf.h"
#include "engine/LoaderFbx.h"
#include "engine/PlaneModel.h"
#include "engine/Image.h"
#include "engine/Sound.h"
#include "engine/FilePath.h"
#include <string>
#include <cstdio>

namespace bb3d
{
    extern engine::Platform *platform_for(zen::VM *vm);
    extern engine::World *world_for(zen::VM *vm);
    extern engine::Image *image_of(long long h);
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
    inline const char *arg_cstr(zen::Value v) { return zen::is_string(v) ? zen::as_cstring(v) : ""; }
}

using namespace zen;

namespace bb3d
{
    static ct::HashMap<long long, engine::Entity *> g_entities;
    static ct::HashMap<VM *, engine::ObjCollision> g_picks;
    // reverse index: World::collide only ever hands back an Object*, not
    // the handle a .bb script already holds for it - a collision command
    // needs the existing handle back (EntityCollided, CollisionEntity),
    // not a fresh one minted every time the same entity collides again.
    static ct::HashMap<engine::Entity *, long long> g_handles;
    static long long g_next_entity = 0;

    engine::Entity *entity_of(long long h)
    {
        engine::Entity **found = g_entities.find(h);
        return found ? *found : nullptr;
    }

    long long store_entity(engine::Entity *e)
    {
        long long h = ++g_next_entity;
        g_entities.put(h, e);
        g_handles.put(e, h);
        return h;
    }

    // extern: looks up a handle a script already holds for an entity
    // World::collide gave us as a raw pointer, instead of minting a new
    // one - 0 if the entity was never stored under a handle (shouldn't
    // happen for anything World::collide can reach, but never assume).
    long long handle_of(engine::Entity *e)
    {
        long long *found = e ? g_handles.find(e) : nullptr;
        return found ? *found : 0;
    }

    // extern: called by shutdown_graphics() when the program ends.
    // ~Entity already deletes its children, so deleting the orphan roots
    // walks the whole scene - deleting every handle instead would
    // double-free anything that is parented to something else.
    void free_all_entities(gpu::Device *dev)
    {
        // GPU buffers first: a destructor can't release them (it has no
        // device), so the whole tree is swept before anything is deleted.
        if (dev)
            for (engine::Entity *root = engine::Entity::orphans(); root; root = root->successor())
                root->freeGpuTree(*dev);
        while (engine::Entity *root = engine::Entity::orphans()) delete root;
        g_entities.clear();
        g_handles.clear();
        g_next_entity = 0;
    }

    /* bbblitz3d.cpp's insert(), which every command that creates or
       copies an entity runs it through. It is what makes CopyEntity of a
       hidden entity come back visible: the flags are forced on for the
       whole subtree rather than inherited from the source, so a script
       can keep a hidden "template" model around and stamp visible copies
       out of it - exactly what castle.bb does with player_model. */
    static void reset_entity_tree(engine::Entity *e)
    {
        e->setVisible(true);
        e->setEnabled(true);
        if (engine::Object *o = e->getObject()) o->reset();
        for (engine::Entity *c = e->children(); c; c = c->successor())
            reset_entity_tree(c);
    }

    void insert_entity(engine::Entity *e, engine::Entity *parent)
    {
        if (parent) e->setParent(parent);
        reset_entity_tree(e);
    }

    static engine::ObjCollision &pick_for(VM *vm)
    {
        engine::ObjCollision *pick = g_picks.find(vm);
        if (!pick)
        {
            engine::ObjCollision empty{};
            empty.collision.time = 1.0f;
            g_picks.put(vm, empty);
            pick = g_picks.find(vm);
        }
        return *pick;
    }

    static engine::Object *doPick(VM *vm, const engine::Line &line, float radius)
    {
        engine::ObjCollision &pick = pick_for(vm);
        pick.with = nullptr;
        pick.coords.clear();
        pick.collision.time = 1.0f;
        pick.collision.normal.clear();
        pick.collision.surface = nullptr;
        pick.collision.index = (unsigned short)~0;
        return world_for(vm)->traceRay(line, radius, &pick);
    }

    static int c_CopyEntity(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *src = entity_of(arg_int(args[0]));
        engine::Entity *parent = entity_of(arg_int(args[1]));
        if (!src || !src->getObject()) { args[0] = val_int(0); return 1; }
        // Object::copy() clones the whole subtree (children + animator);
        // reparent the copy the same way every other Create* does rather
        // than duplicate that logic here.
        engine::Object *cpy = src->getObject()->copy();
        insert_entity(cpy, parent);
        args[0] = val_int(store_entity(cpy));
        return 1;
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

    static int c_CreateMesh(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::MeshModel *mesh = new engine::MeshModel();
        insert_entity(mesh, entity_of(arg_int(args[0])));
        args[0] = val_int(store_entity(mesh));
        return 1;
    }

    static int c_CreateVoxelSprite(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::VoxelSprite *voxel = new engine::VoxelSprite((int)arg_int(args[0]));
        insert_entity(voxel, entity_of(arg_int(args[1])));
        args[0] = val_int(store_entity(voxel));
        return 1;
    }

    static engine::MeshModel *makeGrid(int columns, int rows, const engine::Pixmap *heightmap, float heightScale,
                                       float width, float depth)
    {
        engine::MeshModel *mesh = new engine::MeshModel();
        engine::Surface *surface = mesh->createSurface(engine::Brush());
        engine::Surface::Vertex vertex;
        for (int z = 0; z <= rows; ++z)
            for (int x = 0; x <= columns; ++x)
            {
                float y = 0.0f;
                if (heightmap)
                {
                    const int sx = columns ? x * (heightmap->width - 1) / columns : 0;
                    const int sz = rows ? z * (heightmap->height - 1) / rows : 0;
                    const engine::Color color = heightmap->get_pixel_color((engine::u32)sx, (engine::u32)sz);
                    y = ((float)color.r() + (float)color.g() + (float)color.b()) * (heightScale / (255.0f * 3.0f));
                }
                vertex.coords = engine::Vector((float)x * width / columns - width * 0.5f, y,
                                               (float)z * depth / rows - depth * 0.5f);
                vertex.normal = engine::Vector(0, 1, 0);
                vertex.texCoords[0][0] = (float)x / columns;
                vertex.texCoords[0][1] = (float)z / rows;
                surface->addVertex(vertex);
            }
        for (int z = 0; z < rows; ++z)
            for (int x = 0; x < columns; ++x)
            {
                const unsigned short a = (unsigned short)(z * (columns + 1) + x);
                const unsigned short b = (unsigned short)(a + 1);
                const unsigned short c = (unsigned short)(a + columns + 1);
                const unsigned short d = (unsigned short)(c + 1);
                surface->addTriangle(engine::Surface::Triangle{{a, c, b}});
                surface->addTriangle(engine::Surface::Triangle{{b, c, d}});
            }
        surface->updateNormals();
        return mesh;
    }

    static int c_CreatePlane(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        // A Blitz3D plane is "a flat, infinite 'ground'" (CreatePlane's
        // own docs), not a quad - PlaneModel rebuilds it from the camera
        // frustum every frame so it always reaches the horizon.
        engine::PlaneModel *plane = new engine::PlaneModel((int)arg_int(args[0]));
        insert_entity(plane, entity_of(arg_int(args[1])));
        args[0] = val_int(store_entity(plane));
        return 1;
    }

    static int c_CreateHeightMap(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Image *image = image_of(arg_int(args[0]));
        if (!image || image->width() < 2 || image->height() < 2) { args[0] = val_int(0); return 1; }
        int columns = image->width() - 1, rows = image->height() - 1;
        if (columns > 254) columns = 254;
        if (rows > 254) rows = 254;
        engine::MeshModel *mesh = makeGrid(columns, rows, &image->frame(0).pixels, arg_float(args[1]),
                                           (float)(image->width() - 1), (float)(image->height() - 1));
        insert_entity(mesh, entity_of(arg_int(args[2])));
        args[0] = val_int(store_entity(mesh));
        return 1;
    }

    static engine::Terrain *terrain_of(long long handle)
    {
        engine::Entity *entity = entity_of(handle);
        engine::Model *model = entity ? entity->getModel() : nullptr;
        return model ? model->getTerrain() : nullptr;
    }

    static int c_CreateTerrain(VM *vm, Value *args, int)
    {
        int size = (int)arg_int(args[0]), shift = 0;
        while ((1 << shift) < size) ++shift;
        if (size < 2 || shift > 12 || (1 << shift) != size) { args[0] = val_int(0); return 1; }
        engine::Terrain *terrain = new engine::Terrain(shift);
        insert_entity(terrain, entity_of(arg_int(args[1])));
        args[0] = val_int(store_entity(terrain));
        return 1;
    }

    static int c_LoadTerrain(VM *vm, Value *args, int)
    {
        // Blitz3D shipped on Windows, so scripts assume case-insensitive
        // paths (the .bb sample set has "heightmap_256.bmp" against a
        // "heightmap_256.BMP" file) - same fix Texture.cpp already has.
        engine::Pixmap pixels;
        if (!pixels.load(engine::resolveCaseInsensitive(arg_cstr(args[0])).c_str()) ||
            pixels.width != pixels.height || pixels.width < 2) { args[0] = val_int(0); return 1; }
        int shift = 0;
        while ((1 << shift) < pixels.width) ++shift;
        if (shift > 12 || (1 << shift) != pixels.width) { args[0] = val_int(0); return 1; }
        engine::Terrain *terrain = new engine::Terrain(shift);
        for (int y = 0; y < pixels.height; ++y)
            for (int x = 0; x < pixels.width; ++x)
            {
                engine::Color color = pixels.get_pixel_color((engine::u32)x, (engine::u32)y);
                int top = color.r() > color.g() ? color.r() : color.g();
                if (color.b() > top) top = color.b();
                terrain->setHeight(x, pixels.height - 1 - y, top / 255.0f, false);
            }
        insert_entity(terrain, entity_of(arg_int(args[1])));
        args[0] = val_int(store_entity(terrain));
        return 1;
    }

    static int c_TerrainDetail(VM *, Value *args, int)
    {
        if (engine::Terrain *terrain = terrain_of(arg_int(args[0]))) terrain->setDetail((int)arg_int(args[1]), arg_int(args[2]) != 0);
        return 0;
    }

    static int c_TerrainShading(VM *, Value *args, int)
    {
        if (engine::Terrain *terrain = terrain_of(arg_int(args[0]))) terrain->setShading(arg_int(args[1]) != 0);
        return 0;
    }

    static engine::Vector terrain_vector(engine::Terrain *terrain, float x, float y, float z)
    {
        engine::Vector local = -terrain->getWorldTform() * engine::Vector(x, y, z);
        int ix = (int)std::floor(local.x), iz = (int)std::floor(local.z);
        float tx = local.x - ix, tz = local.z - iz;
        float h0 = terrain->getHeight(ix, iz), h1 = terrain->getHeight(ix + 1, iz);
        float h2 = terrain->getHeight(ix, iz + 1), h3 = terrain->getHeight(ix + 1, iz + 1);
        float ha = (h1 - h0) * tx + h0, hb = (h3 - h2) * tx + h2;
        return terrain->getWorldTform() * engine::Vector(local.x, (hb - ha) * tz + ha, local.z);
    }

    static int c_TerrainX(VM *, Value *args, int) { engine::Terrain *t=terrain_of(arg_int(args[0])); args[0]=val_float(t ? terrain_vector(t,arg_float(args[1]),arg_float(args[2]),arg_float(args[3])).x : 0); return 1; }
    static int c_TerrainY(VM *, Value *args, int) { engine::Terrain *t=terrain_of(arg_int(args[0])); args[0]=val_float(t ? terrain_vector(t,arg_float(args[1]),arg_float(args[2]),arg_float(args[3])).y : 0); return 1; }
    static int c_TerrainZ(VM *, Value *args, int) { engine::Terrain *t=terrain_of(arg_int(args[0])); args[0]=val_float(t ? terrain_vector(t,arg_float(args[1]),arg_float(args[2]),arg_float(args[3])).z : 0); return 1; }
    static int c_TerrainSize(VM *, Value *args, int) { engine::Terrain *t=terrain_of(arg_int(args[0])); args[0]=val_int(t ? t->getSize() : 0); return 1; }
    static int c_TerrainHeight(VM *, Value *args, int) { engine::Terrain *t=terrain_of(arg_int(args[0])); args[0]=val_float(t ? t->getHeight((int)arg_int(args[1]),(int)arg_int(args[2])) : 0); return 1; }
    static int c_ModifyTerrain(VM *, Value *args, int) { if(engine::Terrain *t=terrain_of(arg_int(args[0])))t->setHeight((int)arg_int(args[1]),(int)arg_int(args[2]),arg_float(args[3]),arg_int(args[4])!=0);return 0; }

    static int c_CopyMesh(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *entity = entity_of(arg_int(args[0]));
        engine::MeshModel *source = entity && entity->getModel() ? entity->getModel()->getMeshModel() : nullptr;
        if (!source) { args[0] = val_int(0); return 1; }
        engine::MeshModel *mesh = new engine::MeshModel(*source);
        insert_entity(mesh, entity_of(arg_int(args[1])));
        args[0] = val_int(store_entity(mesh));
        return 1;
    }

    static int c_AddMesh(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *srcEntity = entity_of(arg_int(args[0]));
        engine::Entity *dstEntity = entity_of(arg_int(args[1]));
        engine::MeshModel *src = srcEntity && srcEntity->getModel() ? srcEntity->getModel()->getMeshModel() : nullptr;
        engine::MeshModel *dst = dstEntity && dstEntity->getModel() ? dstEntity->getModel()->getMeshModel() : nullptr;
        if (src && dst) dst->add(*src);
        return 0;
    }

    static int c_MeshCSG(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *aEntity = entity_of(arg_int(args[0]));
        engine::Entity *bEntity = entity_of(arg_int(args[1]));
        engine::MeshModel *a = aEntity && aEntity->getModel() ? aEntity->getModel()->getMeshModel() : nullptr;
        engine::MeshModel *b = bEntity && bEntity->getModel() ? bEntity->getModel()->getMeshModel() : nullptr;
        engine::MeshModel *result = a && b ? engine::CSG::meshCSG(*a, *b, (int)arg_int(args[2])) : nullptr;
        args[0] = val_int(result ? store_entity(result) : 0);
        return 1;
    }

    static int c_UpdateNormals(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *entity = entity_of(arg_int(args[0]));
        if (engine::Model *model = entity ? entity->getModel() : nullptr)
            if (engine::MeshModel *mesh = model->getMeshModel()) mesh->updateNormals();
        return 0;
    }

    static int c_MeshCullBox(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *entity = entity_of(arg_int(args[0]));
        if (engine::Model *model = entity ? entity->getModel() : nullptr)
            if (engine::MeshModel *mesh = model->getMeshModel())
            {
                engine::Vector a(arg_float(args[1]), arg_float(args[2]), arg_float(args[3]));
                engine::Vector b = a + engine::Vector(arg_float(args[4]), arg_float(args[5]), arg_float(args[6]));
                mesh->setCullBox(engine::Box(a, b));
            }
        return 0;
    }

    static int c_MeshesIntersect(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *aEntity = entity_of(arg_int(args[0]));
        engine::Entity *bEntity = entity_of(arg_int(args[1]));
        engine::MeshModel *a = aEntity && aEntity->getModel() ? aEntity->getModel()->getMeshModel() : nullptr;
        engine::MeshModel *b = bEntity && bEntity->getModel() ? bEntity->getModel()->getMeshModel() : nullptr;
        args[0] = val_int(a && b && a->intersects(*b) ? 1 : 0);
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

    // CreateMirror: an invisible, infinite flat plane at the local origin
    // that vertically flips whatever renders above/below it - World
    // already knows how to render it (World::render(cam, Mirror*, dev),
    // populated from o->getMirror() in World::add), so this is nothing
    // more than an Object entry point for that, exactly as bare as the
    // original's Mirror class was.
    static int c_CreateMirror(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *parent = entity_of(arg_int(args[0]));
        engine::Mirror *mirror = new engine::Mirror();
        insert_entity(mirror, parent);
        args[0] = val_int(store_entity(mirror));
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
        (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        if (e)
        {
            if (engine::Object *o = e->getObject()) engine::AudioSystem::get().detachFollower(o);
            e->freeGpuTree(platform_for(vm)->device());
            g_entities.erase(arg_int(args[0]));
            g_handles.erase(e);
            delete e;
        }
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

    static int c_EntityShininess(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        if (engine::Model *m = e ? e->getModel() : nullptr) m->setShininess(arg_float(args[1]));
        return 0;
    }

    static int c_EntityAutoFade(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        if (engine::Model *m = e ? e->getModel() : nullptr) m->setAutoFade(arg_float(args[1]), arg_float(args[2]));
        return 0;
    }

    static int c_NameEntity(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        if (engine::Entity *e = entity_of(arg_int(args[0]))) e->setName(zen::is_string(args[1]) ? zen::as_cstring(args[1]) : "");
        return 0;
    }

    static int c_EntityName(VM *vm, Value *args, int)
    {
        engine::Entity *e = entity_of(arg_int(args[0]));
        args[0] = val_obj((Obj *)vm->make_string(e ? e->getName().c_str() : ""));
        return 1;
    }

    static int c_EntityClass(VM *vm, Value *args, int)
    {
        engine::Entity *e = entity_of(arg_int(args[0]));
        const char *name = !e ? "" : e->getCamera() ? "Camera" : e->getLight() ? "Light" :
                           e->getModel() ? "Mesh" : e->getSprite() ? "Sprite" : e->getObject() ? "Pivot" : "Entity";
        args[0] = val_obj((Obj *)vm->make_string(name));
        return 1;
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

    static int c_CameraProjMode(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        if (engine::Camera *cam = e ? e->getCamera() : nullptr) cam->setProjMode((int)arg_int(args[1]));
        return 0;
    }

    static int c_CameraFogColor(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        if (engine::Camera *cam = e ? e->getCamera() : nullptr)
            cam->setFogColor(engine::Vector(arg_float(args[1]) / 255.0f, arg_float(args[2]) / 255.0f, arg_float(args[3]) / 255.0f));
        return 0;
    }

    static int c_CameraFogRange(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        if (engine::Camera *cam = e ? e->getCamera() : nullptr) cam->setFogRange(arg_float(args[1]), arg_float(args[2]));
        return 0;
    }

    static int c_CameraFogMode(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        if (engine::Camera *cam = e ? e->getCamera() : nullptr) cam->setFogMode((int)arg_int(args[1]));
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

    namespace
    {
        std::string lowerExt(const std::string &f)
        {
            size_t dot = f.find_last_of('.');
            if (dot == std::string::npos) return std::string();
            std::string ext = f.substr(dot + 1);
            for (char &c : ext) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            return ext;
        }
    }

    /* bbLoadMesh's collapseMesh: LoadMesh hands back a single flat mesh a
       script can ScaleMesh/FlipMesh/FitMesh and test with
       MeshesIntersect, not the Frame/node hierarchy the file happens to
       be built from - LoadAnimMesh is the command that keeps that.
       Each child's surfaces are baked into world space and folded into
       the destination, then the child is discarded. LoaderX already does
       this inside itself; .3ds and .b3d did not, so a multi-part model
       loaded as a root with no surfaces of its own (pong3d.bb's paddles
       reported 0 vertices, and MeshesIntersect never fired). */
    static void collapse_mesh(engine::MeshModel *dest, engine::Entity *e)
    {
        while (e->children()) collapse_mesh(dest, e->children());
        if (engine::Model *m = e->getModel())
            if (engine::MeshModel *t = m->getMeshModel())
            {
                t->transform(e->getWorldTform());
                dest->add(*t);
            }
        delete e;
    }

    static int c_LoadMesh(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        const char *file = zen::is_string(args[0]) ? zen::as_cstring(args[0]) : "";
        engine::Entity *parent = entity_of(arg_int(args[1]));

        std::string ext = lowerExt(file);
        engine::Entity *e = nullptr;
        if (ext == "md2")
        {
            engine::MD2Model *m = new engine::MD2Model(file);
            if (!m->getValid()) delete m;
            else e = m;
        }
        else if (ext == "3ds")
        {
            // 3D Studio's .3ds is Z-up; Blitz3D's world is Y-up. The
            // original set this up-front as the default LoaderMatrix for
            // "3ds" (bbblitz3d.cpp, blitz3d_reset): swap Y and Z so the
            // loaded mesh lands the right way up without the .bb script
            // having to call LoaderMatrix itself.
            static const engine::Transform kConv3ds(
                blitz::Matrix(blitz::Vector(1, 0, 0), blitz::Vector(0, 0, 1), blitz::Vector(0, 1, 0)));
            // HintCollapse like every other format: bbLoadMesh always
            // asked for it and then folded the result into one flat mesh
            // (see the .x branch below). Without it a multi-part .3ds
            // came back as a hierarchy whose root had no surfaces at all,
            // so CountSurfaces/CountVertices reported 0 and
            // MeshesIntersect had nothing to test - pong3d.bb never
            // registered a single bounce.
            engine::LoaderB3DS loader;
            e = loader.load(file, kConv3ds, engine::MeshLoader::HintCollapse,
                            &platform_for(vm)->device());
        }
        else if (ext == "x")
        {
            // bbLoadMesh always passed MeshLoader::HINT_COLLAPSE and then
            // folded the result into one flat MeshModel: LoadMesh's handle
            // is a single mesh a script transforms directly (ScaleMesh,
            // FlipMesh, FitMesh), not a Frame hierarchy - LoadAnimMesh is
            // the command that keeps that hierarchy for animation.
            engine::LoaderX loader;
            e = loader.load(file, engine::Transform(), engine::MeshLoader::HintCollapse,
                            &platform_for(vm)->device());
        }
        else if (ext == "gltf" || ext == "glb")
        {
            // Not an original Blitz3D format - see [[project-gltf-fbx-extension]].
            // Same HintCollapse convention as .x above.
            engine::LoaderGltf loader;
            e = loader.load(file, engine::Transform(), engine::MeshLoader::HintCollapse,
                            &platform_for(vm)->device());
        }
        else if (ext == "fbx")
        {
            // Not an original Blitz3D format - see [[project-gltf-fbx-extension]].
            // Same HintCollapse convention as .x/.gltf above.
            engine::LoaderFbx loader;
            e = loader.load(file, engine::Transform(), engine::MeshLoader::HintCollapse,
                            &platform_for(vm)->device());
        }
        else
        {
            // .b3d and anything else fall back to the B3D loader, same as
            // LoadMesh in the original (only .b3d/.3ds/.x were ever real
            // mesh formats it dispatched by extension).
            engine::LoaderB3D loader;
            e = loader.load(file, engine::Transform(), engine::MeshLoader::HintCollapse,
                            &platform_for(vm)->device());
        }

        if (!e)
        {
            char msg[512];
            snprintf(msg, sizeof(msg), "LoadMesh: could not load \"%s\"", file);
            zen::backend_log(vm->backend(), zen::LOG_WARN, msg);
            args[0] = val_int(0);
            return 1;
        }
        // MD2 is its own animated model type, not a mesh hierarchy, so
        // it is handed back as-is; everything else is folded flat.
        if (!e->getModel() || !e->getModel()->getMD2Model())
        {
            engine::MeshModel *flat = new engine::MeshModel();
            while (e->children()) collapse_mesh(flat, e->children());
            if (engine::Model *m = e->getModel())
                if (engine::MeshModel *t = m->getMeshModel()) flat->add(*t);
            delete e;
            e = flat;
        }
        insert_entity(e, parent);
        args[0] = val_int(store_entity(e));
        return 1;
    }

    static int c_LoadMD2(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        const char *file = zen::is_string(args[0]) ? zen::as_cstring(args[0]) : "";
        engine::Entity *parent = entity_of(arg_int(args[1]));
        engine::MD2Model *m = new engine::MD2Model(file);
        if (!m->getValid())
        {
            delete m;
            char msg[512];
            snprintf(msg, sizeof(msg), "LoadMD2: could not load \"%s\"", file);
            zen::backend_log(vm->backend(), zen::LOG_WARN, msg);
            args[0] = val_int(0);
            return 1;
        }
        insert_entity(m, parent);
        args[0] = val_int(store_entity(m));
        return 1;
    }

    static int c_LoadAnimMesh(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        const char *file = zen::is_string(args[0]) ? zen::as_cstring(args[0]) : "";
        engine::Entity *entity = nullptr;
        const std::string ext = lowerExt(file);
        if (ext == "3ds")
        {
            static const engine::Transform conv(blitz::Matrix(blitz::Vector(1, 0, 0), blitz::Vector(0, 0, 1), blitz::Vector(0, 1, 0)));
            engine::LoaderB3DS loader;
            entity = loader.load(file, conv, 0, &platform_for(vm)->device());
        }
        else if (ext == "x")
        {
            // Unlike LoadMesh, no HintCollapse here: LoadAnimMesh keeps the
            // Frame hierarchy (and animation keys) LoaderX parses so
            // Animate/SetAnimTime can walk it, same split as the .b3d path.
            engine::LoaderX loader;
            entity = loader.load(file, engine::Transform(), 0, &platform_for(vm)->device());
        }
        else if (ext == "gltf" || ext == "glb")
        {
            // Same hierarchy-keeping split as .x above.
            engine::LoaderGltf loader;
            entity = loader.load(file, engine::Transform(), 0, &platform_for(vm)->device());
        }
        else if (ext == "fbx")
        {
            // Same hierarchy-keeping split as .x/.gltf above - keeps the
            // bone chain and skin so Animate/SetAnimTime can drive it.
            engine::LoaderFbx loader;
            entity = loader.load(file, engine::Transform(), 0, &platform_for(vm)->device());
        }
        else
        {
            engine::LoaderB3D loader;
            entity = loader.load(file, engine::Transform(), 0, &platform_for(vm)->device());
        }
        if (!entity) { args[0] = val_int(0); return 1; }
        if (engine::Object *object = entity->getObject())
            if (engine::Animator *animator = object->getAnimator()) animator->animate(1, 0, 0, 0);
        insert_entity(entity, entity_of(arg_int(args[1])));
        args[0] = val_int(store_entity(entity));
        return 1;
    }

    static int c_LoadAnimSeq(VM *vm, Value *args, int)
    {
        engine::Entity *entity = entity_of(arg_int(args[0]));
        engine::Object *object = entity ? entity->getObject() : nullptr;
        engine::Animator *animator = object ? object->getAnimator() : nullptr;
        if (!animator) { args[0] = val_int(-1); return 1; }
        const char *file = zen::is_string(args[1]) ? zen::as_cstring(args[1]) : "";
        engine::LoaderB3D loader;
        engine::MeshModel *loaded = loader.load(file, engine::Transform(), engine::MeshLoader::HintAnimOnly, &platform_for(vm)->device());
        if (loaded)
        {
            if (engine::Animator *source = loaded->getAnimator()) animator->addSeqs(source);
            delete loaded;
        }
        args[0] = val_int(animator->numSeqs() - 1);
        return 1;
    }

    static int c_SetAnimTime(VM *vm, Value *args, int)
    {
        engine::Entity *entity = entity_of(arg_int(args[0]));
        if (engine::Object *object = entity ? entity->getObject() : nullptr)
            if (engine::Animator *animator = object->getAnimator()) animator->setAnimTime(arg_float(args[1]), (int)arg_int(args[2]));
        return 0;
    }

    static int c_Animate(VM *vm, Value *args, int)
    {
        engine::Entity *entity = entity_of(arg_int(args[0]));
        if (engine::Object *object = entity ? entity->getObject() : nullptr)
            if (engine::Animator *animator = object->getAnimator()) animator->animate((int)arg_int(args[1]), arg_float(args[2]), (int)arg_int(args[3]), arg_float(args[4]));
        return 0;
    }

    static int c_SetAnimKey(VM *vm, Value *args, int)
    {
        engine::Entity *entity = entity_of(arg_int(args[0]));
        engine::Object *object = entity ? entity->getObject() : nullptr;
        if (!object) return 0;
        engine::Animation animation = object->getAnimation();
        const int frame = (int)arg_int(args[1]);
        if (arg_int(args[2])) animation.setPositionKey(frame, object->getLocalPosition());
        if (arg_int(args[3])) animation.setRotationKey(frame, object->getLocalRotation());
        if (arg_int(args[4])) animation.setScaleKey(frame, object->getLocalScale());
        object->setAnimation(animation);
        return 0;
    }

    static int c_AddAnimSeq(VM *vm, Value *args, int)
    {
        engine::Entity *entity = entity_of(arg_int(args[0]));
        engine::Object *object = entity ? entity->getObject() : nullptr;
        if (!object) { args[0] = val_int(-1); return 1; }
        engine::Animator *animator = object->getAnimator();
        if (animator) animator->addSeq((int)arg_int(args[1]));
        else { animator = new engine::Animator(object, (int)arg_int(args[1])); object->setAnimator(animator); }
        args[0] = val_int(animator->numSeqs() - 1);
        return 1;
    }

    static int c_ExtractAnimSeq(VM *vm, Value *args, int)
    {
        engine::Entity *entity = entity_of(arg_int(args[0]));
        engine::Animator *animator = entity && entity->getObject() ? entity->getObject()->getAnimator() : nullptr;
        if (!animator) { args[0] = val_int(-1); return 1; }
        animator->extractSeq((int)arg_int(args[1]), (int)arg_int(args[2]), (int)arg_int(args[3]));
        args[0] = val_int(animator->numSeqs() - 1);
        return 1;
    }

    static engine::Animator *animator_of(long long handle)
    {
        engine::Entity *entity = entity_of(handle);
        return entity && entity->getObject() ? entity->getObject()->getAnimator() : nullptr;
    }
    static int c_AnimSeq(VM *, Value *args, int) { engine::Animator *a = animator_of(arg_int(args[0])); args[0] = val_int(a ? a->animSeq() : -1); return 1; }
    static int c_AnimTime(VM *, Value *args, int) { engine::Animator *a = animator_of(arg_int(args[0])); args[0] = val_float(a ? a->animTime() : -1); return 1; }
    static int c_AnimLength(VM *, Value *args, int) { engine::Animator *a = animator_of(arg_int(args[0])); args[0] = val_int(a ? a->animLen() : -1); return 1; }
    static int c_Animating(VM *, Value *args, int) { engine::Animator *a = animator_of(arg_int(args[0])); args[0] = val_int(a && a->animating() ? 1 : 0); return 1; }

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
        (void)nargs;
        // bbUpdateWorld passes its argument straight to World::update -
        // Blitz3D animation is counted in frames, not seconds, and the
        // parameter defaults to 1 meaning "advance one animation frame
        // per call". Scaling it by real elapsed seconds (~0.003) instead
        // makes a 20-frame animation take a minute and a half and look
        // frozen, which is what castle.bb's running player hit.
        world_for(vm)->update(arg_float(args[0]));
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

    static int c_CameraPick(VM *vm, Value *args, int)
    {
        engine::Entity *entity = entity_of(arg_int(args[0]));
        engine::Camera *camera = entity ? entity->getCamera() : nullptr;
        if (!camera) { args[0] = val_int(0); return 1; }
        int vpX, vpY, vpW, vpH;
        camera->getViewport(&vpX, &vpY, &vpW, &vpH);
        if (vpW <= 0 || vpH <= 0) { args[0] = val_int(0); return 1; }
        float x = arg_float(args[1]), y = arg_float(args[2]);
        const float nearRange = camera->getFrustumNear(), farRange = camera->getFrustumFar();
        x = ((x / vpW) - 0.5f) * camera->getFrustumWidth();
        y = (0.5f - (y / vpH)) * camera->getFrustumHeight();
        engine::Line line;
        if (camera->getProjMode() == engine::Camera::ProjOrtho)
            line = camera->getWorldTform() * engine::Line(engine::Vector(x, y, 0), engine::Vector(0, 0, farRange));
        else
            line = camera->getWorldTform() * engine::Line(engine::Vector(), engine::Vector(x * farRange / nearRange, y * farRange / nearRange, farRange));
        args[0] = val_int(handle_of(doPick(vm, line, 0.0f)));
        return 1;
    }

    static int c_LinePick(VM *vm, Value *args, int)
    {
        engine::Line line(engine::Vector(arg_float(args[0]), arg_float(args[1]), arg_float(args[2])),
                          engine::Vector(arg_float(args[3]), arg_float(args[4]), arg_float(args[5])));
        args[0] = val_int(handle_of(doPick(vm, line, arg_float(args[6]))));
        return 1;
    }

    static int c_EntityPick(VM *vm, Value *args, int)
    {
        engine::Entity *entity = entity_of(arg_int(args[0]));
        engine::Object *object = entity ? entity->getObject() : nullptr;
        if (!object) { args[0] = val_int(0); return 1; }
        engine::Line line(object->getWorldPosition(), object->getWorldTform().m.k * arg_float(args[1]));
        args[0] = val_int(handle_of(doPick(vm, line, 0.0f)));
        return 1;
    }

    static int c_EntityPickMode(VM *vm, Value *args, int)
    {
        engine::Entity *entity = entity_of(arg_int(args[0]));
        if (engine::Object *object = entity ? entity->getObject() : nullptr)
        {
            object->setPickGeometry((int)arg_int(args[1]));
            object->setObscurer(arg_int(args[2]) != 0);
        }
        return 0;
    }

    static int c_PickedX(VM *vm, Value *args, int) { args[0] = val_float(pick_for(vm).coords.x); return 1; }
    static int c_PickedY(VM *vm, Value *args, int) { args[0] = val_float(pick_for(vm).coords.y); return 1; }
    static int c_PickedZ(VM *vm, Value *args, int) { args[0] = val_float(pick_for(vm).coords.z); return 1; }
    static int c_PickedNX(VM *vm, Value *args, int) { args[0] = val_float(pick_for(vm).collision.normal.x); return 1; }
    static int c_PickedNY(VM *vm, Value *args, int) { args[0] = val_float(pick_for(vm).collision.normal.y); return 1; }
    static int c_PickedNZ(VM *vm, Value *args, int) { args[0] = val_float(pick_for(vm).collision.normal.z); return 1; }
    static int c_PickedTime(VM *vm, Value *args, int) { args[0] = val_float(pick_for(vm).collision.time); return 1; }
    static int c_PickedEntity(VM *vm, Value *args, int) { args[0] = val_int(handle_of(pick_for(vm).with)); return 1; }
    static int c_PickedSurface(VM *vm, Value *args, int) { args[0] = val_int((long long)(std::intptr_t)pick_for(vm).collision.surface); return 1; }
    static int c_PickedTriangle(VM *vm, Value *args, int) { args[0] = val_int(pick_for(vm).collision.index); return 1; }

    extern const zen::CommandDecl bb3d_cmds_world[] = {
        {"%CopyEntity%entity%parent=0", c_CopyEntity},
        {"%CreateMesh%parent=0", c_CreateMesh},
        {"%CreateVoxelSprite%slices=64%parent=0", c_CreateVoxelSprite},
        {"%CreatePlane%segments=1%parent=0", c_CreatePlane},
        {"%CreateHeightMap%image#height_scale=1%parent=0", c_CreateHeightMap},
        {"%CreateHighMap%image#height_scale=1%parent=0", c_CreateHeightMap},
        {"%CreateTerrain%grid_size%parent=0", c_CreateTerrain},
        {"%LoadTerrain$heightmap_file%parent=0", c_LoadTerrain},
        {"TerrainDetail%terrain%detail_level%morph=0", c_TerrainDetail},
        {"TerrainShading%terrain%enable", c_TerrainShading},
        {"#TerrainX%terrain#world_x#world_y#world_z", c_TerrainX},
        {"#TerrainY%terrain#world_x#world_y#world_z", c_TerrainY},
        {"#TerrainZ%terrain#world_x#world_y#world_z", c_TerrainZ},
        {"%TerrainSize%terrain", c_TerrainSize},
        {"#TerrainHeight%terrain%terrain_x%terrain_z", c_TerrainHeight},
        {"ModifyTerrain%terrain%terrain_x%terrain_z#height%realtime=0", c_ModifyTerrain},
        {"%CopyMesh%mesh%parent=0", c_CopyMesh},
        {"AddMesh%source_mesh%dest_mesh", c_AddMesh},
        {"%MeshCSG%mesh_a%mesh_b%method=1", c_MeshCSG},
        {"UpdateNormals%mesh", c_UpdateNormals},
        {"MeshCullBox%mesh#x#y#z#width#height#depth", c_MeshCullBox},
        {"%MeshesIntersect%mesh_a%mesh_b", c_MeshesIntersect},
        {"%CreateCube%parent=0", c_CreateCube},
        {"%CreateSphere%segments=8%parent=0", c_CreateSphere},
        {"%CreateCylinder%segments=8%solid=1%parent=0", c_CreateCylinder},
        {"%CreateCone%segments=8%solid=1%parent=0", c_CreateCone},
        {"%CreateCamera%parent=0", c_CreateCamera},
        {"%CreateMirror%parent=0", c_CreateMirror},
        {"%CreateLight%type=1%parent=0", c_CreateLight},
        {"%CreatePivot%parent=0", c_CreatePivot},

        {"FreeEntity%entity", c_FreeEntity},
        {"HideEntity%entity", c_HideEntity},
        {"ShowEntity%entity", c_ShowEntity},
        {"EntityParent%entity%parent%global=1", c_EntityParent},

        {"PositionEntity%entity#x#y#z%global=0", c_PositionEntity},
        {"RotateEntity%entity#pitch#yaw#roll%global=0", c_RotateEntity},
        {"ScaleEntity%entity#x_scale#y_scale#z_scale%global=0", c_ScaleEntity},
        {"MoveEntity%entity#x#y#z", c_MoveEntity},
        {"TurnEntity%entity#pitch#yaw#roll%global=0", c_TurnEntity},
        {"TranslateEntity%entity#x#y#z%global=0", c_TranslateEntity},
        {"PointEntity%entity%target#roll=0", c_PointEntity},

        {"EntityColor%entity#red#green#blue", c_EntityColor},
        {"EntityAlpha%entity#alpha", c_EntityAlpha},
        {"EntityFX%entity%fx", c_EntityFX},
        {"EntityBlend%entity%blend", c_EntityBlend},
        {"EntityOrder%entity%order", c_EntityOrder},
        {"EntityShininess%entity#shininess", c_EntityShininess},
        {"EntityAutoFade%entity#near#far", c_EntityAutoFade},
        {"NameEntity%entity$name", c_NameEntity},
        {"$EntityName%entity", c_EntityName},
        {"$EntityClass%entity", c_EntityClass},

        {"AmbientLight#red#green#blue", c_AmbientLight},
        {"CameraRange%camera#near#far", c_CameraRange},
        {"CameraZoom%camera#zoom", c_CameraZoom},
        {"CameraViewport%camera%x%y%width%height", c_CameraViewport},
        {"CameraClsColor%camera#red#green#blue", c_CameraClsColor},
        {"CameraClsMode%camera%cls_color%cls_zbuffer", c_CameraClsMode},
        {"CameraProjMode%camera%mode", c_CameraProjMode},
        {"CameraFogColor%camera#red#green#blue", c_CameraFogColor},
        {"CameraFogRange%camera#near#far", c_CameraFogRange},
        {"CameraFogMode%camera%mode", c_CameraFogMode},

        {"LightColor%light#red#green#blue", c_LightColor},
        {"LightRange%light#range", c_LightRange},
        {"LightConeAngles%light#inner_angle#outer_angle", c_LightConeAngles},

        {"%LoadMesh$file%parent=0", c_LoadMesh},
        {"%LoadAnimMesh$file%parent=0", c_LoadAnimMesh},
        {"%LoadAnimSeq%entity$file", c_LoadAnimSeq},
        {"SetAnimTime%entity#time%anim_seq=0", c_SetAnimTime},
        {"Animate%entity%mode=1#speed=1%sequence=0#transition=0", c_Animate},
        {"SetAnimKey%entity%frame%pos_key=1%rot_key=1%scale_key=1", c_SetAnimKey},
        {"%AddAnimSeq%entity%length", c_AddAnimSeq},
        {"%ExtractAnimSeq%entity%first_frame%last_frame%anim_seq=0", c_ExtractAnimSeq},
        {"%AnimSeq%entity", c_AnimSeq},
        {"#AnimTime%entity", c_AnimTime},
        {"%AnimLength%entity", c_AnimLength},
        {"%Animating%entity", c_Animating},
        {"%LoadMD2$file%parent=0", c_LoadMD2},
        {"AnimateMD2%md2%mode=1#speed=1%first_frame=0%last_frame=9999#transition=0", c_AnimateMD2},
        {"#MD2AnimTime%md2", c_MD2AnimTime},
        {"%MD2AnimLength%md2", c_MD2AnimLength},
        {"%MD2Animating%md2", c_MD2Animating},

        {"UpdateWorld#elapsed_time=1", c_UpdateWorld},
        {"RenderWorld#tween=1", c_RenderWorld},
        {"CaptureWorld", c_CaptureWorld},
        {"%CameraPick%camera#viewport_x#viewport_y", c_CameraPick},
        {"%LinePick#x#y#z#dx#dy#dz#radius=0", c_LinePick},
        {"%EntityPick%entity#range", c_EntityPick},
        {"EntityPickMode%entity%pick_geometry%obscurer=1", c_EntityPickMode},
        {"#PickedX", c_PickedX},
        {"#PickedY", c_PickedY},
        {"#PickedZ", c_PickedZ},
        {"#PickedNX", c_PickedNX},
        {"#PickedNY", c_PickedNY},
        {"#PickedNZ", c_PickedNZ},
        {"#PickedTime", c_PickedTime},
        {"%PickedEntity", c_PickedEntity},
        {"%PickedSurface", c_PickedSurface},
        {"%PickedTriangle", c_PickedTriangle},
    };
    extern const int bb3d_cmds_world_count = (int)(sizeof(bb3d_cmds_world) / sizeof(bb3d_cmds_world[0]));
}
