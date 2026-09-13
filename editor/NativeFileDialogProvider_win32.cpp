// Windows implementation of NativeFileDialogProvider - same interface as
// NativeFileDialogProvider_posix.cpp (see NativeFileDialogProvider.hpp),
// backed by native Win32 calls (FindFirstFileW/FindNextFileW) instead of
// dirent.h/sys/stat.h. Only this file and its posix twin differ; CMake
// picks whichever one matches the target platform (see editor/CMakeLists.txt),
// so Editor.hpp/main.cpp never see a #ifdef for this.
//
// ig::String (ct::String) is treated as UTF-8 everywhere else in this editor
// (paths typed by the user, paths read from files) - Win32's *W functions
// take UTF-16, so every path crosses that boundary through
// MultiByteToWideChar/WideCharToMultiByte at the edges of this file only.
// No std:: containers here - this project uses ct:: (ct::String, ct::Vector)
// throughout, same as every other file in this editor; a NUL-terminated
// ct::Vector<wchar_t> stands in for std::wstring.

#include "NativeFileDialogProvider.hpp"

#include <ct/vector.hpp>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>   // SHGetKnownFolderPath
#include <knownfolders.h>

namespace zed
{

namespace
{

using WideBuffer = ct::Vector<wchar_t>; // always NUL-terminated, like a C string

WideBuffer utf8ToWide(const ig::String& s)
{
    WideBuffer wide;
    if (s.empty())
    {
        wide.push_back(L'\0');
        return wide;
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (needed <= 0)
    {
        wide.push_back(L'\0');
        return wide;
    }
    wide.resize(static_cast<size_t>(needed) + 1u, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), wide.data(), needed);
    wide[static_cast<size_t>(needed)] = L'\0';
    return wide;
}

ig::String wideToUtf8(const wchar_t* s, int len = -1)
{
    if (!s || len == 0) return ig::String();
    const int needed = WideCharToMultiByte(CP_UTF8, 0, s, len, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return ig::String();
    ig::String out;
    out.resize(static_cast<size_t>(needed));
    WideCharToMultiByte(CP_UTF8, 0, s, len, &out[0], needed, nullptr, nullptr);
    // len == -1 means s was NUL-terminated and WideCharToMultiByte counted
    // the NUL into needed/out - trim it back off so out matches ig::String's
    // own convention (no embedded/trailing NUL) like the posix provider.
    if (len < 0 && !out.empty() && out.back() == '\0')
        out.resize(out.size() - 1);
    return out;
}

// Editor code (Editor.cpp/FileIO.cpp/the dialog UI itself) builds and
// compares paths with '/' throughout, matching the posix provider - so
// paths are kept '/'-separated everywhere outside this file too, and only
// converted to '\' right at the Win32 API call.
ig::String toForwardSlashes(ig::String path)
{
    for (size_t i = 0; i < path.size(); ++i)
        if (path[i] == '\\') path[i] = '/';
    return path;
}

WideBuffer toBackslashes(const ig::String& path)
{
    WideBuffer wide = utf8ToWide(path);
    for (size_t i = 0; i + 1u < wide.size(); ++i) // skip the trailing NUL
        if (wide[i] == L'/') wide[i] = L'\\';
    return wide;
}

ig::String joinPath(const ig::String& dir, const ig::String& name)
{
    if (dir.empty()) return name;
    ig::String path = dir;
    if (path.back() != '/') path.push_back('/');
    path.append(name);
    return path;
}

} // anonymous namespace

bool NativeFileDialogProvider::listDirectory(ig::StringView path, ct::Vector<ig::FileDialogEntry>& entries,
                                             bool /*needMetadata*/)
{
    // FindFirstFileW/FindNextFileW already return size and last-write-time
    // as part of WIN32_FIND_DATAW - there's no extra per-entry syscall to
    // skip here the way NativeFileDialogProvider_posix.cpp's stat() is, so
    // needMetadata has nothing to optimize on this platform.
    const ig::String nativePath(path.data(), path.size());
    WideBuffer pattern = toBackslashes(nativePath);
    // pattern is NUL-terminated (see WideBuffer); drop the NUL to append
    // a trailing backslash + wildcard, then re-terminate.
    if (!pattern.empty()) pattern.pop_back();
    if (pattern.empty() || pattern.back() != L'\\') pattern.push_back(L'\\');
    pattern.push_back(L'*');
    pattern.push_back(L'\0');

    WIN32_FIND_DATAW findData;
    HANDLE handle = FindFirstFileW(pattern.data(), &findData);
    if (handle == INVALID_HANDLE_VALUE) return false;

    do
    {
        const ig::String name = wideToUtf8(findData.cFileName);
        if (name == "." || name == "..") continue;

        ig::FileDialogEntry item;
        item.name = name;
        item.path = joinPath(nativePath, name);
        item.directory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        if (item.directory)
        {
            item.size = 0u;
            item.modifiedTime = 0u;
        }
        else
        {
            ULARGE_INTEGER size;
            size.LowPart = findData.nFileSizeLow;
            size.HighPart = findData.nFileSizeHigh;
            item.size = static_cast<uint64_t>(size.QuadPart);

            // FILETIME is 100ns ticks since 1601-01-01; convert to a Unix
            // epoch second count so callers (sorting by Modified) get the
            // same unit the posix provider's st_mtime already gives them.
            ULARGE_INTEGER ft;
            ft.LowPart = findData.ftLastWriteTime.dwLowDateTime;
            ft.HighPart = findData.ftLastWriteTime.dwHighDateTime;
            static const uint64_t kEpochDiff100ns = 116444736000000000ULL;
            item.modifiedTime = ft.QuadPart > kEpochDiff100ns
                ? (ft.QuadPart - kEpochDiff100ns) / 10000000ULL
                : 0u;
        }

        item.hidden = (findData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0
                    || (!name.empty() && name[0] == '.');
        entries.push_back(item);
    }
    while (FindNextFileW(handle, &findData) != 0);

    FindClose(handle);
    return true;
}

ig::String NativeFileDialogProvider::parentDirectory(ig::StringView path)
{
    ig::String nativePath = toForwardSlashes(ig::String(path.data(), path.size()));
    while (nativePath.size() > 1 && nativePath.back() == '/')
        nativePath.erase(nativePath.size() - 1, 1);

    const size_t slash = nativePath.find_last_of("/");
    if (slash == ig::String::npos)
        return nativePath; // e.g. "C:" - already a root, nothing above it

    // Keep a drive root's trailing slash ("C:/" not "C:") so the dialog
    // doesn't collapse it into a bare, unusable "C:" path.
    if (slash > 0 && nativePath[slash - 1] == ':')
        return nativePath.substr(0, slash + 1);
    if (slash == 0)
        return "/";
    return nativePath.substr(0, slash);
}

ig::String NativeFileDialogProvider::homeDirectory()
{
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &path)) && path)
    {
        ig::String result = toForwardSlashes(wideToUtf8(path));
        CoTaskMemFree(path);
        return result;
    }

    wchar_t buf[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH)
        return toForwardSlashes(wideToUtf8(buf, static_cast<int>(len)));

    return ig::String("C:/");
}

ig::String NativeFileDialogProvider::createDirectory(ig::StringView parent, ig::StringView name)
{
    ig::String parentPath(parent.data(), parent.size());
    ig::String leafName(name.data(), name.size());
    ig::String path = joinPath(parentPath, leafName);
    const WideBuffer wide = toBackslashes(path);
    return CreateDirectoryW(wide.data(), nullptr) ? path : ig::String();
}

ig::String NativeFileDialogProvider::executableDirectory()
{
    wchar_t buf[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return ig::String(".");

    ig::String path = toForwardSlashes(wideToUtf8(buf, static_cast<int>(len)));
    const size_t slash = path.find_last_of("/");
    if (slash == ig::String::npos) return ig::String(".");
    return path.substr(0, slash);
}

} // namespace zed
