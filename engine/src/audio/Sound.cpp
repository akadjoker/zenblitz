#include "engine/Sound.h"
#include "engine/FilePath.h"
#include "engine/Object.h"
#include "engine/Log.h"
#include "miniaudio.h"

namespace engine
{
    namespace
    {
        float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

        unsigned queryNativeRate(const ct::String &path)
        {
            ma_decoder decoder;
            if (ma_decoder_init_file(path.c_str(), nullptr, &decoder) != MA_SUCCESS) return 0;
            const unsigned rate = decoder.outputSampleRate;
            ma_decoder_uninit(&decoder);
            return rate;
        }

        void applyPitchHz(ma_sound *player, float hz, unsigned nativeRate)
        {
            if (hz <= 0.0f || nativeRate == 0) return;
            ma_sound_set_pitch(player, clampf(hz / (float)nativeRate, 0.01f, 100.0f));
        }
    }

    AudioSystem &AudioSystem::get()
    {
        static AudioSystem instance;
        return instance;
    }

    AudioSystem::~AudioSystem() { shutdown(); }

    AudioSystem::Sound *AudioSystem::findSound(SoundId id) const
    {
        Sound *const *found = mSounds.find(id);
        return found ? *found : nullptr;
    }

    AudioSystem::Channel *AudioSystem::findChannel(ChannelId id) const
    {
        Channel *const *found = mChannels.find(id);
        return found ? *found : nullptr;
    }

    bool AudioSystem::init()
    {
        if (mReady) return true;
        if (mInitFailed) return false;

        mEngine = new ma_engine();
        ma_engine_config cfg = ma_engine_config_init();
        if (ma_engine_init(&cfg, mEngine) != MA_SUCCESS)
        {
            delete mEngine;
            mEngine = nullptr;
            mInitFailed = true;
            Log::error("AudioSystem: could not initialise the audio device");
            return false;
        }

        for (ma_uint32 i = 0; i < mEngine->listenerCount; ++i)
            mEngine->listeners[i].config.handedness = ma_handedness_left;

        mSfxGroup = new ma_sound();
        if (ma_sound_group_init(mEngine, 0, nullptr, mSfxGroup) != MA_SUCCESS)
        {
            ma_engine_uninit(mEngine);
            delete mEngine;
            delete mSfxGroup;
            mEngine = nullptr;
            mSfxGroup = nullptr;
            mInitFailed = true;
            Log::error("AudioSystem: could not create the sfx group");
            return false;
        }

        mReady = true;
        return true;
    }

    void AudioSystem::shutdown()
    {
        if (!mReady) return;

        for (auto &entry : mChannels)
        {
            Channel *c = entry.value;
            if (c->player) { ma_sound_uninit(c->player); delete c->player; }
            delete c;
        }
        mChannels.clear();
        for (auto &entry : mSounds) delete entry.value;
        mSounds.clear();

        if (mSfxGroup) { ma_sound_group_uninit(mSfxGroup); delete mSfxGroup; mSfxGroup = nullptr; }
        if (mEngine) { ma_engine_uninit(mEngine); delete mEngine; mEngine = nullptr; }
        mReady = false;
    }

    void AudioSystem::destroyChannel(ChannelId id, Channel *c)
    {
        if (c->player) { ma_sound_uninit(c->player); delete c->player; }
        delete c;
        mChannels.erase(id);
    }

    void AudioSystem::update()
    {
        if (!mReady) return;

        ct::Vector<ChannelId> finished;
        for (auto &entry : mChannels)
        {
            Channel *c = entry.value;
            if (c->player && !c->loop && ma_sound_at_end(c->player) == MA_TRUE)
            {
                finished.push_back(entry.key);
                continue;
            }
            if (c->follow && c->player)
            {
                const Vector &pos = c->follow->getWorldPosition();
                ma_sound_set_position(c->player, pos.x, pos.y, pos.z);
                const Vector &vel = c->follow->getVelocity();
                ma_sound_set_velocity(c->player, vel.x, vel.y, vel.z);
            }
        }
        for (size_t k = 0; k < finished.size(); ++k)
        {
            Channel *c = findChannel(finished[k]);
            if (c) destroyChannel(finished[k], c);
        }
    }

    AudioSystem::SoundId AudioSystem::loadSound(const char *path, bool is3D)
    {
        if (!path || !path[0]) return 0;
        if (!mReady && !init()) return 0;

        const ct::String resolved = resolveCaseInsensitive(path);
        const unsigned nativeRate = queryNativeRate(resolved);
        if (nativeRate == 0) return 0;

        Sound *s = new Sound();
        s->path = resolved;
        s->is3D = is3D;
        s->nativeRate = nativeRate;
        const SoundId id = mNextSound++;
        mSounds.put(id, s);
        return id;
    }

    void AudioSystem::freeSound(SoundId sound)
    {
        Sound *s = findSound(sound);
        if (!s) return;
        delete s;
        mSounds.erase(sound);
    }

    void AudioSystem::setSoundLoop(SoundId sound, bool loop) { if (Sound *s = findSound(sound)) s->loop = loop; }
    void AudioSystem::setSoundPitchHz(SoundId sound, float hz) { if (Sound *s = findSound(sound)) s->pitchHz = hz; }
    void AudioSystem::setSoundVolume(SoundId sound, float volume) { if (Sound *s = findSound(sound)) s->volume = volume; }
    void AudioSystem::setSoundPan(SoundId sound, float pan) { if (Sound *s = findSound(sound)) s->pan = pan; }

