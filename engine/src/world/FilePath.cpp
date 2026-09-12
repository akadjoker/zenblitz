// Case-insensitive path resolution for asset loading, so .bb source
// written for Windows (NTFS/FAT: case-insensitive) still finds its
// files on a case-sensitive filesystem (Linux/macOS/Android). Desktop-
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
        bool fileOpens(const std::string &path)
        {
            SDL_RWops *f = SDL_RWFromFile(path.c_str(), "rb");
            if (!f) return false;
            SDL_RWclose(f);
            return true;
        }

#ifdef ENGINE_HAS_DIR_LISTING
        bool ciEqual(const std::string &a, const std::string &b)
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
        std::string findEntryCI(const std::string &dir, const std::string &name)
        {
            const std::string listDir = dir.empty() ? "." : dir;
#ifdef _WIN32
            std::string pattern = listDir + "\\*";
            WIN32_FIND_DATAA fd;
            HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) return std::string();
            std::string found;
            do
            {
                if (ciEqual(fd.cFileName, name)) { found = fd.cFileName; break; }
            } while (FindNextFileA(h, &fd));
            FindClose(h);
            return found;
#else
            DIR *d = opendir(listDir.c_str());
            if (!d) return std::string();
            std::string found;
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

    std::string resolveCaseInsensitive(const std::string &path)
    {
        if (path.empty() || fileOpens(path)) return path;

#ifdef ENGINE_HAS_DIR_LISTING
        // Walk the path one component at a time, resolving each against
        // what's actually on disk so far - handles a whole path being
        // miscased (Textures/Wood.PNG vs textures/wood.png), not just
        // the filename.
        std::string resolved;
        size_t start = 0;
        bool anyMismatch = false;
        while (start <= path.size())
        {
            size_t slash = path.find_first_of("/\\", start);
            std::string comp = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            char sep = slash == std::string::npos ? '\0' : path[slash];

            if (!comp.empty())
            {
                std::string dirSoFar = resolved.empty() ? std::string() : resolved;
                std::string real = findEntryCI(dirSoFar, comp);
                if (real.empty())
                {
                    // no case-insensitive match either - give up and let
                    // the caller's own error handling report the failure
                    return path;
                }
                if (real != comp) anyMismatch = true;
                resolved += real;
            }
            if (slash == std::string::npos) break;
            resolved += sep;
            start = slash + 1;
        }

        if (anyMismatch && fileOpens(resolved)) return resolved;
        return path;
#else
        return path;
#endif
    }
}
