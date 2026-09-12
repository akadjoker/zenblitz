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
        mGraphics.setScreenSize((std::uint32_t)width, (std::uint32_t)height);
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
        mDevice.update();
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            mDevice.handleEvent(e);
            handleEvent(&e);
        }
        if (!mDevice.isOpen()) mOpen = false;
    }

    void Platform::endFrame()
    {
        /* Flip: close the Cls pass, upload, reopen with Load, draw, close. */
        if (mGraphics.inFrame()) mGraphics.endFrame();
        mBatch.flip();
        if (mGraphics.reopenScreenPass())
        {
            mBatch.draw();
            mGraphics.endFrame();
        }

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

    unsigned Platform::readScreenPixel(int x, int y)
    {
        if (x < 0 || y < 0 || x >= mGraphics.width() || y >= mGraphics.height()) return 0;
        gpu::TextureRegion region;
        region.x = (std::uint32_t)x;
        region.y = (std::uint32_t)y;
        region.width = 1;
        region.height = 1;
        unsigned char pixel[4] = {0, 0, 0, 0};
        gpu::MutableDataView data{pixel, sizeof(pixel)};
        if (!mGraphics.device().readTexture(mGraphics.screenTexture(), region, data)) return 0;
        return ((unsigned)pixel[3] << 24) | ((unsigned)pixel[0] << 16) | ((unsigned)pixel[1] << 8) | pixel[2];
    }
}
