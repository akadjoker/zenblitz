#include "engine/Texture.h"
#include <cctype>
#include "engine/FilePath.h"
#include <SDL2/SDL_rwops.h>
#include <algorithm>
#include <utility>

namespace engine
{
    namespace
    {
        ct::String g_texturePath;

        // Full mip chain length for a width x height base level - same
        // floor(log2(max(w,h)))+1 the GL backend uses to cap mipCount
        // (gpu::maximumMipCount), so a full chain here never gets rejected
        // by createTexture as "too many levels".
        std::uint32_t fullMipCount(std::uint32_t w, std::uint32_t h)
        {
            std::uint32_t largest = std::max(w, h);
            std::uint32_t count = 0;
            while (largest) { ++count; largest >>= 1; }
            return count ? count : 1;
        }

        bool fileExists(const ct::String &path)
        {
            SDL_RWops *f = SDL_RWFromFile(path.c_str(), "rb");
            if (!f) return false;
            SDL_RWclose(f);
            return true;
        }
    }

    void setTexturePath(const ct::String &dir)
    {
        g_texturePath = dir;
        if (!g_texturePath.empty())
        {
            char last = g_texturePath[g_texturePath.size() - 1];
            if (last != '/' && last != '\\') g_texturePath += '/';
        }
    }

    ct::String resolveTexturePath(const ct::String &file)
    {
        if (!g_texturePath.empty())
        {
            ct::String candidate = resolveCaseInsensitive(g_texturePath + file);
            if (fileExists(candidate)) return candidate;
        }
        return resolveCaseInsensitive(file);
    }

    namespace
    {
        // texture.cpp's `filters`: TextureFilter registers a substring
        // and a set of flags, and every texture whose filename contains
        // that substring gets those flags OR'd in. Matching is
        // case-insensitive on both sides, as in the original's tolower().
        struct TexFilter { ct::String match; int flags; };
        ct::Vector<TexFilter> g_filters;

        ct::String toLower(const ct::String &s)
        {
            ct::String out = s;
            for (size_t k = 0; k < out.size(); ++k)
                out[k] = (char)std::tolower((unsigned char)out[k]);
            return out;
        }

        int filterFile(const ct::String &name, int flags)
        {
            const ct::String lower = toLower(name);
            for (size_t k = 0; k < g_filters.size(); ++k)
                if (lower.find(g_filters[k].match) != ct::String::npos)
                    flags |= g_filters[k].flags;
            return flags;
        }
    }

    void Texture::addFilter(const ct::String &match, int flags)
    {
        TexFilter f;
        f.match = toLower(match);
        f.flags = flags;
        g_filters.push_back(f);
    }

    void Texture::clearFilters() { g_filters.clear(); }

    Texture *Texture::load(const ct::String &file, int flags)
    {
        flags = filterFile(file, flags);
        Texture *t = new Texture();
        t->mName = file;
        t->mFlags = flags;
        Pixmap p;
        if (!p.load(resolveTexturePath(file).c_str()))
        {
            delete t;
            return nullptr;
        }
        t->mTransparent = p.has_alpha();
        t->mImage.addFrame(std::move(p));
        return t;
    }

    Texture *Texture::loadFromMemory(const ct::String &name, const unsigned char *data, unsigned size,
                                     int flags)
    {
        Texture *t = new Texture();
        t->mName = name;
        t->mFlags = flags;
        Pixmap p;
        if (!data || !p.load_from_memory(data, size))
        {
            delete t;
            return nullptr;
        }
        t->mTransparent = p.has_alpha();
        t->mImage.addFrame(std::move(p));
        return t;
    }

    Texture *Texture::loadAnim(const ct::String &file, int flags, int w, int h, int first, int count)
    {
        if (count < 1 || first < 0 || w <= 0 || h <= 0) return nullptr;

        Pixmap sheet;
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
            IntRect rect{srcX, srcY, w, h};
            Pixmap frame(sheet, rect);
            t->mImage.addFrame(std::move(frame));
        }
        return t;
    }

    Texture *Texture::create(int w, int h, int flags, int count)
    {
        if (w <= 0 || h <= 0 || count < 1) return nullptr;

        Texture *t = new Texture();
        t->mName = "";
        t->mFlags = flags;
        // TexAlpha with no TexMask: a blank alpha-capable canvas starts
        // fully transparent, same as the original's CreateTexture - a
        // script Cls's it to whatever it wants before ever showing it.
        // Otherwise (the common case) it starts opaque black, exactly
        // like a fresh BackBuffer before the first Cls.
        const bool alpha = (flags & TexAlpha) != 0;
        t->mTransparent = alpha;
        for (int k = 0; k < count; ++k)
        {
            Pixmap p(w, h, 4);
            p.fill(0, 0, 0, alpha ? 0 : 255);
            t->mImage.addFrame(std::move(p));
        }
        return t;
    }

    Texture::~Texture() {}

    ImageFrame *Texture::canvas(int frame)
    {
        return frame >= 0 && frame < mImage.frameCount() ? &mImage.frame(frame) : nullptr;
    }

    bool Texture::ensureUploaded(gpu::Device &dev, int frame)
    {
        // TexMipmap (LoadTexture's flags bit 8, opt-in like the original):
        // request a full mip chain so the GL backend's createTexture uses
        // GL_LINEAR_MIPMAP_LINEAR and generates it from initialData right
        // away (GLDeviceCommon.inl) - mipCount left at its default 1
        // otherwise gets a single-level GL_LINEAR sample, which is what
        // was aliasing/pixelating minified textures like a tiled terrain.
        //
        // Image::ensureUploaded already skips the reupload when the frame
        // isn't dirty and already has a GPU texture - this is what makes
        // painting into canvas(frame) via SetBuffer TextureBuffer(tex) and
        // then re-showing the texture in 3D pick the new pixels up.
        return mImage.ensureUploaded(dev, frame, (mFlags & TexMipmap) ? fullMipCount : nullptr);
    }

    gpu::TextureHandle Texture::handle(int frame) const
    {
        // Same out-of-range handling as ensureUploaded: Image::frame()
        // clamps to a valid frame rather than returning null, so a script
        // passing a bad frame index gets frame 0 (or the last one) instead
        // of silently untextured geometry - this matches the old
        // clamp-then-index behaviour LoaderX-era callers already rely on.
        return mImage.frameCount() ? mImage.frame(frame).texture : gpu::TextureHandle();
    }
}
