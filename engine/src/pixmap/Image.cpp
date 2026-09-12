/*
** Image.cpp — see Image.h.
*/
#include "engine/Image.h"

namespace engine
{
    bool Image::ensureUploaded(gpu::Device &gpu, int i)
    {
        if (mFrames.empty()) return false;
        ImageFrame &f = mFrames[(size_t)clampFrame(i)];
        if (!f.dirty && f.texture.valid()) return true;

        /* the GPU texture path is always RGBA8 — convert_to_rgba() copies
           when the source is already 4-channel, so this costs nothing extra
           for the common case (LoadImage's stb_image decode is RGBA already). */
        zengl::Pixmap *rgba = f.pixels.components == 4 ? nullptr : f.pixels.convert_to_rgba();
        const zengl::Pixmap *src = rgba ? rgba : &f.pixels;

        gpu::TextureDesc desc;
        desc.width = (std::uint32_t)src->width;
        desc.height = (std::uint32_t)src->height;
        desc.format = gpu::Format::RGBA8;
        desc.initialData = {src->pixels, (size_t)src->get_size()};
        desc.debugName = "bb.image";

        if (f.texture.valid()) gpu.destroy(f.texture);
        f.texture = gpu.createTexture(desc);
        f.dirty = false;
        mGpuOwner = &gpu;

        delete rgba;
        return f.texture.valid();
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
