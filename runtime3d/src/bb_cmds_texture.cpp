/*
** bb_cmds_texture.cpp — CreateTexture, LoadTexture, LoadAnimTexture,
** FreeTexture, ScaleTexture, RotateTexture, PositionTexture,
** TextureBlend, TextureCoords, EntityTexture, BrushTexture,
** CreateBrush, PaintEntity.
*/
#include "runtime.h"
#include "vm.h"
#include "object.h"
#include "ct/hashmap.hpp"
#include "ct/vector.hpp"
#include "engine/Platform.h"
#include "engine/Texture.h"
#include "engine/Model.h"
#include "engine/VoxelSprite.h"
#include <cstdio>

namespace bb3d
{
    extern engine::Platform *platform_for(zen::VM *vm);
    extern engine::Entity *entity_of(long long h);
    // bb_cmds_image.cpp: encodes a (texture, frame) pair into the same
    // negative handle space ImageBuffer uses, so SetBuffer/Color/Rect/
    // Text and friends can target a CreateTexture()'d texture's canvas
    // without knowing it isn't an Image.
    extern long long texture_buffer_handle(long long texture, int frame);
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
    inline const char *arg_cstr(zen::Value v) { return zen::is_string(v) ? zen::as_cstring(v) : ""; }
}

using namespace zen;

namespace bb3d
{
    static ct::HashMap<long long, engine::Texture *> g_textures;
    static long long g_next_texture = 0;

    struct VoxelMaterial
    {
        engine::Texture *texture = nullptr;
        int columns = 1, rows = 1;
        int firstFrame = 0, frameCount = 1;
    };
    static ct::HashMap<long long, VoxelMaterial *> g_voxel_materials;
    static long long g_next_voxel_material = 0;

    static long long store_texture(engine::Texture *t)
    {
        long long h = ++g_next_texture;
        g_textures.put(h, t);
        return h;
    }

    // extern: called by shutdown_graphics() when the program ends, so
    // textures a script never FreeTexture'd still get released.
    // every CreateBrush allocation, so the ones a script never passed to
    // FreeBrush are still released at shutdown.
    static ct::Vector<engine::Brush *> g_brushes;

    void free_all_textures()
    {
        for (auto &e : g_textures) engine::Texture::release(e.value);
        g_textures.clear();
        g_next_texture = 0;
        for (auto &e : g_voxel_materials)
        {
            engine::Texture::release(e.value->texture);
            delete e.value;
        }
        g_voxel_materials.clear();
        g_next_voxel_material = 0;
        for (size_t k = 0; k < g_brushes.size(); ++k) delete g_brushes[k];
        g_brushes.clear();
    }

    engine::Texture *texture_of(long long h)
    {
        engine::Texture **found = g_textures.find(h);
        return found ? *found : nullptr;
    }

    static void warn_load_failed(VM *vm, const char *cmd, const char *file)
    {
        char msg[512];
        snprintf(msg, sizeof(msg), "%s: could not load \"%s\"", cmd, file);
        zen::backend_log(vm->backend(), zen::LOG_WARN, msg);
    }

    static int c_LoadTexture(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        const char *file = arg_cstr(args[0]);
        engine::Texture *t = engine::Texture::load(file, (int)arg_int(args[1]));
        if (!t) warn_load_failed(vm, "LoadTexture", file);
        args[0] = val_int(t ? store_texture(t) : 0);
        return 1;
    }

    static int c_CreateTexture(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        engine::Texture *t = engine::Texture::create(
            (int)arg_int(args[0]), (int)arg_int(args[1]), (int)arg_int(args[2]), (int)arg_int(args[3]));
        if (!t)
        {
            char msg[128];
            snprintf(msg, sizeof(msg), "CreateTexture: illegal size or frame count");
            zen::backend_log(vm->backend(), zen::LOG_WARN, msg);
        }
        args[0] = val_int(t ? store_texture(t) : 0);
        return 1;
    }

    /* Dimensions of the texture's first frame, and the filename it was
       loaded from (empty for a CreateTexture'd one, which has no file) -
       the original read these off the CachedTexture behind the Texture. */
    static int c_TextureWidth(VM *vm, Value *args, int)
    {
        (void)vm;
        engine::Texture *t = texture_of(arg_int(args[0]));
        args[0] = val_int(t ? t->width() : 0);
        return 1;
    }
    static int c_TextureHeight(VM *vm, Value *args, int)
    {
        (void)vm;
        engine::Texture *t = texture_of(arg_int(args[0]));
        args[0] = val_int(t ? t->height() : 0);
        return 1;
    }
    static int c_TextureName(VM *vm, Value *args, int)
    {
        engine::Texture *t = texture_of(arg_int(args[0]));
        args[0] = val_obj((Obj *)vm->make_string(t ? t->getName().c_str() : ""));
        return 1;
    }

