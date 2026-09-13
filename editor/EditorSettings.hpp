#pragma once

// Persists small editor preferences across runs: the list of recently
// opened .bb files (most recent first, capped) and the last folder Open
// navigated to - both backed by ct::Ini (see extern/containers/include/ct/ini.hpp)
// so the file is a plain human-editable .ini, not a bespoke format.
//
// Storage location comes from SDL_GetPrefPath("zenblitz", "editor") - SDL
// already knows each platform's per-user preferences directory (creating it
// if needed) and hands back an absolute, separator-terminated path, so no
// platform-specific code lives here: Linux typically gets
// ~/.local/share/zenblitz/editor/, Windows %APPDATA%\zenblitz\editor\.

#include <ct/ini.hpp>
#include <ct/vector.hpp>
#include <igui/Gui.hpp>

#include "ClassicTheme.hpp"

namespace zed
{

class EditorSettings
{
public:
    // Reads the .ini at configPath() if it exists; a missing/unreadable
    // file just means "no history yet", not an error.
    void load();

    // Writes the current state back to configPath(), creating the
    // per-user config directory first if needed. Returns false only when
    // the directory/file genuinely couldn't be created - callers may
    // ignore this (settings are a convenience, not required for the
    // editor to work).
    bool save() const;

    // Most-recent-first list of previously opened file paths.
    const ct::Vector<ig::String>& recentFiles() const { return recentFiles_; }

    // Moves path to the front of recentFiles (adding it if new), trimmed
    // to kMaxRecentFiles entries. Does not write to disk - call save().
    void noteFileOpened(const ig::String& path);

    void removeRecentFile(const ig::String& path);
    void clearRecentFiles() { recentFiles_.clear(); }

    // Last folder the Open dialog was pointed at (empty until one exists).
    const ig::String& lastOpenFolder() const { return lastOpenFolder_; }
    void setLastOpenFolder(const ig::String& folder) { lastOpenFolder_ = folder; }

    // Every document open in the editor at the moment of the last save() -
    // separate from recentFiles (which is a most-recent-first MRU list):
    // this is "what was on screen", restored verbatim as a full set of tabs
    // on the next launch. Empty paths (an unsaved "untitled" buffer) are
    // never included - there's nothing on disk to reopen.
    const ct::Vector<ig::String>& openDocuments() const { return openDocuments_; }
    void setOpenDocuments(const ct::Vector<ig::String>& paths) { openDocuments_ = paths; }

    // Window placement, restored on launch (see main.cpp). monitorIndex is
    // an SDL display index; x/y are stored *relative to that display's
    // origin* (SDL_GetDisplayBounds), not screen-absolute - so a saved
    // position still lands on the right spot on the right monitor after
    // docks/taskbars shift a display's origin, and a monitor that no longer
    // exists at load time is easy to detect (index out of range) instead of
    // silently placing the window off-screen.
    struct WindowGeometry
    {
        int monitorIndex = 0;
        int x = 0;
        int y = 0;
        int width = 1280;
        int height = 800;
        bool valid = false; // false until a real geometry has been saved once
    };
    const WindowGeometry& windowGeometry() const { return windowGeometry_; }
    void setWindowGeometry(const WindowGeometry& geometry) { windowGeometry_ = geometry; }

    EditorTheme theme() const { return theme_; }
    void setTheme(EditorTheme theme) { theme_ = theme; }

    // Full path to the settings file this instance loads/saves - exposed
    // mainly so tests and diagnostics don't have to duplicate the platform
    // logic themselves.
    static ig::String configPath();

    static constexpr size_t kMaxRecentFiles = 10;
    static constexpr size_t kMaxOpenDocuments = 32;

private:
    ct::Vector<ig::String> recentFiles_;
    ig::String lastOpenFolder_;
    ct::Vector<ig::String> openDocuments_;
    WindowGeometry windowGeometry_;
    EditorTheme theme_ = EditorTheme::Classic;
};

} // namespace zed
