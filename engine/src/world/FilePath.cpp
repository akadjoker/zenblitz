// Case-insensitive path resolution for asset loading, so .bb source
// written for Windows still finds its files on a case-sensitive
// filesystem (Linux/macOS/Android). Windows is forgiving in two ways
// the sample set leans on: NTFS/FAT ignore case, and the OS accepts
// "\\" as a path separator - so .bb sources are full of
// "sounds\\shoot.wav" and "castle\\CASTLE1.X". Both are normalised
// here, the separator unconditionally (it is never a legal character
// in a path component we could be shadowing) and the case only when
// the literal path does not already open. Desktop-
// only: directory listing isn't meaningful against an Android APK's
// packaged assets (SDL_RWFromFile already reaches those through
// AAssetManager), so there resolveCaseInsensitive() is a no-op and the
// path must already be correct, same as it always had to be.
#include "engine/FilePath.h"
#include <SDL2/SDL_rwops.h>

#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#define ENGINE_HAS_DIR_LISTING 1
#endif

#ifdef ENGINE_HAS_DIR_LISTING
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif
#include <cctype>
#endif

namespace engine
{
    namespace
    {
        bool fileOpens(const ct::String &path)
        {
            SDL_RWops *f = SDL_RWFromFile(path.c_str(), "rb");
            if (!f) return false;
            SDL_RWclose(f);
            return true;
        }

#ifdef ENGINE_HAS_DIR_LISTING
        bool ciEqual(const ct::String &a, const ct::String &b)
        {
            if (a.size() != b.size()) return false;
            for (size_t k = 0; k < a.size(); ++k)
                if (std::tolower((unsigned char)a[k]) != std::tolower((unsigned char)b[k])) return false;
            return true;
        }

        // Case-insensitively finds `name` among the entries of `dir`
        // (both plain path strings, dir defaulting to "." for a bare
        // filename); returns the entry's real on-disk spelling, or empty
        // if the directory can't be listed or has no matching entry.
        ct::String findEntryCI(const ct::String &dir, const ct::String &name)
        {
            const ct::String listDir = dir.empty() ? "." : dir;
#ifdef _WIN32
            ct::String pattern = listDir + "\\*";
            WIN32_FIND_DATAA fd;
            HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) return ct::String();
            ct::String found;
            do
            {
                if (ciEqual(fd.cFileName, name)) { found = fd.cFileName; break; }
            } while (FindNextFileA(h, &fd));
            FindClose(h);
            return found;
#else
            DIR *d = opendir(listDir.c_str());
            if (!d) return ct::String();
            ct::String found;
            while (struct dirent *e = readdir(d))
            {
                if (ciEqual(e->d_name, name)) { found = e->d_name; break; }
            }
            closedir(d);
            return found;
#endif
        }
#endif
    }

    ct::String resolveCaseInsensitive(const ct::String &path)
    {
        if (path.empty() || fileOpens(path)) return path;

#ifdef ENGINE_HAS_DIR_LISTING
        // Walk the path one component at a time, resolving each against
        // what's actually on disk so far - handles a whole path being
        // miscased (Textures/Wood.PNG vs textures/wood.png), not just
        // the filename.
        ct::String resolved;
        size_t start = 0;
        while (start <= path.size())
        {
            size_t slash = path.find_first_of("/\\", start);
            ct::String comp = path.substr(start, slash == ct::String::npos ? ct::String::npos : slash - start);

            if (!comp.empty())
            {
                ct::String dirSoFar = resolved.empty() ? ct::String() : resolved;
                ct::String real = findEntryCI(dirSoFar, comp);
                if (real.empty())
                {
                    // no case-insensitive match either - give up and let
                    // the caller's own error handling report the failure
                    return path;
                }
                resolved += real;
            }
            if (slash == ct::String::npos) break;
            // Always '/': a rebuilt path has to be one this platform can
            // actually open, so a Windows-style "\\" in the source is
            // normalised rather than echoed straight back.
            resolved += '/';
            start = slash + 1;
        }

        // Compare the whole rebuilt path rather than tracking which
        // components were respelled: the separator rewrite is a change
        // too, and "sounds\\shoot.wav" has every component spelled
        // correctly while still needing it.
        if (resolved != path && fileOpens(resolved)) return resolved;
        return path;
#else
        return path;
#endif
    }
}
