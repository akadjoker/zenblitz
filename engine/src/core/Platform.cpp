/*
** Platform.cpp — see Platform.h.
*/
#include "engine/Platform.h"
#include "engine/Profiler.h"
#include "engine/Log.h"

#include <SDL2/SDL.h>

namespace engine
{
    Platform::Platform() {}
    Platform::~Platform() { close(); }

    bool Platform::open(int width, int height, const char *title, bool fullscreen, bool visible)
    {
        if (mOpen) close();

#if defined(__EMSCRIPTEN__)
        const gpu::Backend backend = gpu::Backend::OpenGLES;
#else
        const gpu::Backend backend = gpu::Backend::OpenGL;
#endif
        mDevice.setBackend(backend);
        if (!mDevice.create(title ? title : "zenblitz3d", width, height,
                            0, /*resizable*/ true, fullscreen, visible))
        {
            Log::error("Platform: could not create the window");
            return false;
        }
        if (!mGraphics.create(mDevice))
        {
            Log::error("Platform: could not create the GPU device");
            mDevice.destroy();
            return false;
        }
        if (!mBatch.init(mGraphics.device(), mGraphics.shaderDialect()))
        {
            Log::error("Platform: could not initialise the 2D/3D batch renderer");
            mGraphics.destroy();
            mDevice.destroy();
            return false;
        }
        /* GetKey returns typed characters, which only arrive as
           SDL_TEXTINPUT while text input is enabled */
        SDL_StartTextInput();
        mGraphics.setScreenSize((std::uint32_t)width, (std::uint32_t)height);
        mBatch.resize(width, height);
        mOpen = true;
        // first frame starts now; every later one starts at the end of
        // the previous Flip (see endFrame)
        Profiler::getSingleton().beginFrame();
        return true;
    }

    void Platform::close()
    {
        if (!mOpen) return;
        mBatch.shutdown();
        mGraphics.destroy();
        mDevice.destroy();
        mOpen = false;
    }

    void Platform::setClearColor(float r, float g, float b)
    {
        mClearR = r; mClearG = g; mClearB = b;
    }

    void Platform::beginFrame()
    {
        /* Cls: (re)open the screen pass with a clear. The batch's
           projection tracks the logical screen resolution, not the
           window's - resizing the window only rescales the final blit. */
        if (mGraphics.inFrame()) mGraphics.endFrame();
        mGraphics.beginFrame(mClearR, mClearG, mClearB, 1.0f);
        int gw = (int)mGraphics.screenWidth(), gh = (int)mGraphics.screenHeight();
        int bw, bh;
        mBatch.getWindowSize(bw, bh);
        if (gw > 0 && gh > 0 && (gw != bw || gh != bh)) mBatch.resize(gw, gh);
        mBatch.update();
    }

