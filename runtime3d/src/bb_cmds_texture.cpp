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

namespace bb3d
{
    extern engine::Platform *platform_for(zen::VM *vm);
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
    inline const char *arg_cstr(zen::Value v) { return zen::is_string(v) ? zen::as_cstring(v) : ""; }
}

using namespace zen;

namespace bb3d
{
    static ct::HashMap<long long, engine::Texture *> g_textures;
    static long long g_next_texture = 0;

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
        for (size_t k = 0; k < g_brushes.size(); ++k) delete g_brushes[k];
        g_brushes.clear();
    }

    engine::Texture *texture_of(long long h)
    {
        engine::Texture **found = g_textures.find(h);
        return found ? *found : nullptr;
    }

    static int c_LoadTexture(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Texture *t = engine::Texture::load(arg_cstr(args[0]), (int)arg_int(args[1]));
        args[0] = val_int(t ? store_texture(t) : 0);
        return 1;
    }

    static int c_LoadAnimTexture(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Texture *t = engine::Texture::loadAnim(arg_cstr(args[0]), (int)arg_int(args[1]),
                                                       (int)arg_int(args[2]), (int)arg_int(args[3]),
                                                       (int)arg_int(args[4]), (int)arg_int(args[5]));
        args[0] = val_int(t ? store_texture(t) : 0);
        return 1;
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
        if (m && t)
        {
            int frame = (int)arg_int(args[2]);
            int index = (int)arg_int(args[3]);
            m->setTexture(index, engine::BrushTexture::fromTexture(platform_for(vm)->device(), t, frame));
        }
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

    extern const zen::CommandDecl bb3d_cmds_texture[] = {
        {"%LoadTexture$file%flags=1", c_LoadTexture},
        {"%LoadAnimTexture$file%flags%width%height%first%count", c_LoadAnimTexture},
        {"FreeTexture%texture", c_FreeTexture},
        {"ScaleTexture%texture#u_scale#v_scale", c_ScaleTexture},
        {"RotateTexture%texture#angle", c_RotateTexture},
        {"PositionTexture%texture#u_offset#v_offset", c_PositionTexture},
        {"TextureBlend%texture%blend", c_TextureBlend},
        {"TextureCoords%texture%coords", c_TextureCoords},
        {"EntityTexture%entity%texture%frame=0%index=0", c_EntityTexture},

        {"%CreateBrush#red=255#green=255#blue=255", c_CreateBrush},
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
