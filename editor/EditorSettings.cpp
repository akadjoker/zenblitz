#include "EditorSettings.hpp"

#include <SDL.h>

namespace zed
{

namespace
{
constexpr const char* kSection = "editor";
constexpr const char* kRecentKeyPrefix = "recent"; // recent0, recent1, ...
constexpr const char* kLastFolderKey = "last_open_folder";
constexpr const char* kOpenDocKeyPrefix = "open"; // open0, open1, ...
constexpr const char* kWindowSection = "window";
constexpr const char* kMonitorKey = "monitor";
constexpr const char* kXKey = "x";
constexpr const char* kYKey = "y";
constexpr const char* kWidthKey = "width";
constexpr const char* kHeightKey = "height";
constexpr const char* kThemeKey = "theme";

const char* themeName(EditorTheme theme)
{
    switch (theme)
    {
    case EditorTheme::Light: return "light";
    case EditorTheme::Dark:  return "dark";
    default:                 return "classic";
    }
}

EditorTheme themeFromName(const ig::String& name)
{
    if (name == "light") return EditorTheme::Light;
    if (name == "dark") return EditorTheme::Dark;
    return EditorTheme::Classic;
}
} // anonymous namespace

ig::String EditorSettings::configPath()
{
    // SDL_GetPrefPath creates the directory tree if it doesn't exist yet and
    // returns it with a trailing separator already appended.
    char* prefPath = SDL_GetPrefPath("zenblitz", "editor");
    ig::String path;
    if (prefPath)
    {
        path = prefPath;
        SDL_free(prefPath);
    }
    // SDL's separator is native ('\' on Windows); every other path in this
    // editor is kept '/'-separated (see NativeFileDialogProvider_win32.cpp),
    // so normalize here too.
    for (size_t i = 0; i < path.size(); ++i)
        if (path[i] == '\\') path[i] = '/';
    path.append("editor.ini");
    return path;
}

void EditorSettings::load()
{
    recentFiles_.clear();
    lastOpenFolder_.clear();
    openDocuments_.clear();
    windowGeometry_ = WindowGeometry();
    theme_ = EditorTheme::Classic;

    ct::Ini ini;
    if (!ini.load(configPath()))
        return; // no settings file yet - nothing to restore

    lastOpenFolder_ = ini.get(kSection, kLastFolderKey, "");
    theme_ = themeFromName(ini.get(kSection, kThemeKey, "classic"));

    for (size_t i = 0; i < kMaxRecentFiles; ++i)
    {
        ig::String key(kRecentKeyPrefix);
        key.append(ig::String::number(static_cast<int>(i)));
        ig::String value = ini.get(kSection, key.c_str(), "");
        if (value.empty())
            break; // entries are written contiguously from 0, so this ends the list
        recentFiles_.push_back(value);
    }

    for (size_t i = 0; i < kMaxOpenDocuments; ++i)
    {
        ig::String key(kOpenDocKeyPrefix);
        key.append(ig::String::number(static_cast<int>(i)));
        ig::String value = ini.get(kSection, key.c_str(), "");
        if (value.empty())
            break; // same contiguous-from-0 convention as recentN
        openDocuments_.push_back(value);
    }

    // has(..., kWidthKey) as the "a real geometry was saved before" marker -
    // width is always written together with the rest, so its absence means
    // this .ini predates window geometry or was never saved with a window.
    if (ini.has(kWindowSection, kWidthKey))
    {
        windowGeometry_.monitorIndex = static_cast<int>(ini.get_int(kWindowSection, kMonitorKey, 0));
        windowGeometry_.x = static_cast<int>(ini.get_int(kWindowSection, kXKey, 0));
        windowGeometry_.y = static_cast<int>(ini.get_int(kWindowSection, kYKey, 0));
        windowGeometry_.width = static_cast<int>(ini.get_int(kWindowSection, kWidthKey, 1280));
        windowGeometry_.height = static_cast<int>(ini.get_int(kWindowSection, kHeightKey, 800));
        windowGeometry_.valid = true;
    }
}

bool EditorSettings::save() const
{
    ct::Ini ini;
    ini.set(kSection, kLastFolderKey, lastOpenFolder_);
    ini.set(kSection, kThemeKey, themeName(theme_));
    for (size_t i = 0; i < recentFiles_.size(); ++i)
    {
        ig::String key(kRecentKeyPrefix);
        key.append(ig::String::number(static_cast<int>(i)));
        ini.set(kSection, key.c_str(), recentFiles_[i]);
    }
    for (size_t i = 0; i < openDocuments_.size(); ++i)
    {
        ig::String key(kOpenDocKeyPrefix);
        key.append(ig::String::number(static_cast<int>(i)));
        ini.set(kSection, key.c_str(), openDocuments_[i]);
    }
    if (windowGeometry_.valid)
    {
        ini.set(kWindowSection, kMonitorKey, windowGeometry_.monitorIndex);
        ini.set(kWindowSection, kXKey, windowGeometry_.x);
        ini.set(kWindowSection, kYKey, windowGeometry_.y);
        ini.set(kWindowSection, kWidthKey, windowGeometry_.width);
        ini.set(kWindowSection, kHeightKey, windowGeometry_.height);
    }
    return ini.save(configPath());
}

void EditorSettings::noteFileOpened(const ig::String& path)
{
    if (path.empty())
        return;

    for (size_t i = 0; i < recentFiles_.size(); ++i)
    {
        if (recentFiles_[i] == path)
        {
            recentFiles_.erase(recentFiles_.begin() + static_cast<ptrdiff_t>(i));
            break;
        }
    }
    recentFiles_.insert(recentFiles_.begin(), path);
    if (recentFiles_.size() > kMaxRecentFiles)
        recentFiles_.resize(kMaxRecentFiles);
}

void EditorSettings::removeRecentFile(const ig::String& path)
{
    for (size_t i = 0; i < recentFiles_.size(); ++i)
    {
        if (recentFiles_[i] == path)
        {
            recentFiles_.erase(recentFiles_.begin() + static_cast<ptrdiff_t>(i));
            return;
        }
    }
}

} // namespace zed
