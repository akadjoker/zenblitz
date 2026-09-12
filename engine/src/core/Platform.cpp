/*
** Platform.cpp — see Platform.h.
*/
#include "engine/Platform.h"
#include "engine/Log.h"

#include <SDL2/SDL.h>

namespace engine
{
    Platform::Platform() {}
    Platform::~Platform() { close(); }

    bool Platform::open(int width, int height, const char *title, bool fullscreen)
    {
        if (mOpen) close();

#if defined(__EMSCRIPTEN__)
        const gpu::Backend backend = gpu::Backend::OpenGLES;
#else
        const gpu::Backend backend = gpu::Backend::OpenGL;
#endif
        mDevice.setBackend(backend);
        if (!mDevice.create(title ? title : "zenblitz3d", width, height,
                            0, /*resizable*/ true, fullscreen))
        {
            kx::Log::error("Platform: could not create the window");
            return false;
        }
        if (!mGraphics.create(mDevice))
        {
            kx::Log::error("Platform: could not create the GPU device");
            mDevice.destroy();
            return false;
        }
        if (!mBatch.init(mGraphics.device(), mGraphics.shaderDialect()))
        {
            kx::Log::error("Platform: could not initialise the 2D/3D batch renderer");
            mGraphics.destroy();
            mDevice.destroy();
            return false;
        }
        mBatch.resize(width, height);
        mOpen = true;
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
        mGraphics.beginFrame(mClearR, mClearG, mClearB, 1.0f);
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
            if (b) mMouseState[b] = (e.type == SDL_MOUSEBUTTONDOWN);
            break;
        }
        default:
            break;
        }
    }

    void Platform::pumpEvents()
    {
        mDevice.update(); /* timing + window-resize bookkeeping; no longer polls itself */
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            mDevice.handleEvent(e); /* close request, close key, minimize/restore/resize */
            handleEvent(&e);        /* DIK keyboard, mouse */
        }
        if (!mDevice.isOpen()) mOpen = false;
    }

    void Platform::endFrame()
    {
        mGraphics.endFrame();
        mDevice.flip();
        pumpEvents();
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
    }

    bool Platform::mouseDown(int button) const
    {
        return button > 0 && button < 4 && mMouseState[button];
    }

    double Platform::milliSecs() const
    {
        return (double)mDevice.getTicks();
    }
}
