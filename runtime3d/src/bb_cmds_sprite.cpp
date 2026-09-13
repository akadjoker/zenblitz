#include "runtime.h"
#include "vm.h"
#include "object.h"
#include "engine/Sprite.h"
#include "engine/TextureCache.h"
#include "engine/Texture.h"
#include "engine/Platform.h"

namespace bb3d
{
    // Textures owned by sprites. A BrushTexture stores only a GPU handle,
    // and nothing in Sprite's destruction path releases the Texture it
    // came from, so these stay alive for the process - see LoadSprite.

    extern engine::Entity *entity_of(long long h);
    extern long long store_entity(engine::Entity *e);
    extern void insert_entity(engine::Entity *e, engine::Entity *parent);
    extern engine::Platform *platform_for(zen::VM *vm);
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
    inline const char *arg_cstr(zen::Value v)
    {
        return zen::is_string(v) ? zen::as_cstring(v) : "";
    }

    engine::Sprite *sprite_of(long long h)
    {
        engine::Entity *e = bb3d::entity_of(h);
        return e ? e->getSprite() : nullptr;
    }
}

using namespace zen;

namespace bb3d
{
    static int c_CreateSprite(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *parent = entity_of(arg_int(args[0]));
        engine::Sprite *s = new engine::Sprite();
        s->setFX(engine::FxFullbright);
        insert_entity(s, parent);
        args[0] = val_int(store_entity(s));
        return 1;
    }

    static int c_LoadSprite(VM *vm, Value *args, int nargs)
    {
        const char *file = arg_cstr(args[0]);
        int flags = nargs > 1 ? (int)arg_int(args[1]) : 1;
        engine::Entity *parent = nargs > 2 ? entity_of(arg_int(args[2])) : nullptr;

        engine::Texture *tex = engine::TextureCache::acquire(file, flags);
        if (!tex)
        {
            char msg[512];
            snprintf(msg, sizeof(msg), "LoadSprite: could not load \"%s\"", file);
            zen::backend_log(vm->backend(), zen::LOG_WARN, msg);
            args[0] = val_int(0);
            return 1;
        }

        engine::Sprite *s = new engine::Sprite();
        s->setTexture(0, engine::BrushTexture::fromTexture(platform_for(vm)->device(), tex, 0));
        s->setFX(engine::FxFullbright);
        // bbLoadSprite's blend selection, which only the masked case was
        // carrying over: masked sprites draw opaque, an alpha texture
        // blends, and everything else is additive (how the original's
        // spark/flare sprites glow).
        if (flags & engine::TexMask) s->setBlend(engine::BlendReplace);
        else if (flags & engine::TexAlpha) s->setBlend(engine::BlendAlpha);
        else s->setBlend(engine::BlendAdd);

        // Not released here: the Sprite's brush keeps only the GPU handle
        // copied out above, so dropping the last reference would destroy
        // that GPU texture and the sprite would bind a dead handle every
        // frame. TextureCache owns it and releases it on shutdown.

        insert_entity(s, parent);
        args[0] = val_int(store_entity(s));
        return 1;
    }

    static int c_RotateSprite(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Sprite *s = sprite_of(arg_int(args[0]));
        if (s) s->setRotation(arg_float(args[1]) * blitz::PI / 180.0f);
        return 0;
    }

    static int c_ScaleSprite(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Sprite *s = sprite_of(arg_int(args[0]));
        if (s) s->setScale(arg_float(args[1]), arg_float(args[2]));
        return 0;
    }

    static int c_HandleSprite(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Sprite *s = sprite_of(arg_int(args[0]));
        if (s) s->setHandle(arg_float(args[1]), arg_float(args[2]));
        return 0;
    }

    static int c_SpriteViewMode(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Sprite *s = sprite_of(arg_int(args[0]));
        if (s) s->setViewmode((int)arg_int(args[1]));
        return 0;
    }

    extern const zen::CommandDecl bb3d_cmds_sprite[] = {
        {"%CreateSprite%parent=0", c_CreateSprite},
        {"%LoadSprite$file%texture_flags=1%parent=0", c_LoadSprite},
        {"RotateSprite%sprite#angle", c_RotateSprite},
        {"ScaleSprite%sprite#x_scale#y_scale", c_ScaleSprite},
        {"HandleSprite%sprite#x_handle#y_handle", c_HandleSprite},
        {"SpriteViewMode%sprite%view_mode", c_SpriteViewMode},
    };
    extern const int bb3d_cmds_sprite_count = (int)(sizeof(bb3d_cmds_sprite) / sizeof(bb3d_cmds_sprite[0]));
}
