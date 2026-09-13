#ifndef ENGINE_SOUND_H
#define ENGINE_SOUND_H

#include "engine/Geom.h"
#include <ct/hashmap.hpp>
#include <ct/string.hpp>

struct ma_engine;
struct ma_sound;

namespace engine
{
    using blitz::Vector;

    class Object;

    class AudioSystem
    {
    public:
        using SoundId = int;
        using ChannelId = int;

        static AudioSystem &get();

        bool init();
        void shutdown();
        bool ready() const { return mReady; }

        void update();

        SoundId loadSound(const char *path, bool is3D);
        void freeSound(SoundId sound);
        bool soundValid(SoundId sound) const { return findSound(sound) != nullptr; }

        void setSoundLoop(SoundId sound, bool loop);
        void setSoundPitchHz(SoundId sound, float hz);
        void setSoundVolume(SoundId sound, float volume);
        void setSoundPan(SoundId sound, float pan);

        ChannelId play(SoundId sound);
        ChannelId playFile(const char *path, bool loop);
        ChannelId emit(SoundId sound, Object *follow);
        void detachFollower(Object *follow);

        void stopChannel(ChannelId channel);
        void setChannelPaused(ChannelId channel, bool paused);
        void setChannelPitchHz(ChannelId channel, float hz);
        void setChannelVolume(ChannelId channel, float volume);
        void setChannelPan(ChannelId channel, float pan);
        bool isChannelPlaying(ChannelId channel) const;

        void setListenerPosition(const Vector &pos);
        void setListenerVelocity(const Vector &vel);
        void setListenerOrientation(const Vector &forward, const Vector &up);
        void setListenerParams(float rolloff, float dopplerScale, float distanceScale);

    private:
        struct Sound
        {
            ct::String path;
            bool is3D = false;
            bool loop = false;
            float pitchHz = 0.0f;
            float volume = 1.0f;
            float pan = 0.0f;
            unsigned nativeRate = 0;
        };

        struct Channel
        {
            ma_sound *player = nullptr;
            Object *follow = nullptr;
            bool loop = false;
            unsigned nativeRate = 0;
        };

        AudioSystem() {}
        ~AudioSystem();
        AudioSystem(const AudioSystem &) = delete;
        AudioSystem &operator=(const AudioSystem &) = delete;

        Sound *findSound(SoundId id) const;
        Channel *findChannel(ChannelId id) const;
        void destroyChannel(ChannelId id, Channel *c);
        ChannelId startPlayer(const ct::String &path, bool loop, float pitchHz, unsigned nativeRate,
                              float volume, float pan, bool is3D, bool streamed);

        ma_engine *mEngine = nullptr;
        ma_sound *mSfxGroup = nullptr;
        bool mReady = false;
        bool mInitFailed = false;

        ct::HashMap<SoundId, Sound *> mSounds;
        ct::HashMap<ChannelId, Channel *> mChannels;
        SoundId mNextSound = 1;
        ChannelId mNextChannel = 1;

        float mRolloff = 1.0f;
        float mDopplerScale = 1.0f;
        float mDistanceScale = 1.0f;
    };
}

#endif