    void Platform::handleEvent(const void *sdlEventPtr)
    {
        const SDL_Event &e = *static_cast<const SDL_Event *>(sdlEventPtr);
        switch (e.type)
        {
        case SDL_KEYDOWN:
        {
            int dik = scancode_to_dik(e.key.keysym.scancode);
            if (dik) { mKeyState[dik] = true; mKeyHitState[dik] = true; }
            /* gxInput::toAscii mapped the navigation keys to control
               codes of its own and everything else through the keyboard
               layout; printable characters arrive as SDL_TEXTINPUT
               instead, which already has the layout and shift applied. */
            int ascii = 0;
            switch (e.key.keysym.sym)
            {
            case SDLK_HOME: ascii = 1; break;
            case SDLK_END: ascii = 2; break;
            case SDLK_INSERT: ascii = 3; break;
            case SDLK_DELETE: ascii = 4; break;
            case SDLK_PAGEUP: ascii = 5; break;
            case SDLK_PAGEDOWN: ascii = 6; break;
            case SDLK_UP: ascii = 28; break;
            case SDLK_DOWN: ascii = 29; break;
            case SDLK_RIGHT: ascii = 30; break;
            case SDLK_LEFT: ascii = 31; break;
            case SDLK_BACKSPACE: ascii = 8; break;
            case SDLK_TAB: ascii = 9; break;
            case SDLK_RETURN: case SDLK_KP_ENTER: ascii = 13; break;
            case SDLK_ESCAPE: ascii = 27; break;
            default: break;
            }
            if (ascii) pushKey(ascii);
            break;
        }
        case SDL_TEXTINPUT:
        {
            /* one queue entry per byte keeps this to the 7-bit ASCII
               Blitz3D's GetKey returned; anything above is skipped
               rather than delivered as a broken half-character */
            for (const char *c = e.text.text; *c; ++c)
            {
                const unsigned char ch = (unsigned char)*c;
                if (ch >= 32 && ch < 128) pushKey(ch);
            }
            break;
        }
        case SDL_KEYUP:
        {
            int dik = scancode_to_dik(e.key.keysym.scancode);
            if (dik) mKeyState[dik] = false;
            break;
        }
        case SDL_MOUSEMOTION:
            mMouseX = e.motion.x;
            mMouseY = e.motion.y;
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        {
            /* Blitz numbering: 1=left, 2=right, 3=middle */
            int b = e.button.button == SDL_BUTTON_LEFT ? 1
                  : e.button.button == SDL_BUTTON_RIGHT ? 2
                  : e.button.button == SDL_BUTTON_MIDDLE ? 3 : 0;
            if (b)
            {
                const bool down = e.type == SDL_MOUSEBUTTONDOWN;
                /* count the press only on the transition, as
                   gxDevice::setDownState did before raising downEvent */
                if (down && !mMouseState[b]) { ++mMouseHitState[b]; pushButton(b); }
                mMouseState[b] = down;
            }
            break;
        }
        case SDL_MOUSEWHEEL:
        {
            int dz = e.wheel.y;
            if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) dz = -dz;
            mMouseZ += dz;
            break;
        }
        default:
            break;
        }
    }

    void Platform::pumpEvents()
    {
        mDevice.update();
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            mDevice.handleEvent(e);
            handleEvent(&e);
        }
        if (!mDevice.isOpen()) mOpen = false;
    }

    void Platform::flushCanvasPass()
    {
        ENGINE_PROFILE_SCOPE("Canvas/Flush");
        if (mGraphics.inFrame()) mGraphics.endFrame();
        mBatch.flip();
        if (mGraphics.reopenScreenPass())
        {
            mBatch.draw();
            mGraphics.endFrame();
        }
    }

    bool Platform::beginWorldRender()
    {
        return mGraphics.reopenScreenPass();
    }

    void Platform::endWorldRender()
    {
        mGraphics.endFrame();
    }

    void Platform::endFrame()
    {
        /* Flip: close the Cls pass, upload, reopen with Load, draw, close. */
        flushCanvasPass();

        /* the blit quad fills the window, not the logical screen size -
           switch the batch's projection to the window for just this draw. */
        mBatch.resize((int)mGraphics.width(), (int)mGraphics.height());
        mBatch.loadIdentity();
        mBatch.setColor((unsigned char)255, (unsigned char)255, (unsigned char)255);
        /* screenTexture is written with GL's bottom-left origin; the ortho
           projection is top-left, so sample it flipped vertically here. */
        mBatch.drawTexture(mGraphics.screenTexture(), 0.0f, 0.0f,
                           (float)mGraphics.width(), (float)mGraphics.height(),
                           0.0f, 1.0f, 1.0f, -1.0f);
        mBatch.flip();

        if (mGraphics.beginSurfaceBlit())
        {
            mBatch.draw();
            mGraphics.endSurfaceBlit();
        }

        {
            ENGINE_PROFILE_SCOPE("Screen/Flip");
            mDevice.flip();
        }
        pumpEvents();
        // a Blitz frame runs Flip to Flip, so this is the boundary:
        // close the sample set for the frame just presented and open
        // the next one
        Profiler::getSingleton().endFrame();
        Profiler::getSingleton().beginFrame();
    }

    bool Platform::keyDown(int dik) const
    {
        return dik > 0 && dik < kMaxDIK && mKeyState[dik];
    }

    bool Platform::keyHit(int dik)
    {
        if (dik <= 0 || dik >= kMaxDIK) return false;
        bool hit = mKeyHitState[dik];
        mKeyHitState[dik] = false;
        return hit;
    }

    void Platform::flushKeyHits()
    {
        for (bool &h : mKeyHitState) h = false;
        /* gxDevice::flush cleared the hit counts and the queue together */
        mKeyPut = mKeyGet = 0;
    }

    void Platform::pushKey(int ascii)
    {
        if (mKeyPut - mKeyGet >= kQueueSize) return;
        mKeyQueue[mKeyPut++ & (kQueueSize - 1)] = ascii;
    }

    int Platform::popKey()
    {
        return mKeyGet < mKeyPut ? mKeyQueue[mKeyGet++ & (kQueueSize - 1)] : 0;
    }

    void Platform::pushButton(int button)
    {
        if (mButtonPut - mButtonGet >= kQueueSize) return;
        mButtonQueue[mButtonPut++ & (kQueueSize - 1)] = button;
    }

    int Platform::popMouseButton()
    {
        return mButtonGet < mButtonPut ? mButtonQueue[mButtonGet++ & (kQueueSize - 1)] : 0;
    }

    bool Platform::mouseDown(int button) const
    {
        return button > 0 && button < 4 && mMouseState[button];
    }

    int Platform::mouseHit(int button)
    {
        if (button <= 0 || button >= 4) return 0;
        const int hits = mMouseHitState[button];
        mMouseHitState[button] = 0;
        return hits;
    }

    void Platform::flushMouseHits()
    {
        for (int &h : mMouseHitState) h = 0;
        mButtonPut = mButtonGet = 0;
    }

    bool Platform::moveMouse(int x, int y)
    {
        if (!mOpen) return false;
        SDL_WarpMouseInWindow(mDevice.getNativeWindow(), x, y);
        /* SDL_WarpMouseInWindow posts a motion event, but MouseX must
           already read the new position when this call returns */
        mMouseX = x;
        mMouseY = y;
        return true;
    }

    void Platform::showPointer(bool visible)
    {
        SDL_ShowCursor(visible ? SDL_ENABLE : SDL_DISABLE);
    }

    double Platform::milliSecs() const
    {
        return (double)mDevice.getTicks();
    }

    unsigned Platform::readScreenPixel(int x, int y)
    {
        if (x < 0 || y < 0 || x >= (int)mGraphics.width() || y >= (int)mGraphics.height()) return 0;
        // Nothing has been presented yet (a script that reads the screen
        // or GrabImage's from it before its first Flip), so there is no
        // captured frame to sample - reading through the null handle just
        // fills the GPU error queue with one entry per pixel.
        if (!mGraphics.screenTexture().valid()) return 0;
        gpu::TextureRegion region;
        region.x = (std::uint32_t)x;
        // screenTexture is written with GL's bottom-left origin (see the
        // comment in endFrame); callers pass top-left coordinates like
        // everywhere else, so flip here rather than surprise every caller.
        region.y = (std::uint32_t)(mGraphics.height() - 1 - y);
        region.width = 1;
        region.height = 1;
        unsigned char pixel[4] = {0, 0, 0, 0};
        gpu::MutableDataView data{pixel, sizeof(pixel)};
        if (!mGraphics.device().readTexture(mGraphics.screenTexture(), region, data)) return 0;
        return ((unsigned)pixel[3] << 24) | ((unsigned)pixel[0] << 16) | ((unsigned)pixel[1] << 8) | pixel[2];
    }
}