    // TextureBuffer(texture,frame): only a create()d texture has a canvas
    // to draw into (Texture::canvas is null for a load()ed one, same as
    // the original's getCanvas() needing color depth to draw with) - so
    // this returns 0 for anything else, and SetBuffer's own "buffer does
    // not exist" check catches a script trying to draw on it anyway.
    static int c_TextureBuffer(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        const long long handle = arg_int(args[0]);
        const int frame = (int)arg_int(args[1]);
        engine::Texture *t = texture_of(handle);
        args[0] = val_int(t && t->canvas(frame) ? texture_buffer_handle(handle, frame) : 0);
        return 1;
    }

    static int c_LoadAnimTexture(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        const char *file = arg_cstr(args[0]);
        engine::Texture *t = engine::Texture::loadAnim(file, (int)arg_int(args[1]),
                                                       (int)arg_int(args[2]), (int)arg_int(args[3]),
                                                       (int)arg_int(args[4]), (int)arg_int(args[5]));
        if (!t) warn_load_failed(vm, "LoadAnimTexture", file);
        args[0] = val_int(t ? store_texture(t) : 0);
        return 1;
    }

    static int c_LoadMaterial(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        const char *file = arg_cstr(args[0]);
        engine::Texture *texture = engine::Texture::load(file, (int)arg_int(args[1]));
        if (!texture) { warn_load_failed(vm, "LoadMaterial", file); args[0] = val_int(0); return 1; }

        int frameWidth = (int)arg_int(args[2]);
        int frameHeight = (int)arg_int(args[3]);
        if (frameWidth < 1) frameWidth = texture->width();
        if (frameHeight < 1) frameHeight = texture->height();
        int columns = frameWidth > 0 ? texture->width() / frameWidth : 0;
        int rows = frameHeight > 0 ? texture->height() / frameHeight : 0;
        if (columns < 1 || rows < 1)
        {
            engine::Texture::release(texture);
            args[0] = val_int(0);
            return 1;
        }
        const int total = columns * rows;
        int firstFrame = (int)arg_int(args[4]);
        if (firstFrame < 0) firstFrame = 0;
        if (firstFrame >= total) firstFrame = total - 1;
        int frameCount = (int)arg_int(args[5]);
        if (frameCount < 1 || frameCount > total - firstFrame) frameCount = total - firstFrame;

        VoxelMaterial *material = new VoxelMaterial;
        material->texture = texture;
        material->columns = columns;
        material->rows = rows;
        material->firstFrame = firstFrame;
        material->frameCount = frameCount;
        const long long handle = ++g_next_voxel_material;
        g_voxel_materials.put(handle, material);
        args[0] = val_int(handle);
        return 1;
    }

    static int c_FreeMaterial(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        const long long handle = arg_int(args[0]);
        VoxelMaterial **found = g_voxel_materials.find(handle);
        if (!found) return 0;
        engine::Texture::release((*found)->texture);
        delete *found;
        g_voxel_materials.erase(handle);
        return 0;
    }

    static int c_FreeTexture(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        long long h = arg_int(args[0]);
        engine::Texture *t = texture_of(h);
        if (t) { engine::Texture::release(t); g_textures.erase(h); }
        return 0;
    }

    static int c_ScaleTexture(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Texture *t = texture_of(arg_int(args[0]));
        if (t) t->setScale(1.0f / arg_float(args[1]), 1.0f / arg_float(args[2]));
        return 0;
    }

    static int c_RotateTexture(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Texture *t = texture_of(arg_int(args[0]));
        if (t) t->setRotation(-arg_float(args[1]) * 0.0174532925199432957692369076848861f);
        return 0;
    }

    static int c_PositionTexture(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Texture *t = texture_of(arg_int(args[0]));
        if (t) t->setPosition(-arg_float(args[1]), -arg_float(args[2]));
        return 0;
    }

    static int c_TextureBlend(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Texture *t = texture_of(arg_int(args[0]));
        if (t) t->setBlend((int)arg_int(args[1]));
        return 0;
    }

    static int c_TextureCoords(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Texture *t = texture_of(arg_int(args[0]));
        if (t) t->setFlags((int)arg_int(args[1]));
        return 0;
    }

