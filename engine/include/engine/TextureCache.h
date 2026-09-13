#ifndef ENGINE_TEXTURECACHE_H
#define ENGINE_TEXTURECACHE_H

#include "engine/Texture.h"
#include <ct/string.hpp>
#include <ct/vector.hpp>

namespace engine
{
    // Owns every texture a mesh loader pulls in, so no loader has to own
    // one itself.
    //
    // Ported from Blitz3D's cachedtexture.cpp, which kept exactly this:
    // a process-wide set of texture reps (its rep_set), looked up by the
    // key a load is identified by, handed out with a reference taken, and
    // deleted when the last reference went. This is that set, over this
    // engine's own refcounted Texture.
    //
    // It exists because the four mesh loaders had each grown a private
    // `g_textures` global with its own idea of when to let go - one
    // released per load, the other three never released at all, and
    // LoadMesh on a .x/.fbx/.gltf leaked every texture in the file. A
    // loader now asks for a texture and forgets about it; the cache
    // dedups repeats (castle1.x names one .jpg across ~20 materials) and
    // releases the lot on shutdown.
    //
    // Not thread-safe, and not meant to be: loading runs on the thread
    // that called LoadMesh, same as the original.
    class TextureCache
    {
    public:
        // Texture for `file`, loaded on first ask and shared afterwards.
        // The cache keeps ownership - do not release the result. Returns
        // null if the file cannot be loaded, and does not cache that, so
        // a later ask retries (a script may well create the file first).
        static Texture *acquire(const ct::String &file, int flags);

        // Same, for an image already in memory (a glTF buffer_view or a
        // base64 data: URI, which have no path to reopen). `name` is the
        // cache key as well as the texture's label, so an embedded image
        // with no name of its own is never shared with another.
        static Texture *acquireFromMemory(const ct::String &name, const unsigned char *data, unsigned size,
                                          int flags);

        // Drops every cached texture. Call once nothing draws with them
        // any more - the runtime does it from shutdown_graphics(), after
        // the entities holding their GPU handles have been freed.
        static void clear();

    private:
        struct Entry
        {
            ct::String key;
            Texture *texture;
        };
        static ct::Vector<Entry> &entries();
        static Texture *find(const ct::String &key);
    };
}

#endif
