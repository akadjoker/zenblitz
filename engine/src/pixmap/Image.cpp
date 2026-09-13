/*
** Image.cpp — see Image.h.
*/
#include "engine/Image.h"

namespace engine
{
    namespace
    {
        /* A pixel counts as "solid" for collision when it differs from the
           mask colour, exactly as gxCanvas::updateBitMask compared against
           mask_argb with the alpha byte dropped. */
        inline std::uint32_t rgbOf(const Pixmap &p, int x, int y)
        {
            const Color c = p.get_pixel_color((u32)x, (u32)y);
            return ((std::uint32_t)c.r() << 16) | ((std::uint32_t)c.g() << 8) | c.b();
        }
    }

    bool Image::ensureUploaded(gpu::Device &gpu, int i, std::uint32_t (*mipCounter)(std::uint32_t, std::uint32_t))
    {
        if (mFrames.empty()) return false;
        ImageFrame &f = mFrames[(size_t)clampFrame(i)];
        // A handle is only meaningful to the device that made it. After
        // EndGraphics + Graphics3D the old device is gone and every
        // handle it issued is stale, but still "valid" as a value - so
        // without this check the upload is skipped and the dead handle
        // gets bound, which the GPU layer reports as "invalid resource
        // handle" and draws nothing. Treat a different owner as dirty
        // and re-upload; do not destroy through the new device, the old
        // one already took its resources down with it.
        if (mGpuOwner != &gpu)
        {
            f.texture = gpu::TextureHandle();
            f.dirty = true;
        }
        if (!f.dirty && f.texture.valid()) return true;

        Pixmap *rgba = f.pixels.components == 4 ? nullptr : f.pixels.convert_to_rgba();
        const Pixmap *src = rgba ? rgba : &f.pixels;

        gpu::TextureDesc desc;
        desc.width = (std::uint32_t)src->width;
        desc.height = (std::uint32_t)src->height;
        desc.format = gpu::Format::RGBA8;
        desc.initialData = {src->pixels, (size_t)src->get_size()};
        desc.debugName = "bb.image";
        if (mipCounter) desc.mipCount = mipCounter(desc.width, desc.height);

        if (f.texture.valid()) gpu.destroy(f.texture);
        f.texture = gpu.createTexture(desc);
        f.dirty = false;
        mGpuOwner = &gpu;

        delete rgba;
        return f.texture.valid();
    }

    void Image::setMask(std::uint32_t argb)
    {
        if (mMask == argb) return;
        mMask = argb;
        /* the bit masks describe the old mask colour; drop them so the
           next collision test rebuilds against the new one */
        for (size_t i = 0; i < mFrames.size(); ++i)
        {
            mFrames[i].cmMask.clear();
            mFrames[i].cmPitch = 0;
        }
    }

    /* gxCanvas::updateBitMask, one word per 32 pixels of a row. */
    void Image::updateBitMask(int i) const
    {
        ImageFrame &f = const_cast<ImageFrame &>(mFrames[(size_t)clampFrame(i)]);
        const int w = f.pixels.width, h = f.pixels.height;
        if (w <= 0 || h <= 0) return;

        f.cmPitch = (w + 31) / 32;
        f.cmMask.clear();
        f.cmMask.resize((size_t)f.cmPitch * (size_t)h, 0u);

        const std::uint32_t maskRgb = mMask & 0xffffffu;
        const bool hasMask = (mMask & 0xff000000u) == 0;
        for (int y = 0; y < h; ++y)
        {
            std::uint32_t *row = f.cmMask.data() + (size_t)y * (size_t)f.cmPitch;
            for (int x = 0; x < w; ++x)
            {
                const bool solid = !hasMask || rgbOf(f.pixels, x, y) != maskRgb;
                if (solid) row[x / 32] |= 0x80000000u >> (x & 31);
            }
        }
    }

