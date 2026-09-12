/*
** Image.h — Blitz3D's bbImage: one or more frames, each a CPU pixel buffer
** (zengl::Pixmap) plus its GPU texture. Frames are separate images, not
** UV rectangles into one atlas — that is how the original modelled
** LoadAnimImage too (bbruntime/bbgraphics.cpp: each frame is its own
** gxCanvas, cut from the sheet at load time), and DrawImage/MaskImage/
** ImagesCollide are simplest when every frame is independently addressable.
**
** Handle (the draw origin/pivot Blitz3D's HandleImage/MidHandle set) is
** shared by all frames of one Image, as in the original.
*/
#ifndef ENGINE_IMAGE_H
#define ENGINE_IMAGE_H

#include "engine/Pixmap.h"
#include "gpu/GPU.h"
#include <ct/vector.hpp>
#include <utility>

namespace engine
{
    struct ImageFrame
    {
        zengl::Pixmap pixels;        /* CPU copy: LockBuffer/ReadPixel/WritePixel, collision masks */
        gpu::TextureHandle texture;  /* GPU copy: what DrawImage actually draws */
        bool dirty = false;          /* pixels changed since the texture was last uploaded */
    };

    class Image
    {
    public:
        ~Image() { destroy(); }

        int width() const { return mFrames.empty() ? 0 : mFrames[0].pixels.width; }
        int height() const { return mFrames.empty() ? 0 : mFrames[0].pixels.height; }
        int frameCount() const { return (int)mFrames.size(); }

        ImageFrame &frame(int i) { return mFrames[(size_t)clampFrame(i)]; }
        const ImageFrame &frame(int i) const { return mFrames[(size_t)clampFrame(i)]; }

        int handleX() const { return mHandleX; }
        int handleY() const { return mHandleY; }
        void setHandle(int x, int y) { mHandleX = x; mHandleY = y; }
        void setMidHandle() { mHandleX = width() / 2; mHandleY = height() / 2; }

        /* Uploads (or re-uploads, if WritePixel/LockBuffer touched the
           frame) the GPU texture for frame i. Called lazily, right before a
           frame is drawn — an image that is only ever poked at with
           WritePixel and never drawn never pays for an upload. */
        bool ensureUploaded(gpu::Device &gpu, int i);

        void addFrame(zengl::Pixmap &&pixels)
        {
            ImageFrame f;
            f.pixels = std::move(pixels);
            f.dirty = true;
            mFrames.push_back(std::move(f));
        }

        void destroy(); /* releases every frame's GPU texture; safe to call more than once */

    private:
        ct::Vector<ImageFrame> mFrames;
        int mHandleX = 0, mHandleY = 0;
        gpu::Device *mGpuOwner = nullptr; /* whichever Device last created a texture, for destroy() */

        int clampFrame(int i) const
        {
            if (mFrames.empty()) return 0;
            if (i < 0) return 0;
            if (i >= (int)mFrames.size()) return (int)mFrames.size() - 1;
            return i;
        }
    };
}

#endif
