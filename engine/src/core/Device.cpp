#include "engine/Device.h"

#include "engine/Input.h"
#include "engine/Log.h"

#include <cmath>

extern "C" const char *__lsan_default_suppressions()
{
  return "leak:libSDL2\n"
         "leak:SDL_DBus\n";
}

namespace kx
{

  Device::Device()
      : mBackend(gpu::Backend::OpenGL), mWidth(0), mHeight(0), mRunning(false), mResized(false),
        mMinimized(false), mFullscreen(false), mRelativeMouseMode(false), mCurrent(0.0),
        mPrevious(0.0), mUpdate(0.0), mDraw(0.0), mFrame(0.0), mTarget(0.0), mReady(false),
        mCloseKey(SDLK_ESCAPE), mMonitor(0), mDebugContext(false), mSdlInitialized(false),
        mFpsHistory(), mFpsHistoryIndex(0), mFpsAverage(0.0f), mFpsLastSampleTime(0.0)
  {
  }

  Device::~Device() { destroy(); }

  bool Device::create(const ct::String &title, int width, int height, int monitor,
                      bool resizable, bool fullscreen, bool visible)
  {
#if defined(SDL_HINT_DBUS)
    SDL_SetHint(SDL_HINT_DBUS, "0");
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0)
    {
      Log::error("SDL_Init failed: %s", SDL_GetError());
      return false;
    }
    mSdlInitialized = true;

    mMonitor = monitor;

    int monitorCount = SDL_GetNumVideoDisplays();
    if (monitorCount < 1)
    {
      Log::error("SDL_GetNumVideoDisplays failed: %s", SDL_GetError());
      monitorCount = 1;
    }
    if (mMonitor < 0 || mMonitor >= monitorCount)
    {
      Log::warning("Device::create: monitor %d out of range (0..%d), using 0", mMonitor,
                   monitorCount - 1);
      mMonitor = 0;
    }

    gpu::SDLWindowDesc desc;
    desc.title = title.c_str();
    desc.x = SDL_WINDOWPOS_CENTERED_DISPLAY(mMonitor);
    desc.y = SDL_WINDOWPOS_CENTERED_DISPLAY(mMonitor);
    desc.width = width > 0 ? static_cast<std::uint32_t>(width) : 1;
    desc.height = height > 0 ? static_cast<std::uint32_t>(height) : 1;
    desc.flags = visible ? SDL_WINDOW_SHOWN : SDL_WINDOW_HIDDEN;
    if (resizable)
      desc.flags |= SDL_WINDOW_RESIZABLE;
    if (fullscreen)
      desc.flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    desc.backend = mBackend;
    desc.debugContext = mDebugContext;
    desc.vsync = true;

    if (!mWindow.create(desc))
    {
      Log::error("Device::create: %s", mWindow.lastError());
      destroy();
      return false;
    }
    mFullscreen = fullscreen;

    mWidth = width;
    mHeight = height;
    mRunning = true;

    Input::init();
    mCurrent = getTime();
    mPrevious = mCurrent;
    mReady = true;

