/*
** Image.h — Blitz3D's bbImage: one or more frames, each a CPU pixel buffer
** plus its GPU texture.
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
        zengl::Pixmap pixels;
        gpu::TextureHandle texture;
        bool dirty = false;
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

        bool ensureUploaded(gpu::Device &gpu, int i);

        void addFrame(zengl::Pixmap &&pixels)
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
