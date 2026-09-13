#include "runtime.h"
#include "vm.h"
#include "engine/ParticleEmitter.h"
#include "engine/Texture.h"
#include "engine/Platform.h"

namespace bb3d
{
    extern engine::Entity *entity_of(long long h);
    extern long long store_entity(engine::Entity *e);
    extern void insert_entity(engine::Entity *e, engine::Entity *parent);
    extern engine::Texture *texture_of(long long h);
    extern engine::Platform *platform_for(zen::VM *vm);
}

namespace
{
    inline long long arg_int(zen::Value value)
    {
        return zen::is_int(value) ? value.as.integer : zen::is_float(value) ? (long long)value.as.number : 0;
    }
    inline float arg_float(zen::Value value)
    {
        return zen::is_float(value) ? (float)value.as.number : zen::is_int(value) ? (float)value.as.integer : 0.0f;
    }
    engine::ParticleEmitter *emitter_of(long long handle)
    {
        engine::Entity *entity = bb3d::entity_of(handle);
        return entity && entity->getModel() ? dynamic_cast<engine::ParticleEmitter *>(entity->getModel()) : nullptr;
    }
}

using namespace zen;

namespace bb3d
{
    static int c_CreateParticleEmitter(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::ParticleEmitter *emitter = new engine::ParticleEmitter((int)arg_int(args[0]));
        insert_entity(emitter, entity_of(arg_int(args[1])));
        args[0] = val_int(store_entity(emitter));
        return 1;
    }

    static int c_EmitterTexture(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        engine::ParticleEmitter *emitter = emitter_of(arg_int(args[0]));
        engine::Texture *texture = texture_of(arg_int(args[1]));
        if (!emitter || !texture) return 0;
        int frameWidth = (int)arg_int(args[2]);
        int frameHeight = (int)arg_int(args[3]);
        if (frameWidth < 1) frameWidth = texture->width();
        if (frameHeight < 1) frameHeight = texture->height();
        int columns = frameWidth > 0 ? texture->width() / frameWidth : 1;
        int rows = frameHeight > 0 ? texture->height() / frameHeight : 1;
        if (columns < 1) columns = 1;
        if (rows < 1) rows = 1;
        emitter->setTexture(0, engine::BrushTexture::fromTexture(platform_for(vm)->device(), texture, 0));
        emitter->setAtlas(columns, rows, (int)arg_int(args[4]), (int)arg_int(args[5]));
        return 0;
    }

    static int c_EmitterRate(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        if (engine::ParticleEmitter *emitter = emitter_of(arg_int(args[0]))) emitter->setRate(arg_float(args[1]));
        return 0;
    }

    static int c_EmitterVector(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        if (engine::ParticleEmitter *emitter = emitter_of(arg_int(args[0])))
            emitter->setVector(engine::Vector(arg_float(args[1]), arg_float(args[2]), arg_float(args[3])));
        return 0;
    }

    static int c_EmitterSpread(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        if (engine::ParticleEmitter *emitter = emitter_of(arg_int(args[0])))
            emitter->setSpread(engine::Vector(arg_float(args[1]), arg_float(args[2]), arg_float(args[3])));
        return 0;
    }

    static int c_EmitterGravity(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        if (engine::ParticleEmitter *emitter = emitter_of(arg_int(args[0])))
            emitter->setGravity(engine::Vector(arg_float(args[1]), arg_float(args[2]), arg_float(args[3])));
        return 0;
    }

    static int c_EmitterLife(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        if (engine::ParticleEmitter *emitter = emitter_of(arg_int(args[0])))
        {
            float minimum = arg_float(args[1]);
            float maximum = arg_float(args[2]);
            emitter->setLife(minimum, maximum < 0.0f ? minimum : maximum);
        }
        return 0;
    }

    static int c_EmitterSpeed(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        if (engine::ParticleEmitter *emitter = emitter_of(arg_int(args[0])))
        {
            float minimum = arg_float(args[1]);
            float maximum = arg_float(args[2]);
            emitter->setSpeed(minimum, maximum < 0.0f ? minimum : maximum);
        }
        return 0;
    }

    static int c_EmitterSize(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        if (engine::ParticleEmitter *emitter = emitter_of(arg_int(args[0])))
        {
            float start = arg_float(args[1]);
            float end = arg_float(args[2]);
            emitter->setSize(start, end < 0.0f ? start : end);
        }
        return 0;
    }

    static int c_EmitterColor(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        if (engine::ParticleEmitter *emitter = emitter_of(arg_int(args[0])))
        {
            engine::Vector start(arg_float(args[1]) / 255.0f, arg_float(args[2]) / 255.0f, arg_float(args[3]) / 255.0f);
            engine::Vector end(arg_float(args[4]) / 255.0f, arg_float(args[5]) / 255.0f, arg_float(args[6]) / 255.0f);
            emitter->setColor(start, end);
        }
        return 0;
    }

    static int c_EmitterAlpha(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        if (engine::ParticleEmitter *emitter = emitter_of(arg_int(args[0]))) emitter->setAlpha(arg_float(args[1]), arg_float(args[2]));
        return 0;
    }

    static int c_EmitterBlend(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        if (engine::ParticleEmitter *emitter = emitter_of(arg_int(args[0]))) emitter->setBlend((int)arg_int(args[1]));
        return 0;
    }

    static int c_EmitParticles(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        if (engine::ParticleEmitter *emitter = emitter_of(arg_int(args[0]))) emitter->emit((int)arg_int(args[1]));
        return 0;
    }

    static int c_ClearParticles(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        if (engine::ParticleEmitter *emitter = emitter_of(arg_int(args[0]))) emitter->clearParticles();
        return 0;
    }

    static int c_CountParticles(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::ParticleEmitter *emitter = emitter_of(arg_int(args[0]));
        args[0] = val_int(emitter ? emitter->particleCount() : 0);
        return 1;
    }

    extern const CommandDecl bb3d_cmds_particle[] = {
        {"%CreateParticleEmitter%max_particles=512%parent=0", c_CreateParticleEmitter},
        {"EmitterTexture%emitter%texture%frame_width=0%frame_height=0%first_frame=0%frame_count=0", c_EmitterTexture},
        {"EmitterRate%emitter#particles_per_tick", c_EmitterRate},
        {"EmitterVector%emitter#x#y#z", c_EmitterVector},
        {"EmitterSpread%emitter#x#y#z", c_EmitterSpread},
        {"EmitterGravity%emitter#x#y#z", c_EmitterGravity},
        {"EmitterLife%emitter#minimum%maximum=-1", c_EmitterLife},
        {"EmitterSpeed%emitter#minimum#maximum=-1", c_EmitterSpeed},
        {"EmitterSize%emitter#start#end=-1", c_EmitterSize},
        {"EmitterColor%emitter#start_red#start_green#start_blue#end_red#end_green#end_blue", c_EmitterColor},
        {"EmitterAlpha%emitter#start#end", c_EmitterAlpha},
        {"EmitterBlend%emitter%blend", c_EmitterBlend},
        {"EmitParticles%emitter%count", c_EmitParticles},
        {"ClearParticles%emitter", c_ClearParticles},
        {"%CountParticles%emitter", c_CountParticles},
    };
    extern const int bb3d_cmds_particle_count = (int)(sizeof(bb3d_cmds_particle) / sizeof(bb3d_cmds_particle[0]));
}
