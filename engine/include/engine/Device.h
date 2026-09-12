#pragma once

#include "gpu/GPUSDLWindow.h"

#include <ct/string.hpp>

#include <SDL2/SDL.h>

namespace kx
{

  class Device
  {
  public:
    Device();
    ~Device();

    bool create(const ct::String &title, int width, int height, int monitor = 0,
                bool resizable = true, bool fullscreen = false, bool visible = true);
    void destroy();

    void setBackend(gpu::Backend backend) { mBackend = backend; }
    gpu::Backend getBackend() const { return mBackend; }
    void setDebugContext(bool enable = true) { mDebugContext = enable; }
    bool isDebugContext() const { return mDebugContext; }

    bool isOpen() const;
    void requestClose() { mRunning = false; }
    bool isMinimized() const;
    bool isMaximized() const;
    bool hasFocus() const;

    void update();
    void flip();

    SDL_Window *getNativeWindow() const { return mWindow.handle(); }
    gpu::SDLWindow &getSurface() { return mWindow; }
    const gpu::SDLWindow &getSurface() const { return mWindow; }

    float getDeltaTime() const { return (float)mFrame; }
    float getFrameTime() const { return (float)mFrame; }

    void setTargetFPS(int fps);
    int getFPS();

    double getTime() const;
    Uint32 getTicks() const;
    void wait(float ms) const;

    void setExitKey(Sint32 key) { mCloseKey = key; }
    Sint32 getExitKey() const { return mCloseKey; }

    int getWidth() const;
    int getHeight() const;
    void getDrawableSize(int &width, int &height) const;
    void setSize(int width, int height);

    void getPosition(int &x, int &y) const;
    void setPosition(int x, int y);

    bool consumeResized();

    void setTitle(const ct::String &title);

    void setFullscreen(bool fullscreen);
    bool isFullscreen() const { return mFullscreen; }

    void minimize();
    void maximize();
    void restore();

    void setVSync(bool enabled);
    bool isVSync() const { return mWindow.vsync(); }

    void setRelativeMouseMode(bool enabled);
    bool isRelativeMouseMode() const { return mRelativeMouseMode; }

    ct::String getClipboardText() const;
    void setClipboardText(const ct::String &text);

    void setMonitor(int monitor);
    int getMonitor() const { return mMonitor; }
    int getMonitorCount() const;

  private:
    gpu::SDLWindow mWindow;
    gpu::Backend mBackend;

    int mWidth, mHeight;
    bool mRunning;
    bool mResized;
    bool mMinimized;
    bool mFullscreen;
    bool mRelativeMouseMode;

    double mCurrent;
    double mPrevious;
    double mUpdate;
    double mDraw;
    double mFrame;
    double mTarget;
    bool mReady;
    Sint32 mCloseKey;
    int mMonitor;
    bool mDebugContext;
    bool mSdlInitialized;

    float mFpsHistory[30];
    int mFpsHistoryIndex;
    float mFpsAverage;
    double mFpsLastSampleTime;
  };

} // namespace kx
