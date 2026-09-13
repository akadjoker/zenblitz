#include "NativeFileDialogProvider.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>

namespace zed
{

namespace
{
ig::String joinPath(const ig::String& dir, const ig::String& name)
{
    if (dir.empty()) return name;
    ig::String path = dir;
    if (path[path.size() - 1] != '/') path.push_back('/');
    path.append(name);
    return path;
}
} // anonymous namespace

bool NativeFileDialogProvider::listDirectory(ig::StringView path, ct::Vector<ig::FileDialogEntry>& entries,
                                             bool needMetadata)
{
    const ig::String nativePath(path.data(), path.size());
    DIR* dir = opendir(nativePath.c_str());
    if (!dir) return false;

    // readdir()'s d_type already tells us regular-file vs directory on every
    // filesystem we've measured here (ext4 and, notably, the fuseblk/NTFS-3G
    // mount this project actually lives on) - so stat() is only needed as a
    // DT_UNKNOWN fallback, or (when needMetadata is true - Details view, or
    // sorting by Size/Modified; see fileDialogNeedsMetadata in FileDialog.cpp)
    // to fill in size/modifiedTime for a *file* (a directory's size is
    // always reported as 0 anyway, so directories never need it regardless).
    // On the fuseblk mount each stat() has been measured at ~0.2-0.7ms vs
    // ~0.004ms on ext4 - noticeable on Back/Up/double-click navigation in
    // any folder with more than a couple dozen files, which needMetadata
    // now avoids paying entirely outside Details view.
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        const ig::String name = entry->d_name;
        if (name == "." || name == "..") continue;

        const ig::String fullPath = joinPath(nativePath, name);

        bool isDirectory = false;
        bool typeKnown = false;
#ifdef DT_UNKNOWN
        if (entry->d_type == DT_DIR) { isDirectory = true; typeKnown = true; }
        else if (entry->d_type == DT_REG) { isDirectory = false; typeKnown = true; }
#endif

        ig::FileDialogEntry item;
        item.name = name;
        item.path = fullPath;

        if (typeKnown && !(isDirectory == false && needMetadata))
        {
            // Either a directory (size always 0, never needs stat()), or a
            // file we don't need metadata for this call - skip stat().
            item.directory = isDirectory;
            item.size = 0u;
            item.modifiedTime = 0u;
        }
        else
        {
            struct stat info;
            if (stat(fullPath.c_str(), &info) != 0) continue;
            item.directory = S_ISDIR(info.st_mode);
            item.size = item.directory ? 0u : static_cast<uint64_t>(info.st_size);
            item.modifiedTime = static_cast<uint64_t>(info.st_mtime);
        }

        item.hidden = !name.empty() && name[0] == '.';
        entries.push_back(item);
    }
    closedir(dir);
    return true;
}

ig::String NativeFileDialogProvider::parentDirectory(ig::StringView path)
{
    ig::String nativePath(path.data(), path.size());
    while (nativePath.size() > 1 && nativePath[nativePath.size() - 1] == '/')
        nativePath.erase(nativePath.size() - 1, 1);
    size_t slash = nativePath.find_last_of("/");
    if (slash == ig::String::npos) return "/";
    if (slash == 0) return "/";
    return nativePath.substr(0, slash);
}

ig::String NativeFileDialogProvider::homeDirectory()
{
    const char* home = getenv("HOME");
    if (home && home[0] != '\0')
        return ig::String(home);
    char buf[4096];
    if (getcwd(buf, sizeof(buf))) return ig::String(buf);
    return ig::String("/");
}

ig::String NativeFileDialogProvider::createDirectory(ig::StringView parent, ig::StringView name)
{
    ig::String parentPath(parent.data(), parent.size());
    ig::String leafName(name.data(), name.size());
    ig::String path = joinPath(parentPath, leafName);
    return mkdir(path.c_str(), 0755) == 0 ? path : ig::String();
}

ig::String NativeFileDialogProvider::executableDirectory()
{
    char buf[4096];
    const ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return ig::String(".");
    buf[len] = '\0';

    ig::String path(buf);
    const size_t slash = path.find_last_of("/");
    if (slash == ig::String::npos) return ig::String(".");
    if (slash == 0) return ig::String("/");
    return path.substr(0, slash);
}

} // namespace zed
