#include "FileIO.hpp"

#include <SDL_rwops.h>

#include <ct/vector.hpp>

namespace zed
{

bool readFile(const char *path, ig::String &out)
{
    SDL_RWops *f = SDL_RWFromFile(path, "rb");
    if (!f) return false;

    out.clear();
    const Sint64 size = SDL_RWsize(f);
    bool ok = true;
    if (size > 0)
    {
        ct::Vector<char> buf;
        buf.resize(static_cast<size_t>(size));
        const size_t read = SDL_RWread(f, buf.data(), 1, static_cast<size_t>(size));
        out.append(buf.data(), read);
        ok = read == static_cast<size_t>(size);
    }
    else if (size < 0)
    {
        ok = false;
    }

    SDL_RWclose(f);
    return ok;
}

bool writeFile(const char *path, const ig::String &text)
{
    SDL_RWops *f = SDL_RWFromFile(path, "wb");
    if (!f) return false;

    bool ok = true;
    if (!text.empty())
        ok = SDL_RWwrite(f, text.data(), 1, text.size()) == text.size();

    return SDL_RWclose(f) == 0 && ok;
}

} // namespace zed
