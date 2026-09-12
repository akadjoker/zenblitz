/*
** Platform.h — the window, input and per-frame timing behind Graphics,
** Flip, KeyDown/KeyHit and friends. Wraps engine::Device/Graphics/BlitzKeys
** (copied from Kinetix3d) so runtime3d's commands never touch SDL or GPU
** directly — the same separation zen::Backend gives the console runtime.
**
** Deliberately thin: Platform owns the window and the raw draw surface
** (engine::Graphics::beginFrame/endFrame), not a rendering style. The
** fixed-function-DX7 Renderer (Marco 3) and the 2D Canvas (Marco 2) are
** built on top of this using their own shaders — swapping either later
** means writing a new class against Platform's Device&/gpu::Device&, not
** touching this file.
*/
#ifndef ENGINE_PLATFORM_H
#define ENGINE_PLATFORM_H

#include "engine/Device.h"
#include "engine/Graphics.h"
#include "engine/Batch.h"
#include "engine/BlitzKeys.h"

namespace engine
{
    /* Device/Graphics/Batch are kx:: (copied from Kinetix3d, unrenamed so
       they stay a plain diff against the source); everything zenblitz3d
       writes fresh lives in engine::. */
    class Platform
    {
    public:
        Platform();
        ~Platform();

        Platform(const Platform &) = delete;
        Platform &operator=(const Platform &) = delete;

        /* Graphics/Graphics3D. Re-creating the window (a second call) tears
           down the previous one first, as Blitz3D allowed. */
        bool open(int width, int height, const char *title, bool fullscreen);
        /* EndGraphics */
        void close();
        bool isOpen() const { return mOpen; }

        int width() const { return mDevice.getWidth(); }
        int height() const { return mDevice.getHeight(); }

        /* Caps how fast endFrame()'s flip() returns; 0 removes the cap.
           kx::Device::flip() only waits when this has been set. */
        void setTargetFPS(int fps) { mDevice.setTargetFPS(fps); }

        /* Cls/ClsColor: colour applied at the next beginFrame. */
        void setClearColor(float r, float g, float b);
        void beginFrame();
        /* Flip: presents the frame and pumps input. Suspends the VM for a
           frame (VM::request_suspend) instead of blocking, so a browser or
           Android host can hand control back to its own loop between
           frames — see zen::VM::wake_at() and cli/host_loop.h for the
           console side of the same mechanism. */
        void endFrame();

        gpu::Device &device() { return mGraphics.device(); }
        kx::BatchRenderer &batch() { return mBatch; }

        /* KeyDown/KeyHit: `key` is a DIK code (see BlitzKeys.h) — the same
           number a real Blitz3D program already uses. */
        bool keyDown(int dik) const;
        bool keyHit(int dik); /* consumes the hit, like the original */
        void flushKeyHits();  /* FlushKeys */

        int mouseX() const { return mMouseX; }
        int mouseY() const { return mMouseY; }
        bool mouseDown(int button) const; /* 1=left, 2=right, 3=middle, Blitz numbering */

        double milliSecs() const;

        /* one event pump without presenting — used by Delay/WaitTimer's
           resume loop so the window keeps responding while a program waits */
        void pumpEvents();

    private:
        kx::Device mDevice;
        kx::Graphics mGraphics;
        kx::BatchRenderer mBatch;
        bool mOpen = false;
        float mClearR = 0, mClearG = 0, mClearB = 0;

        bool mKeyState[kMaxDIK] = {};
        bool mKeyHitState[kMaxDIK] = {};
        int mMouseX = 0, mMouseY = 0;
        bool mMouseState[4] = {};

        void handleEvent(const void *sdlEvent); /* SDL_Event*, kept opaque here */
    };
}

#endif