    return true;
  }

  void Device::destroy()
  {
    if (!mWindow.valid() && !mRunning && !mSdlInitialized)
      return;

    mWindow.destroy();
    if (mSdlInitialized)
    {
      SDL_Quit();
      mSdlInitialized = false;
    }
    mRunning = false;
    mReady = false;
  }

  bool Device::isOpen() const { return mRunning; }

  bool Device::isMinimized() const { return mMinimized; }

  bool Device::isMaximized() const
  {
    if (!mWindow.valid())
      return false;
    return (SDL_GetWindowFlags(mWindow.handle()) & SDL_WINDOW_MAXIMIZED) != 0;
  }

  bool Device::hasFocus() const
  {
    if (!mWindow.valid())
      return false;
    return (SDL_GetWindowFlags(mWindow.handle()) & SDL_WINDOW_INPUT_FOCUS) != 0;
  }

  void Device::update()
  {
    if (!mReady)
      return;

    mCurrent = getTime();
    mUpdate = mCurrent - mPrevious;
    mPrevious = mCurrent;

    Input::update();

    SDL_Event e;
    while (SDL_PollEvent(&e))
    {
      if (e.type == SDL_QUIT)
        mRunning = false;
      if (e.type == SDL_KEYDOWN && e.key.keysym.sym == mCloseKey)
        mRunning = false;

      switch (e.type)
      {
      case SDL_KEYDOWN:
        Input::onKeyDown(e.key);
        break;
      case SDL_KEYUP:
        Input::onKeyUp(e.key);
        break;
      case SDL_MOUSEBUTTONDOWN:
        Input::onMouseDown(e.button);
        break;
      case SDL_MOUSEBUTTONUP:
        Input::onMouseUp(e.button);
        break;
      case SDL_MOUSEMOTION:
        Input::onMouseMove(e.motion);
        break;
      case SDL_MOUSEWHEEL:
        Input::onMouseWheel(e.wheel);
        break;
      case SDL_TEXTINPUT:
        Input::onTextInput(e.text);
        break;
      case SDL_FINGERDOWN:
        Input::onTouchDown(e.tfinger);
        break;
      case SDL_FINGERUP:
        Input::onTouchUp(e.tfinger);
        break;
      case SDL_FINGERMOTION:
        Input::onTouchMove(e.tfinger);
        break;
      default:
        break;
      }

      if (e.type == SDL_WINDOWEVENT)
      {
        switch (e.window.event)
        {
        case SDL_WINDOWEVENT_MINIMIZED:
          mMinimized = true;
          break;
        case SDL_WINDOWEVENT_RESTORED:
        case SDL_WINDOWEVENT_MAXIMIZED:
          mMinimized = false;
          break;
        case SDL_WINDOWEVENT_CLOSE:
          mRunning = false;
          break;
        case SDL_WINDOWEVENT_SIZE_CHANGED:
          if (e.window.data1 != mWidth || e.window.data2 != mHeight)
          {
            mWidth = e.window.data1;
            mHeight = e.window.data2;
            mResized = true;
          }
          break;
        default:
          break;
        }
      }
    }

    if (mWindow.valid())
    {
      int actualWidth = 0;
      int actualHeight = 0;
      SDL_GetWindowSize(mWindow.handle(), &actualWidth, &actualHeight);
      if (actualWidth > 0 && actualHeight > 0 &&
          (actualWidth != mWidth || actualHeight != mHeight))
      {
        mWidth = actualWidth;
        mHeight = actualHeight;
        mResized = true;
      }
    }
  }

  void Device::flip()
  {
    if (!mReady)
      return;

    mCurrent = getTime();
    mDraw = mCurrent - mPrevious;
    mPrevious = mCurrent;
    mFrame = mUpdate + mDraw;

    if (mTarget > 0.0 && mFrame < mTarget)
    {
      wait(static_cast<float>((mTarget - mFrame) * 1000.0));

      mCurrent = getTime();
      double waitTime = mCurrent - mPrevious;
      mPrevious = mCurrent;
      mFrame += waitTime;
    }

    const int kCaptureFrames = 30;
    const float kAverageTime = 0.5f;
    const float kStep = kAverageTime / kCaptureFrames;
    if (mFrame > 0.0 && (mCurrent - mFpsLastSampleTime) > kStep)
    {
      mFpsLastSampleTime = mCurrent;
      mFpsHistoryIndex = (mFpsHistoryIndex + 1) % kCaptureFrames;
      mFpsAverage -= mFpsHistory[mFpsHistoryIndex];
      mFpsHistory[mFpsHistoryIndex] = static_cast<float>(mFrame) / kCaptureFrames;
      mFpsAverage += mFpsHistory[mFpsHistoryIndex];
    }
  }

  void Device::setTargetFPS(int fps) { mTarget = (fps < 1) ? 0.0 : 1.0 / fps; }

  int Device::getFPS()
  {
    return mFpsAverage > 0.0f ? static_cast<int>(std::round(1.0f / mFpsAverage)) : 0;
  }

  double Device::getTime() const
  {
    return static_cast<double>(SDL_GetPerformanceCounter()) /
           static_cast<double>(SDL_GetPerformanceFrequency());
  }

  Uint32 Device::getTicks() const { return SDL_GetTicks(); }

  void Device::wait(float ms) const
  {
    if (ms > 0.0f)
      SDL_Delay(static_cast<Uint32>(ms));
  }

  int Device::getWidth() const { return mWidth; }

  int Device::getHeight() const { return mHeight; }

  void Device::getDrawableSize(int &width, int &height) const
  {
    std::uint32_t drawableWidth = 0;
    std::uint32_t drawableHeight = 0;
    mWindow.drawableSize(drawableWidth, drawableHeight);
    width = static_cast<int>(drawableWidth);
    height = static_cast<int>(drawableHeight);
  }

  void Device::setSize(int width, int height)
  {
    if (!mWindow.valid() || width <= 0 || height <= 0)
      return;
    SDL_SetWindowSize(mWindow.handle(), width, height);
    mWidth = width;
    mHeight = height;
    mResized = true;
  }

  void Device::getPosition(int &x, int &y) const
  {
    x = 0;
    y = 0;
    if (mWindow.valid())
      SDL_GetWindowPosition(mWindow.handle(), &x, &y);
  }

  void Device::setPosition(int x, int y)
  {
    if (mWindow.valid())
      SDL_SetWindowPosition(mWindow.handle(), x, y);
  }

  bool Device::consumeResized()
  {
    bool resized = mResized;
    mResized = false;
    return resized;
  }

  void Device::setTitle(const ct::String &title)
  {
    if (mWindow.valid())
      SDL_SetWindowTitle(mWindow.handle(), title.c_str());
  }

  void Device::setFullscreen(bool fullscreen)
  {
    if (!mWindow.valid())
      return;
    if (SDL_SetWindowFullscreen(mWindow.handle(), fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) == 0)
      mFullscreen = fullscreen;
    else
      Log::error("SDL_SetWindowFullscreen failed: %s", SDL_GetError());
  }

  void Device::minimize()
  {
    if (mWindow.valid())
      SDL_MinimizeWindow(mWindow.handle());
  }

  void Device::maximize()
  {
    if (mWindow.valid())
      SDL_MaximizeWindow(mWindow.handle());
  }

  void Device::restore()
  {
    if (mWindow.valid())
      SDL_RestoreWindow(mWindow.handle());
  }

  void Device::setVSync(bool enabled)
  {
    if (!mWindow.setVSync(enabled))
      Log::warning("Device::setVSync: %s", mWindow.lastError());
  }

  void Device::setRelativeMouseMode(bool enabled)
  {
    if (SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE) != 0)
      Log::error("SDL_SetRelativeMouseMode failed: %s", SDL_GetError());
    mRelativeMouseMode = enabled;
  }

  ct::String Device::getClipboardText() const
  {
    if (!SDL_HasClipboardText())
      return "";

    char *text = SDL_GetClipboardText();
    ct::String result = text ? text : "";
    SDL_free(text);
    return result;
  }

  void Device::setClipboardText(const ct::String &text) { SDL_SetClipboardText(text.c_str()); }

  void Device::setMonitor(int monitor)
  {
    mMonitor = monitor;
    if (!mWindow.valid())
      return;

    int monitorCount = SDL_GetNumVideoDisplays();
    if (monitor < 0 || monitor >= monitorCount)
      return;

    SDL_SetWindowPosition(mWindow.handle(), SDL_WINDOWPOS_CENTERED_DISPLAY(monitor),
                          SDL_WINDOWPOS_CENTERED_DISPLAY(monitor));
  }

  int Device::getMonitorCount() const { return SDL_GetNumVideoDisplays(); }

} // namespace kx
