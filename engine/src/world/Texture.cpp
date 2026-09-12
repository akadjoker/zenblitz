#include "engine/Texture.h"
#include <SDL2/SDL_rwops.h>

namespace engine
{
    namespace
    {
        std::string g_texturePath;

        bool fileExists(const std::string &path)
        {
            SDL_RWops *f = SDL_RWFromFile(path.c_str(), "rb");
            if (!f) return false;
            SDL_RWclose(f);
            return true;
        }
    }

    void setTexturePath(const std::string &dir)
    {
        g_texturePath = dir;
        if (!g_texturePath.empty())
        {
            char last = g_texturePath[g_texturePath.size() - 1];
            if (last != '/' && last != '\\') g_texturePath += '/';
        }
    }

    std::string resolveTexturePath(const std::string &file)
    {
        if (!g_texturePath.empty())
        {
            std::string candidate = g_texturePath + file;
            if (fileExists(candidate)) return candidate;
        }
        return file;
    }

    Texture *Texture::load(const std::string &file, int flags)
    {
        Texture *t = new Texture();
        t->mName = file;
        t->mFlags = flags;
        zengl::Pixmap p;
        if (!p.load(resolveTexturePath(file).c_str()))
        {
            delete t;
            return nullptr;
        }
        t->mTransparent = p.has_alpha();
        t->mFrames.push_back(std::move(p));
        return t;
    }

    Texture *Texture::loadAnim(const std::string &file, int flags, int w, int h, int first, int count)
    {
        if (count < 1 || first < 0 || w <= 0 || h <= 0) return nullptr;

        zengl::Pixmap sheet;
        if (!sheet.load(resolveTexturePath(file).c_str())) return nullptr;

        int fpr = sheet.width / w;
        int fpp = fpr > 0 ? (sheet.height / h) * fpr : 0;
        if (fpr <= 0 || first + count > fpp) return nullptr;

        Texture *t = new Texture();
        t->mName = file;
        t->mFlags = flags;
        t->mTransparent = sheet.has_alpha();
        for (int k = 0; k < count; ++k)
        {
            int idx = first + k;
            int srcX = (idx % fpr) * w, srcY = (idx / fpr) * h;
            zengl::IntRect rect{srcX, srcY, w, h};
            zengl::Pixmap frame(sheet, rect);
            t->mFrames.push_back(std::move(frame));
        }
        return t;
    }

    Texture::~Texture()
    {
        if (mGpuOwner)
            for (size_t k = 0; k < mGpuFrames.size(); ++k)
                if (mGpuFrames[k].valid()) mGpuOwner->destroy(mGpuFrames[k]);
    }

    bool Texture::ensureUploaded(gpu::Device &dev, int frame)
    {
        if (mFrames.empty()) return false;
        if (frame < 0) frame = 0;
        if (frame >= (int)mFrames.size()) frame = (int)mFrames.size() - 1;

        mGpuFrames.resize(mFrames.size());
        if (mGpuFrames[frame].valid()) return true;

        zengl::Pixmap &src = mFrames[frame];
        zengl::Pixmap *rgba = src.components == 4 ? nullptr : src.convert_to_rgba();
        zengl::Pixmap *use = rgba ? rgba : &src;

        gpu::TextureDesc desc;
        desc.width = (std::uint32_t)use->width;
        desc.height = (std::uint32_t)use->height;
        desc.format = gpu::Format::RGBA8;
        desc.initialData = {use->pixels, (size_t)use->get_size()};
        desc.debugName = "bb.texture";
        // CopySource lets tools/tests read texture contents back via
        // Device::readTexture; harmless for a normal sampled texture.
        desc.usage = gpu::TextureUsageSampled | gpu::TextureUsageCopySource;

        mGpuFrames[frame] = dev.createTexture(desc);
        mGpuOwner = &dev;

        delete rgba;
        return mGpuFrames[frame].valid();
    }

    gpu::TextureHandle Texture::handle(int frame) const
    {
        if (frame < 0 || frame >= (int)mGpuFrames.size()) return gpu::TextureHandle();
        return mGpuFrames[frame];
    }
}
