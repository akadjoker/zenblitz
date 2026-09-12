/*
** Platform.h — window, input and per-frame timing behind Graphics/Flip/
** KeyDown/KeyHit.
*/
#ifndef ENGINE_PLATFORM_H
#define ENGINE_PLATFORM_H

#include "engine/Device.h"
#include "engine/Graphics.h"
#include "engine/Batch.h"
#include "engine/BlitzKeys.h"

namespace engine
{
    class Platform
    {
    public:
        Platform();
        ~Platform();

        Platform(const Platform &) = delete;
        Platform &operator=(const Platform &) = delete;

        bool open(int width, int height, const char *title, bool fullscreen, bool visible = true);
        void close();
        bool isOpen() const { return mOpen; }

        int width() const { return mDevice.getWidth(); }
        int height() const { return mDevice.getHeight(); }

        void setTargetFPS(int fps) { mDevice.setTargetFPS(fps); }
        void setVSync(bool on) { mDevice.setVSync(on); }
        bool isVSync() const { return mDevice.isVSync(); }

        void setClearColor(float r, float g, float b);
        void beginFrame();
        void endFrame();

        /* RenderWorld: closes the Canvas 2D pass (flushing any pending
           Batch draws first), so 3D code can do its own GPU uploads with
           no pass open. beginWorldRender() reopens on the same texture
           with Load once 3D is done, so 2D commands after RenderWorld
           keep drawing over it. */
        void flushCanvasPass();
        bool beginWorldRender();
        void endWorldRender();

        gpu::Device &device() { return mGraphics.device(); }
        kx::ShaderDialect shaderDialect() const { return mGraphics.shaderDialect(); }
        kx::BatchRenderer &batch() { return mBatch; }

        /* ARGB, 0 outside the window. (x,y) is top-left like every other
           screen coordinate the engine uses; internally flipped to sample
           the previous frame's finished screen texture (see
           Graphics::screenTexture), which GL writes bottom-up — the
           presentation surface itself cannot be read back on any backend
           this GPU library exposes. */
        unsigned readScreenPixel(int x, int y);

        bool keyDown(int dik) const;
        bool keyHit(int dik);
        void flushKeyHits();

        int mouseX() const { return mMouseX; }
        int mouseY() const { return mMouseY; }
        bool mouseDown(int button) const;

        double milliSecs() const;
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

        void handleEvent(const void *sdlEvent);
    };
}

#endif
