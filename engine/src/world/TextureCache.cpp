#include "engine/TextureCache.h"

namespace engine
{
    namespace
    {
        // Key for a cached entry. The original keyed on file+flags+w+h+
        // first+count; this engine's Texture::load takes only file and
        // flags (loadAnim, which needs the rest, is a script-facing
        // command and does not come through here), so those two are the
        // whole key. Flags are part of it because the same image loaded
        // with TexMipmap and without is two different GPU textures.
        ct::String cacheKey(const ct::String &file, int flags)
        {
            char suffix[24];
            int n = 0;
            unsigned v = (unsigned)flags;
            suffix[n++] = '|';
            if (!v) suffix[n++] = '0';
            else
            {
                char digits[16];
                int d = 0;
                while (v) { digits[d++] = (char)('0' + (v % 10)); v /= 10; }
                while (d) suffix[n++] = digits[--d];
            }
            suffix[n] = '\0';
            return file + suffix;
        }
    }

    ct::Vector<TextureCache::Entry> &TextureCache::entries()
    {
        // Function-local so the cache is alive for any loader that runs
        // before main() would have initialised a file-scope vector.
        static ct::Vector<Entry> instance;
        return instance;
    }

    Texture *TextureCache::find(const ct::String &key)
    {
        ct::Vector<Entry> &all = entries();
        for (size_t k = 0; k < all.size(); ++k)
            if (all[k].key == key) return all[k].texture;
        return nullptr;
    }

    Texture *TextureCache::acquire(const ct::String &file, int flags)
    {
        if (file.empty()) return nullptr;

        const ct::String key = cacheKey(file, flags);
        if (Texture *hit = find(key)) return hit;

        Texture *tex = Texture::load(file, flags);
        // A failed load is not cached: the miss is cheap next to a decode,
        // and caching null would stop a retry from ever working.
        if (!tex) return nullptr;

        const Entry entry = {key, tex};
        entries().push_back(entry);
        return tex;
    }

    Texture *TextureCache::acquireFromMemory(const ct::String &name, const unsigned char *data, unsigned size,
                                             int flags)
    {
        if (!data || !size) return nullptr;

        // An unnamed embedded image has no identity to share on - two of
        // them in one file are unrelated - so it skips the cache lookup
        // and is only registered for the cleanup that follows.
        const bool shareable = !name.empty();
        const ct::String key = cacheKey(name, flags);
        if (shareable)
            if (Texture *hit = find(key)) return hit;

        Texture *tex = Texture::loadFromMemory(name, data, size, flags);
        if (!tex) return nullptr;

        const Entry entry = {shareable ? key : ct::String(), tex};
        entries().push_back(entry);
        return tex;
    }

    void TextureCache::clear()
    {
        ct::Vector<Entry> &all = entries();
        for (size_t k = 0; k < all.size(); ++k) Texture::release(all[k].texture);
        all.clear();
    }
}
