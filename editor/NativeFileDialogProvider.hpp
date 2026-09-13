#pragma once

// FileDialogProvider backed directly by the host OS's filesystem API.
// Same class name/interface on every platform - CMake picks which .cpp to
// compile (NativeFileDialogProvider_posix.cpp on Linux/Mac via dirent.h and
// sys/stat.h, NativeFileDialogProvider_win32.cpp on Windows via
// FindFirstFileW/FindNextFileW), so Editor.hpp/main.cpp never need an
// #ifdef to pick a type.

#include <igui/FileDialog.hpp>

namespace zed
{

class NativeFileDialogProvider : public ig::FileDialogProvider
{
public:
    bool listDirectory(ig::StringView path, ct::Vector<ig::FileDialogEntry>& entries,
                       bool needMetadata = true) override;
    ig::String parentDirectory(ig::StringView path) override;
    ig::String homeDirectory() override;
    ig::String createDirectory(ig::StringView parent, ig::StringView name) override;

    // Directory the running executable lives in - Open's real fallback
    // (before ever falling back further to homeDirectory()), so a fresh
    // install with no settings yet opens next to the binary and its
    // sample/project files, not in the user's home. Not part of
    // ig::FileDialogProvider (that interface only calls homeDirectory()
    // itself) - Editor decides which one to use as FileDialogOptions::initialPath.
    // Linux: reads /proc/self/exe. Windows: GetModuleFileNameW(nullptr, ...).
    // Returns "." if the platform call fails for any reason.
    static ig::String executableDirectory();
};

} // namespace zed
