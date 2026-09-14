#pragma once

// Menu bar (File/Edit/Run) plus a button row (New, Save, Play) and the
// current file name/dirty indicator. Reports which action the user picked
// this frame - it never touches the buffer, Runner, or clipboard itself,
// Editor::update applies the result. This is the layout BlitzIDE's
// mainframe.cpp menus and toolbar covered (File New/Open/Save, Edit
// Cut/Copy/Paste/Find, Run Execute) adapted to iGUI.

#include <ct/vector.hpp>
#include <igui/Gui.hpp>

namespace zed
{

enum class ToolbarAction
{
    ThemeClassic,
    ThemeLight,
    ThemeDark,
    None,
    New,
    Open,
    OpenRecent, // see Toolbar::lastRecentFilePicked() for which one
    Save,
    Play,
    Build,
    BuildAndRun,
    Stop,
    Close,
    Copy,
    Cut,
    Paste,
    Find,
    FindReplace,
};

struct ToolbarState
{
    ig::String currentPath;   // display only - Editor owns the real path
    bool isDirty = false;
    bool hasRunOnce = false;
    bool isRunning = false;
    int lastExitCode = 0;
    // Most-recent-first; drawn as a File > Recent Files submenu. Empty
    // hides the submenu entirely rather than showing it disabled.
    ct::Vector<ig::String> recentFiles;
};

class Toolbar
{
public:
    ig::TextureId icons;
    // Draws the menu bar and button row at the current layout cursor.
    // Returns the action picked this frame (only one can fire per frame).
    ToolbarAction draw(ig::Context& ui, const ToolbarState& state);

    // Valid only for the frame draw() returned ToolbarAction::OpenRecent -
    // the path from state.recentFiles the user picked that frame.
    const ig::String& lastRecentFilePicked() const { return recentFilePicked_; }

private:
    ig::String recentFilePicked_;
};

} // namespace zed