    static int c_EntityTexture(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        engine::Model *m = e ? e->getModel() : nullptr;
        engine::Texture *t = texture_of(arg_int(args[1]));
        // Uploading needs a live GL device: before Graphics3D (or after
        // EndGraphics) Platform::device() dereferences a null mGpu and
        // segfaults inside Image::ensureUploaded. The original guarded
        // every 3D command with debug3d()'s "3D Graphics mode not set"
        // runtime error rather than crashing - same thing here.
        if (!platform_for(vm)->isOpen())
        {
            vm->runtime_error("3D Graphics mode not set");
            return -1;
        }
        if (m && t)
        {
            int frame = (int)arg_int(args[2]);
            int index = (int)arg_int(args[3]);
            m->setTexture(index, engine::BrushTexture::fromTexture(platform_for(vm)->device(), t, frame));
        }
        return 0;
    }

    static int c_VoxelSpriteTexture(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        engine::Entity *entity = entity_of(arg_int(args[0]));
        engine::VoxelSprite *voxel = entity && entity->getModel()
            ? dynamic_cast<engine::VoxelSprite *>(entity->getModel()) : nullptr;
        engine::Texture *texture = texture_of(arg_int(args[1]));
        if (!voxel || !texture) return 0;

        int frameWidth = (int)arg_int(args[2]);
        int frameHeight = (int)arg_int(args[3]);
        if (frameWidth < 1) frameWidth = texture->width();
        if (frameHeight < 1) frameHeight = texture->height();
        int columns = frameWidth > 0 ? texture->width() / frameWidth : 1;
        int rows = frameHeight > 0 ? texture->height() / frameHeight : 1;
        if (columns < 1) columns = 1;
        if (rows < 1) rows = 1;
        voxel->setTexture(0, engine::BrushTexture::fromTexture(platform_for(vm)->device(), texture, 0));
        voxel->setAtlas(columns, rows, (int)arg_int(args[4]), (int)arg_int(args[5]));
        return 0;
    }

    static int c_VoxelSpriteMaterial(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        engine::Entity *entity = entity_of(arg_int(args[0]));
        engine::VoxelSprite *voxel = entity && entity->getModel()
            ? dynamic_cast<engine::VoxelSprite *>(entity->getModel()) : nullptr;
        VoxelMaterial **found = g_voxel_materials.find(arg_int(args[1]));
        if (!voxel || !found) return 0;
        VoxelMaterial *material = *found;
        voxel->setTexture(0, engine::BrushTexture::fromTexture(platform_for(vm)->device(), material->texture, 0));
        voxel->setAtlas(material->columns, material->rows, material->firstFrame, material->frameCount);
        return 0;
    }

    static int c_CreateBrush(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Brush *b = new engine::Brush();
        g_brushes.push_back(b);
        b->setColor(engine::Vector(arg_float(args[0]) / 255.0f, arg_float(args[1]) / 255.0f, arg_float(args[2]) / 255.0f));
        args[0] = val_int((long long)(std::intptr_t)b);
        return 1;
    }

    static int c_FreeBrush(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Brush *b = (engine::Brush *)(std::intptr_t)arg_int(args[0]);
        for (size_t k = 0; k < g_brushes.size(); ++k)
            if (g_brushes[k] == b) { g_brushes.erase(g_brushes.begin() + k); break; }
        delete b;
        return 0;
    }

    static int c_BrushColor(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Brush *b = (engine::Brush *)(std::intptr_t)arg_int(args[0]);
        if (b) b->setColor(engine::Vector(arg_float(args[1]) / 255.0f, arg_float(args[2]) / 255.0f, arg_float(args[3]) / 255.0f));
        return 0;
    }

    static int c_BrushAlpha(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Brush *b = (engine::Brush *)(std::intptr_t)arg_int(args[0]);
        if (b) b->setAlpha(arg_float(args[1]));
        return 0;
    }

    static int c_BrushShininess(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Brush *b = (engine::Brush *)(std::intptr_t)arg_int(args[0]);
        if (b) b->setShininess(arg_float(args[1]));
        return 0;
    }

    static int c_BrushBlend(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Brush *b = (engine::Brush *)(std::intptr_t)arg_int(args[0]);
        if (b) b->setBlend((int)arg_int(args[1]));
        return 0;
    }

    static int c_BrushFX(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Brush *b = (engine::Brush *)(std::intptr_t)arg_int(args[0]);
        if (b) b->setFX((int)arg_int(args[1]));
        return 0;
    }

