#include "Profiler.h"

#include "engine/Log.h"

#include <SDL2/SDL.h>

namespace kx
{

  Profiler &Profiler::getSingleton()
  {
    static Profiler profiler;
    return profiler;
  }

  void Profiler::beginFrame()
  {
    if (mFrequency == 1)
      mFrequency = SDL_GetPerformanceFrequency();
    for (std::uint32_t i = 0; i < mSampleCount; ++i)
      mSamples[i].milliseconds = 0.0f;
    mDepth = 0;
    mFrameStart = SDL_GetPerformanceCounter();
  }

  void Profiler::endFrame()
  {
    const std::uint64_t now = SDL_GetPerformanceCounter();
    mFrameMilliseconds = static_cast<float>((now - mFrameStart) * 1000.0 / mFrequency);
    const std::uint32_t frameSample = findOrCreate("Frame");
    if (frameSample < MaxSamples)
      mSamples[frameSample].milliseconds = mFrameMilliseconds;

    const bool refresh = (now - mLastRefresh) > static_cast<std::uint64_t>(RefreshSeconds * mFrequency);
    if (refresh)
    {
      mLastRefresh = now;
      mDisplayFrameMilliseconds = mFrameMilliseconds;
    }

    for (std::uint32_t i = 0; i < mSampleCount; ++i)
    {
      ProfileSample &sample = mSamples[i];
      if (refresh)
        sample.display = sample.milliseconds;
      sample.history[sample.historyCursor] = sample.milliseconds;
      sample.historyCursor = (sample.historyCursor + 1) % ProfileSample::HistorySize;
      if (sample.historyCount < ProfileSample::HistorySize)
        ++sample.historyCount;

      float total = 0.0f;
      sample.maximum = 0.0f;
      for (std::uint32_t j = 0; j < sample.historyCount; ++j)
      {
        total += sample.history[j];
        if (sample.history[j] > sample.maximum)
          sample.maximum = sample.history[j];
      }
      sample.average = sample.historyCount ? total / sample.historyCount : 0.0f;
    }
  }

  std::uint32_t Profiler::findOrCreate(const char *name)
  {
    for (std::uint32_t i = 0; i < mSampleCount; ++i)
    {
      if (mSamples[i].name == name)
        return i;
    }
    if (mSampleCount >= MaxSamples)
    {
      if (!mOverflowWarned)
      {
        Log::warning("Profiler: MaxSamples (%u) exceeded; '%s' and further scopes will not "
                    "be tracked",
                    MaxSamples, name);
        mOverflowWarned = true;
      }
      return MaxSamples;
    }

    mSamples[mSampleCount].name = name;
    return mSampleCount++;
  }

  bool Profiler::begin(const char *name)
  {
    if (!name)
      return false;
    if (mDepth >= MaxDepth)
    {
      if (!mDepthOverflowWarned)
      {
        Log::warning("Profiler: MaxDepth (%u) exceeded at '%s'; deeper scopes will not be "
                    "timed",
                    MaxDepth, name);
        mDepthOverflowWarned = true;
      }
      return false;
    }
    if (mFrequency == 1)
      mFrequency = SDL_GetPerformanceFrequency();
    const std::uint32_t sample = findOrCreate(name);
    if (sample >= MaxSamples)
      return false;
    mStack[mDepth++] = {sample, SDL_GetPerformanceCounter()};
    return true;
  }

  void Profiler::end()
  {
    if (mDepth == 0)
      return;
    const std::uint64_t now = SDL_GetPerformanceCounter();
    const ActiveScope scope = mStack[--mDepth];
    mSamples[scope.sample].milliseconds +=
        static_cast<float>((now - scope.counter) * 1000.0 / mFrequency);
  }

  void Profiler::addSample(const char *name, float milliseconds)
  {
    if (!name)
      return;
    const std::uint32_t sample = findOrCreate(name);
    if (sample < MaxSamples)
      mSamples[sample].milliseconds += milliseconds;
  }

  const ProfileSample *Profiler::samples() const { return mSamples; }

  std::uint32_t Profiler::sampleCount() const { return mSampleCount; }

  float Profiler::frameMilliseconds() const { return mDisplayFrameMilliseconds; }

  ProfileScope::ProfileScope(const char *name) : mActive(Profiler::getSingleton().begin(name)) {}

  ProfileScope::~ProfileScope()
  {
    if (mActive)
      Profiler::getSingleton().end();
  }

} // namespace kx
