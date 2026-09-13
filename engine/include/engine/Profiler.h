#pragma once

#include <ct/string.hpp>

#include <cstdint>

namespace engine
{

  struct ProfileSample
  {
    static constexpr std::uint32_t HistorySize = 120;

    ct::String name;
    float milliseconds = 0.0f;
    float display = 0.0f;
    float average = 0.0f;
    float maximum = 0.0f;
    float history[HistorySize] = {};
    std::uint32_t historyCount = 0;
    std::uint32_t historyCursor = 0;
  };

  class Profiler
  {
  public:
    static constexpr std::uint32_t MaxSamples = 32;
    static constexpr std::uint32_t MaxDepth = 16;
    static constexpr double RefreshSeconds = 0.25;

    static Profiler &getSingleton();

    void beginFrame();
    void endFrame();
    bool begin(const char *name);
    void end();
    void addSample(const char *name, float milliseconds);

    const ProfileSample *samples() const;
    std::uint32_t sampleCount() const;
    float frameMilliseconds() const;

  private:
    struct ActiveScope
    {
      std::uint32_t sample = 0;
      std::uint64_t counter = 0;
    };

    std::uint32_t findOrCreate(const char *name);

    ProfileSample mSamples[MaxSamples];
    ActiveScope mStack[MaxDepth];
    std::uint64_t mFrameStart = 0;
    std::uint64_t mFrequency = 1;
    std::uint64_t mLastRefresh = 0;
    float mFrameMilliseconds = 0.0f;
    float mDisplayFrameMilliseconds = 0.0f;
    std::uint32_t mSampleCount = 0;
    std::uint32_t mDepth = 0;
    bool mOverflowWarned = false;
    bool mDepthOverflowWarned = false;
  };

  class ProfileScope
  {
  public:
    explicit ProfileScope(const char *name);
    ~ProfileScope();

    ProfileScope(const ProfileScope &) = delete;
    ProfileScope &operator=(const ProfileScope &) = delete;

  private:
    bool mActive = false;
  };

} // namespace engine

#define ENGINE_PROFILE_JOIN_IMPL(a, b) a##b
#define ENGINE_PROFILE_JOIN(a, b) ENGINE_PROFILE_JOIN_IMPL(a, b)
#define ENGINE_PROFILE_SCOPE(name) ::engine::ProfileScope ENGINE_PROFILE_JOIN(engineProfileScope, __LINE__)(name)
