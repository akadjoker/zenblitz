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
        // Real seconds since the previous Flip - UpdateWorld's anim_speed#
        // is a multiplier on this, not a raw elapsed-time value the
        // script supplies itself (see UpdateWorld's own doc comment).
        float getDeltaTime() const { return mDevice.getDeltaTime(); }

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
        ShaderDialect shaderDialect() const { return mGraphics.shaderDialect(); }
        BatchRenderer &batch() { return mBatch; }

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
        /* gxDevice's key queue: every press is pushed as the ASCII value
           GetKey returns, and read back one at a time until empty (0).
           Blitz3D's own queue held 32 entries. */
        int popKey();
        /* the same queue for the mouse, holding button numbers */
        int popMouseButton();

        int mouseX() const { return mMouseX; }
        int mouseY() const { return mMouseY; }
        bool mouseDown(int button) const;
        /* gxDevice counted presses between reads and cleared the count
           on read, which is what MouseHit returns. */
        int mouseHit(int button);
        void flushMouseHits();
        /* SDL reports wheel notches; DirectInput reported 120 per notch
           and Blitz3D divided by 120, so the notch count is what MouseZ
           means. Cumulative since the window opened, as gxDevice's axis. */
        int mouseZ() const { return mMouseZ; }
        /* false when there is no window to warp inside, so callers do
           not record a move that never happened */
        bool moveMouse(int x, int y);
        void showPointer(bool visible);

        double milliSecs() const;
        void pumpEvents();

    private:
        Device mDevice;
        Graphics mGraphics;
        BatchRenderer mBatch;
        bool mOpen = false;
        float mClearR = 0, mClearG = 0, mClearB = 0;

        bool mKeyState[kMaxDIK] = {};
        bool mKeyHitState[kMaxDIK] = {};
        int mMouseX = 0, mMouseY = 0;
        bool mMouseState[4] = {};
        int mMouseHitState[4] = {};
        int mMouseZ = 0;

        static const int kQueueSize = 32;
        int mKeyQueue[kQueueSize] = {};
        int mKeyPut = 0, mKeyGet = 0;
        int mButtonQueue[kQueueSize] = {};
        int mButtonPut = 0, mButtonGet = 0;
        void pushKey(int ascii);
        void pushButton(int button);

        void handleEvent(const void *sdlEvent);
    };
}

#endif