    AudioSystem::ChannelId AudioSystem::startPlayer(const ct::String &path, bool loop, float pitchHz,
                                                    unsigned nativeRate, float volume, float pan, bool is3D,
                                                    bool streamed)
    {
        if (!mReady && !init()) return 0;

        ma_sound *player = new ma_sound();
        const ma_uint32 flags = streamed ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
        if (ma_sound_init_from_file(mEngine, path.c_str(), flags, mSfxGroup, nullptr, player) != MA_SUCCESS)
        {
            delete player;
            return 0;
        }

        ma_sound_set_looping(player, loop ? MA_TRUE : MA_FALSE);
        applyPitchHz(player, pitchHz, nativeRate);
        ma_sound_set_volume(player, clampf(volume, 0.0f, 4.0f));
        ma_sound_set_spatialization_enabled(player, is3D ? MA_TRUE : MA_FALSE);
        if (is3D)
        {
            ma_sound_set_positioning(player, ma_positioning_absolute);
            ma_sound_set_attenuation_model(player, ma_attenuation_model_inverse);
            ma_sound_set_rolloff(player, mRolloff);
            ma_sound_set_doppler_factor(player, mDopplerScale);
            ma_sound_set_min_distance(player, 1.0f * mDistanceScale);
            ma_sound_set_max_distance(player, 1000000.0f * mDistanceScale);
        }
        else
        {
            ma_sound_set_pan(player, clampf(pan, -1.0f, 1.0f));
        }

        if (ma_sound_start(player) != MA_SUCCESS)
        {
            ma_sound_uninit(player);
            delete player;
            return 0;
        }

        Channel *c = new Channel();
        c->player = player;
        c->loop = loop;
        c->nativeRate = nativeRate;
        const ChannelId id = mNextChannel++;
        mChannels.put(id, c);
        return id;
    }

    AudioSystem::ChannelId AudioSystem::play(SoundId sound)
    {
        Sound *s = findSound(sound);
        if (!s) return 0;
        return startPlayer(s->path, s->loop, s->pitchHz, s->nativeRate, s->volume, s->pan, s->is3D, false);
    }

    AudioSystem::ChannelId AudioSystem::playFile(const char *path, bool loop)
    {
        if (!path || !path[0]) return 0;
        const ct::String resolved = resolveCaseInsensitive(path);
        return startPlayer(resolved, loop, 0.0f, 0, 1.0f, 0.0f, false, true);
    }

    AudioSystem::ChannelId AudioSystem::emit(SoundId sound, Object *follow)
    {
        Sound *s = findSound(sound);
        if (!s) return 0;
        ChannelId id = startPlayer(s->path, s->loop, s->pitchHz, s->nativeRate, s->volume, s->pan, s->is3D, false);
        if (Channel *c = findChannel(id))
        {
            c->follow = follow;
            if (follow)
            {
                const Vector &pos = follow->getWorldPosition();
                ma_sound_set_position(c->player, pos.x, pos.y, pos.z);
                const Vector &vel = follow->getVelocity();
                ma_sound_set_velocity(c->player, vel.x, vel.y, vel.z);
            }
        }
        return id;
    }

    void AudioSystem::detachFollower(Object *follow)
    {
        if (!follow) return;
        for (auto &entry : mChannels)
            if (entry.value->follow == follow) entry.value->follow = nullptr;
    }

    void AudioSystem::stopChannel(ChannelId channel)
    {
        if (Channel *c = findChannel(channel)) destroyChannel(channel, c);
    }

    void AudioSystem::setChannelPaused(ChannelId channel, bool paused)
    {
        Channel *c = findChannel(channel);
        if (!c || !c->player) return;
        if (paused) ma_sound_stop(c->player);
        else ma_sound_start(c->player);
    }

    void AudioSystem::setChannelPitchHz(ChannelId channel, float hz)
    {
        if (Channel *c = findChannel(channel)) if (c->player) applyPitchHz(c->player, hz, c->nativeRate);
    }

    void AudioSystem::setChannelVolume(ChannelId channel, float volume)
    {
        if (Channel *c = findChannel(channel)) if (c->player) ma_sound_set_volume(c->player, clampf(volume, 0.0f, 4.0f));
    }

    void AudioSystem::setChannelPan(ChannelId channel, float pan)
    {
        if (Channel *c = findChannel(channel)) if (c->player) ma_sound_set_pan(c->player, clampf(pan, -1.0f, 1.0f));
    }

    bool AudioSystem::isChannelPlaying(ChannelId channel) const
    {
        Channel *c = findChannel(channel);
        return c && c->player && ma_sound_is_playing(c->player) == MA_TRUE && ma_sound_at_end(c->player) == MA_FALSE;
    }

    void AudioSystem::setListenerPosition(const Vector &pos)
    {
        if (mReady) ma_engine_listener_set_position(mEngine, 0, pos.x, pos.y, pos.z);
    }

    void AudioSystem::setListenerVelocity(const Vector &vel)
    {
        if (mReady) ma_engine_listener_set_velocity(mEngine, 0, vel.x, vel.y, vel.z);
    }

    void AudioSystem::setListenerOrientation(const Vector &forward, const Vector &up)
    {
        if (!mReady) return;
        ma_engine_listener_set_direction(mEngine, 0, forward.x, forward.y, forward.z);
        ma_engine_listener_set_world_up(mEngine, 0, up.x, up.y, up.z);
    }

    void AudioSystem::setListenerParams(float rolloff, float dopplerScale, float distanceScale)
    {
        mRolloff = rolloff;
        mDopplerScale = dopplerScale;
        mDistanceScale = distanceScale > 0.0f ? distanceScale : 1.0f;
    }
}