    /* gxCanvas::collide. The original walked both bit masks a word at a
       time with a shift; this keeps the same structure but tests the
       overlap bit by bit - an image collision runs once per pair per
       frame, and the shifted-word version is where its two out-of-range
       reads lived. */
    bool Image::collide(int x1, int y1, int f1, const Image &other, int x2, int y2, int f2,
                        bool solid) const
    {
        if (mFrames.empty() || other.mFrames.empty()) return false;
        const ImageFrame &a = mFrames[(size_t)clampFrame(f1)];
        const ImageFrame &b = other.mFrames[(size_t)other.clampFrame(f2)];

        x1 -= mHandleX; x2 -= other.mHandleX;
        if (x1 + a.pixels.width <= x2 || x1 >= x2 + b.pixels.width) return false;
        y1 -= mHandleY; y2 -= other.mHandleY;
        if (y1 + a.pixels.height <= y2 || y1 >= y2 + b.pixels.height) return false;

        if (solid) return true;

        if (a.cmMask.empty()) updateBitMask(f1);
        if (b.cmMask.empty()) other.updateBitMask(f2);
        if (a.cmMask.empty() || b.cmMask.empty()) return false;

        const int left = x1 > x2 ? x1 : x2;
        const int right = (x1 + a.pixels.width) < (x2 + b.pixels.width)
                              ? x1 + a.pixels.width : x2 + b.pixels.width;
        const int top = y1 > y2 ? y1 : y2;
        const int bottom = (y1 + a.pixels.height) < (y2 + b.pixels.height)
                               ? y1 + a.pixels.height : y2 + b.pixels.height;

        for (int y = top; y < bottom; ++y)
        {
            const std::uint32_t *rowA = a.cmMask.data() + (size_t)(y - y1) * (size_t)a.cmPitch;
            const std::uint32_t *rowB = b.cmMask.data() + (size_t)(y - y2) * (size_t)b.cmPitch;
            for (int x = left; x < right; ++x)
            {
                const int ax = x - x1, bx = x - x2;
                const bool sa = (rowA[ax / 32] & (0x80000000u >> (ax & 31))) != 0;
                if (!sa) continue;
                if (rowB[bx / 32] & (0x80000000u >> (bx & 31))) return true;
            }
        }
        return false;
    }

    /* gxCanvas::rect_collide. */
    bool Image::rectCollide(int x1, int y1, int f1, int rx, int ry, int rw, int rh,
                            bool solid) const
    {
        if (mFrames.empty()) return false;
        const ImageFrame &a = mFrames[(size_t)clampFrame(f1)];

        x1 -= mHandleX;
        if (x1 + a.pixels.width <= rx || x1 >= rx + rw) return false;
        y1 -= mHandleY;
        if (y1 + a.pixels.height <= ry || y1 >= ry + rh) return false;

        if (solid) return true;

        if (a.cmMask.empty()) updateBitMask(f1);
        if (a.cmMask.empty()) return false;

        const int left = x1 > rx ? x1 : rx;
        const int right = (x1 + a.pixels.width) < (rx + rw) ? x1 + a.pixels.width : rx + rw;
        const int top = y1 > ry ? y1 : ry;
        const int bottom = (y1 + a.pixels.height) < (ry + rh) ? y1 + a.pixels.height : ry + rh;

        for (int y = top; y < bottom; ++y)
        {
            const std::uint32_t *row = a.cmMask.data() + (size_t)(y - y1) * (size_t)a.cmPitch;
            for (int x = left; x < right; ++x)
            {
                const int ax = x - x1;
                if (row[ax / 32] & (0x80000000u >> (ax & 31))) return true;
            }
        }
        return false;
    }

    void Image::destroy()
    {
        for (size_t i = 0; i < mFrames.size(); ++i)
        {
            if (mGpuOwner && mFrames[i].texture.valid())
                mGpuOwner->destroy(mFrames[i].texture);
        }
        mFrames.clear();
    }
}
