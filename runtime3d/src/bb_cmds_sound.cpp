#include "runtime.h"
#include "vm.h"
#include "object.h"
#include "engine/Sound.h"
#include "engine/Entity.h"
#include "engine/Object.h"
#include "engine/Listener.h"

namespace bb3d
{
    extern engine::Entity *entity_of(long long h);
    extern long long store_entity(engine::Entity *e);
    extern void insert_entity(engine::Entity *e, engine::Entity *parent);
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

    void warn_load_failed(zen::VM *vm, const char *cmd, const char *file)
    {
        char msg[512];
        snprintf(msg, sizeof(msg), "%s: could not load \"%s\"", cmd, file);
        zen::backend_log(vm->backend(), zen::LOG_WARN, msg);
    }
}

using namespace zen;

namespace bb3d
{
    static int c_LoadSound(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        const char *file = arg_cstr(args[0]);
        engine::AudioSystem::SoundId id = engine::AudioSystem::get().loadSound(file, false);
        if (!id) warn_load_failed(vm, "LoadSound", file);
        args[0] = val_int(id);
        return 1;
    }

    static int c_Load3DSound(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        const char *file = arg_cstr(args[0]);
        engine::AudioSystem::SoundId id = engine::AudioSystem::get().loadSound(file, true);
        if (!id) warn_load_failed(vm, "Load3DSound", file);
        args[0] = val_int(id);
        return 1;
    }

    static int c_FreeSound(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::AudioSystem::get().freeSound((int)arg_int(args[0]));
        return 0;
    }

    static int c_LoopSound(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::AudioSystem::get().setSoundLoop((int)arg_int(args[0]), true);
        return 0;
    }

    static int c_SoundPitch(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::AudioSystem::get().setSoundPitchHz((int)arg_int(args[0]), arg_float(args[1]));
        return 0;
    }

    static int c_SoundVolume(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::AudioSystem::get().setSoundVolume((int)arg_int(args[0]), arg_float(args[1]));
        return 0;
    }

    static int c_SoundPan(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::AudioSystem::get().setSoundPan((int)arg_int(args[0]), arg_float(args[1]));
        return 0;
    }

    static int c_PlaySound(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        args[0] = val_int(engine::AudioSystem::get().play((int)arg_int(args[0])));
        return 1;
    }

    static int c_PlayMusic(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        const char *file = arg_cstr(args[0]);
        engine::AudioSystem::ChannelId id = engine::AudioSystem::get().playFile(file, false);
        if (!id) warn_load_failed(vm, "PlayMusic", file);
        args[0] = val_int(id);
        return 1;
    }

    static int c_PlayCDTrack(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)args; (void)nargs;
        args[0] = val_int(0);
        return 1;
    }

    static int c_StopChannel(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::AudioSystem::get().stopChannel((int)arg_int(args[0]));
        return 0;
    }

    static int c_PauseChannel(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::AudioSystem::get().setChannelPaused((int)arg_int(args[0]), true);
        return 0;
    }

    static int c_ResumeChannel(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::AudioSystem::get().setChannelPaused((int)arg_int(args[0]), false);
        return 0;
    }

    static int c_ChannelPitch(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::AudioSystem::get().setChannelPitchHz((int)arg_int(args[0]), arg_float(args[1]));
        return 0;
    }

    static int c_ChannelVolume(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::AudioSystem::get().setChannelVolume((int)arg_int(args[0]), arg_float(args[1]));
        return 0;
    }

    static int c_ChannelPan(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::AudioSystem::get().setChannelPan((int)arg_int(args[0]), arg_float(args[1]));
        return 0;
    }

    static int c_ChannelPlaying(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        args[0] = val_int(engine::AudioSystem::get().isChannelPlaying((int)arg_int(args[0])) ? 1 : 0);
        return 1;
    }

    static int c_CreateListener(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::Entity *parent = entity_of(arg_int(args[0]));
        engine::Listener *l = new engine::Listener(arg_float(args[1]), arg_float(args[2]), arg_float(args[3]));
        insert_entity(l, parent);
        args[0] = val_int(store_entity(l));
        return 1;
    }

    static int c_EmitSound(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        engine::AudioSystem::SoundId sound = (int)arg_int(args[0]);
        engine::Entity *e = entity_of(arg_int(args[1]));
        engine::Object *o = e ? e->getObject() : nullptr;
        args[0] = val_int(o ? engine::AudioSystem::get().emit(sound, o) : 0);
        return 1;
    }

    extern const zen::CommandDecl bb3d_cmds_sound[] = {
        {"%LoadSound$filename", c_LoadSound},
        {"%Load3DSound$filename", c_Load3DSound},
        {"FreeSound%sound", c_FreeSound},
        {"LoopSound%sound", c_LoopSound},
        {"SoundPitch%sound%pitch", c_SoundPitch},
        {"SoundVolume%sound#volume", c_SoundVolume},
        {"SoundPan%sound#pan", c_SoundPan},
        {"%PlaySound%sound", c_PlaySound},
        {"%PlayMusic$midifile", c_PlayMusic},
        {"%PlayCDTrack%track%mode=1", c_PlayCDTrack},
        {"StopChannel%channel", c_StopChannel},
        {"PauseChannel%channel", c_PauseChannel},
        {"ResumeChannel%channel", c_ResumeChannel},
        {"ChannelPitch%channel%pitch", c_ChannelPitch},
        {"ChannelVolume%channel#volume", c_ChannelVolume},
        {"ChannelPan%channel#pan", c_ChannelPan},
        {"%ChannelPlaying%channel", c_ChannelPlaying},
        {"%CreateListener%parent#rolloff_factor=1#doppler_scale=1#distance_scale=1", c_CreateListener},
        {"%EmitSound%sound%entity", c_EmitSound},
    };
    extern const int bb3d_cmds_sound_count = (int)(sizeof(bb3d_cmds_sound) / sizeof(bb3d_cmds_sound[0]));
}
