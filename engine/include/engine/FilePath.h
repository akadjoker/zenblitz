#ifndef ENGINE_FILEPATH_H
#define ENGINE_FILEPATH_H

#include <string>

namespace engine
{
    // Blitz3D shipped for Windows, where NTFS/FAT treat "House.3DS" and
    // "house.3ds" as the same file; real .bb source (including Blitz3D's
    // own tutorials) relies on that and gets the casing wrong on a
    // case-sensitive filesystem. If `path` opens as given, it's returned
    // unchanged; otherwise this walks it component by component, listing
    // each directory and matching case-insensitively, so assets written
    // for Windows still load on Linux/Android/macOS. Returns the original
    // path if no case-insensitive match exists either, so the caller's
    // own "file not found" handling still fires.
    std::string resolveCaseInsensitive(const std::string &path);
}

#endif
