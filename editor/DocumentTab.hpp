#pragma once

// One open .bb document: its own buffer, path, dirty flag, symbol index,
// and the output/exit code of its own last Play run. The editor keeps a
// list of these (see Editor.hpp) so several files can be open at once,
// switched between with a tab bar.

#include <igui/Gui.hpp>

#include "Runner.hpp"
#include "SymbolPanel.hpp"

namespace zed
{

class DocumentTab
{
public:
    ig::CodeEditorState code;
    ig::String path;        // empty until Save/Open gives it a real file name
    bool isDirty = false;

    ig::String lastOutput;
    int lastExitCode = 0;
    bool hasRunOnce = false;

    // Set by Editor::play() while a run started via Runner::runAsync() for
    // this tab hasn't finished yet (see Editor::update(), which polls it
    // every frame) - lets the UI show "Running..." and refuse to start a
    // second overlapping run on the same tab. Never touched by the worker
    // thread itself; only Runner::poll() (called from the UI thread) reads
    // and clears it.
    Runner::RunHandle* runHandle = nullptr;
    bool isRunning = false;
    int nativeBuildPhase = 0;
    bool runNativeAfterBuild = false;
    ig::String nativeCppPath;
    ig::String nativeBinaryPath;

    SymbolPanel symbols;

    // Short label for the tab strip: the file's leaf name, or "untitled" for
    // an unsaved buffer, with a leading "*" while isDirty is set.
    ig::String tabLabel() const
    {
        ig::String base;
        if (path.empty())
        {
            base = "untitled";
        }
        else
        {
            size_t slash = path.find_last_of("/\\");
            base = slash == ig::String::npos ? path : path.substr(slash + 1);
        }
        return isDirty ? (ig::String("* ") + base) : base;
    }
};

} // namespace zed