    static int c_BrushTexture(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        engine::Brush *b = (engine::Brush *)(std::intptr_t)arg_int(args[0]);
        engine::Texture *t = texture_of(arg_int(args[1]));
        if (b && t)
        {
            int frame = (int)arg_int(args[2]);
            int index = (int)arg_int(args[3]);
            b->setTexture(index, engine::BrushTexture::fromTexture(platform_for(vm)->device(), t, frame));
        }
        return 0;
    }

    static int c_PaintEntity(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *e = entity_of(arg_int(args[0]));
        engine::Model *m = e ? e->getModel() : nullptr;
        engine::Brush *b = (engine::Brush *)(std::intptr_t)arg_int(args[1]);
        if (m && b) m->setBrush(*b);
        return 0;
    }

    /* bbLoadBrush (bbblitz3d.cpp:487): load a texture, optionally rescale
       its UVs, and hand back a plain white brush carrying it on slot 0.
       The original passes 1/u_scale to Texture::setScale exactly as
       ScaleTexture does, so the same inversion is kept here. A failed
       texture load returns 0 (no brush created), matching the original's
       early-out on a null canvas. */
    static int c_LoadBrush(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        const char *file = arg_cstr(args[0]);
        engine::Texture *t = engine::Texture::load(file, (int)arg_int(args[1]));
        if (!t)
        {
            warn_load_failed(vm, "LoadBrush", file);
            args[0] = val_int(0);
            return 1;
        }
        const float uScale = arg_float(args[2]), vScale = arg_float(args[3]);
        if (uScale != 1.0f || vScale != 1.0f) t->setScale(1.0f / uScale, 1.0f / vScale);

        engine::Brush *b = new engine::Brush();
        g_brushes.push_back(b);
        b->setColor(engine::Vector(1.0f, 1.0f, 1.0f));
        b->setTexture(0, engine::BrushTexture::fromTexture(platform_for(vm)->device(), t, 0));
        store_texture(t);
        args[0] = val_int((long long)(std::intptr_t)b);
        return 1;
    }

    extern const zen::CommandDecl bb3d_cmds_texture[] = {
        {"%LoadTexture$file%flags=1", c_LoadTexture},
        {"%CreateTexture%width%height%flags=1%frames=1", c_CreateTexture},
        {"%TextureBuffer%texture%frame=0", c_TextureBuffer},
        {"%TextureWidth%texture", c_TextureWidth},
        {"%TextureHeight%texture", c_TextureHeight},
        {"$TextureName%texture", c_TextureName},
        {"%LoadAnimTexture$file%flags%width%height%first%count", c_LoadAnimTexture},
        {"%LoadMaterial$file%flags=0%frame_width=0%frame_height=0%first_frame=0%frame_count=0", c_LoadMaterial},
        {"FreeTexture%texture", c_FreeTexture},
        {"FreeMaterial%material", c_FreeMaterial},
        {"ScaleTexture%texture#u_scale#v_scale", c_ScaleTexture},
        {"RotateTexture%texture#angle", c_RotateTexture},
        {"PositionTexture%texture#u_offset#v_offset", c_PositionTexture},
        {"TextureBlend%texture%blend", c_TextureBlend},
        {"TextureCoords%texture%coords", c_TextureCoords},
        {"EntityTexture%entity%texture%frame=0%index=0", c_EntityTexture},
        {"VoxelSpriteTexture%voxel%texture%frame_width=0%frame_height=0%first_frame=0%frame_count=0", c_VoxelSpriteTexture},
        {"VoxelSpriteMaterial%voxel%material", c_VoxelSpriteMaterial},

        {"%CreateBrush#red=255#green=255#blue=255", c_CreateBrush},
        {"%LoadBrush$file%texture_flags=1#u_scale=1#v_scale=1", c_LoadBrush},
        {"FreeBrush%brush", c_FreeBrush},
        {"BrushColor%brush#red#green#blue", c_BrushColor},
        {"BrushAlpha%brush#alpha", c_BrushAlpha},
        {"BrushShininess%brush#shininess", c_BrushShininess},
        {"BrushBlend%brush%blend", c_BrushBlend},
        {"BrushFX%brush%fx", c_BrushFX},
        {"BrushTexture%brush%texture%frame=0%index=0", c_BrushTexture},
        {"PaintEntity%entity%brush", c_PaintEntity},
    };
    extern const int bb3d_cmds_texture_count = (int)(sizeof(bb3d_cmds_texture) / sizeof(bb3d_cmds_texture[0]));
}
