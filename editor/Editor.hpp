#pragma once

// Owns open documents and the classic BlitzIDE toolbar, source/symbol split,
// optional output and status bar. main.cpp owns the SDL platform loop.

#include <ct/ptr.hpp>
#include <ct/vector.hpp>
#include <igui/FileDialog.hpp>
#include <igui/Gui.hpp>

#include "DocumentTab.hpp"
#include "ClassicTheme.hpp"
#include "EditorSettings.hpp"
#include "FindBar.hpp"
#include "OutputPanel.hpp"
#include "NativeFileDialogProvider.hpp"
#include "Runner.hpp"
#include "Toolbar.hpp"

namespace zed
{

class Editor
{
public:
    Editor();
    // Persists settings_ (recent files, last folder) - see EditorSettings.
    ~Editor();
    void setToolbarIcons(ig::TextureId icons) { toolbar_.icons = icons; }

    // Latest smoothed frame rate, shown in the status bar by update().
    void setFps(float fps) { fps_ = fps; }

    // main.cpp reads windowGeometry() before creating the SDL window (to
    // restore position/size/monitor) and writes it back via
    // setWindowGeometry() right before the Editor is destroyed (so it's
    // included in the ~Editor() settings_.save() - see EditorSettings for
    // what's actually persisted and how). Everything else settings_ tracks
    // (recent files, open documents) stays internal to Editor.
    const EditorSettings::WindowGeometry& windowGeometry() const { return settings_.windowGeometry(); }
    void setWindowGeometry(const EditorSettings::WindowGeometry& geometry) { settings_.setWindowGeometry(geometry); }

    // Opens a file as a new tab and makes it active. With an explicit path
    // (argv[1]) that reads successfully, only that file is opened. With no
    // path, restores every document settings_ remembers being open at the
    // last save() (see EditorSettings::openDocuments), each as its own tab,
    // most-recently-active last so it ends up selected; if none of those
    // exist any more either, falls back to a small built-in sample - so the
    // editor always starts with something to show.
    void openInitial(const char* path);

    // Draws the whole editor window and applies whatever the toolbar/panels
    // panels report this frame. Called once per frame inside the
    // platform's beginFrame/endFrame.
    void update(ig::Context& ui);

    // "ZenEditor - <file>", with a leading "*" while the active document has
    // unsaved changes. main.cpp applies it with SDL_SetWindowTitle when it
    // changes; Editor has no window handle of its own.
    ig::String windowTitle() const;

private:
    ct::Vector<ct::Unique<DocumentTab>> tabs_;
    // Selected document; updated immediately when tabs open, switch or close.
    DocumentTab* activeTab_ = nullptr;

    EditorTheme theme_ = EditorTheme::Classic;
    int selectedTab_ = 0;
    // Smoothed FPS reported by main.cpp each frame (see setFps).
    float fps_ = 0.0f;
    // User-adjustable via the splitter drawn above the Output panel (see
    // drawDocumentTabs's sibling logic in update()) - not per-tab, matching
    // BlitzIDE's single fixed-position output pane.
    float outputPanelHeight_ = 160.0f;
    // Collapsed/expanded state of the Output pane's log body. The header
    // row (its toggle plus Copy/Clear) stays on screen either way, so
    // collapsing frees the code view's height without losing the run's
    // captured output - lastOutput/hasRunOnce are untouched by it.
    bool outputVisible_ = true;
    // Horizontal scroll of the document tab strip, in pixels - only
    // nonzero once tabs no longer fit even at their floor width (see
    // drawDocumentTabs). Clamped every frame to [0, maxScroll], so a tab
    // close that shortens the strip can't leave it scrolled past the end.
    float tabScrollOffset_ = 0.0f;
    Runner runner_;
    Toolbar toolbar_;
    OutputPanel outputPanel_;
    FindBar findBar_;

    NativeFileDialogProvider fileProvider_;
    ig::FileDialogState openDialogState_;
    bool openDialogOpen_ = false;
    EditorSettings settings_;

    // Unsaved documents wait for Save/Discard/Cancel before closing.
    DocumentTab* pendingClose_ = nullptr;
    bool closeConfirmOpen_ = false;

    DocumentTab& openTab(const ig::String& path, const ig::String& initialText);
    void newFile();
    void openFromDialog(const ig::String& path);
    void save();
    void play();
    void buildNative(bool runAfterBuild);
    void startNativeCompile(DocumentTab& tab);
    void stop();
    // Closes immediately if the tab has no unsaved changes; otherwise opens
    // the confirmation prompt update() draws and defers to its answer.
    void requestClose(DocumentTab& tab);
    void closeNow(DocumentTab& tab);

    // Draws one tab per open document at the layout cursor, each with its
    // own close (x) glyph inside the same tab rect (not a separate widget
    // beside it - see NativeFileDialogProvider's sibling files for the same
    // "one rect, not two widgets" lesson applied to tabs). Updates
    // selectedTab_ on click and calls requestClose() when a tab's x is
    // clicked. width bounds the whole strip; each tab gets an equal share
    // capped at a comfortable reading width.
    void drawDocumentTabs(ig::Context& ui, float width);
};

} // namespace zed
