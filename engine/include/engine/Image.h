/*
** Image.h — Blitz3D's bbImage: one or more frames, each a CPU pixel buffer
** plus its GPU texture.
*/
#ifndef ENGINE_IMAGE_H
#define ENGINE_IMAGE_H

#include "engine/Pixmap.h"
#include "gpu/GPU.h"
#include <ct/vector.hpp>
#include <cstdint>
#include <utility>

namespace engine
{
    struct ImageFrame
    {
        Pixmap pixels;
        gpu::TextureHandle texture;
        bool dirty = false;
        /* gxCanvas's cm_mask: one bit per pixel, set where the pixel is
           not the mask colour. Built lazily by collision tests and thrown
           away whenever the pixels or the mask colour change. */
        ct::Vector<std::uint32_t> cmMask;
        int cmPitch = 0;
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

        // mipCounter, when given, is called with (width, height) and its
        // result requested as the GPU texture's mip count - used by
        // Texture for LoadTexture's TexMipmap flag; Image's own callers
        // (plain 2D images) never need more than the default single level.
        bool ensureUploaded(gpu::Device &gpu, int i, std::uint32_t (*mipCounter)(std::uint32_t, std::uint32_t) = nullptr);

        /* Blitz3D's mask colour: every pixel matching it is transparent.
           0xff000000 (an impossible ARGB for an opaque pixel) means the
           image has no mask, as gxCanvas started out. */
        std::uint32_t mask() const { return mMask; }
        void setMask(std::uint32_t argb);

        /* gxCanvas::collide / rect_collide. x,y are the top-left corners
           the script drew at; handles are applied inside. */
        bool collide(int x, int y, int frame, const Image &other, int otherX, int otherY,
                     int otherFrame, bool solid) const;
        bool rectCollide(int x, int y, int frame, int rx, int ry, int rw, int rh, bool solid) const;

        void addFrame(Pixmap &&pixels)
        {
            ImageFrame f;
            f.pixels = std::move(pixels);
            f.dirty = true;
            mFrames.push_back(std::move(f));
        }

        void destroy();

    private:
        ct::Vector<ImageFrame> mFrames;
        int mHandleX = 0, mHandleY = 0;
        gpu::Device *mGpuOwner = nullptr;
        std::uint32_t mMask = 0xff000000u;

        void updateBitMask(int frame) const;

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
