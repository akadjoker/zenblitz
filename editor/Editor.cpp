#include "Editor.hpp"

#include <utility>
#include <algorithm>
#include <cstdio>
#include "ClassicTheme.hpp"

#include "FileIO.hpp"

namespace zed
{
    static ig::String parentDirectory(const ig::String& path)
    {
        const size_t slash = path.find_last_of("/\\");
        return slash == ig::String::npos ? ig::String(".") : path.substr(0, slash);
    }

    static ig::String nativeBasePath(const ig::String& scriptPath)
    {
        const size_t dot = scriptPath.find_last_of(".");
        const size_t slash = scriptPath.find_last_of("/\\");
        return dot != ig::String::npos && (slash == ig::String::npos || dot > slash)
            ? scriptPath.substr(0, dot) : scriptPath;
    }

    // Runner's own default already picks the right spelling per platform
    // (see Runner.hpp) - ".\zenblitz3d.exe" on Windows, "./zenblitz3d"
    // elsewhere. Hardcoding the Unix form here bypassed that and made
    // Play fail on Windows with "'.' is not recognized...", cmd.exe's
    // reaction to a Unix "./" prefix it doesn't understand.
    Editor::Editor() : runner_()
    {
        settings_.load();
        theme_ = settings_.theme();
    }

    ig::String Editor::windowTitle() const
    {
        if (!activeTab_)
            return ig::String("ZenEditor");

        ig::String name;
        if (activeTab_->path.empty())
        {
            name = "untitled";
        }
        else
        {
            const size_t slash = activeTab_->path.find_last_of("/\\");
            name = slash == ig::String::npos ? activeTab_->path
                                             : activeTab_->path.substr(slash + 1);
        }

        ig::String title("ZenEditor - ");
        if (activeTab_->isDirty)
            title.append("*");
        title.append(name);
        title.append(" - @djoker 2026");
        return title;
    }

    Editor::~Editor()
    {
        // Games launched by this editor belong to it. Request their
        // termination before SDL tears down the worker threads/process data.
        for (auto &tabPtr : tabs_)
            if (tabPtr->isRunning)
                runner_.stop(tabPtr->runHandle);
        ct::Vector<ig::String> openPaths;
        for (const auto &tabPtr : tabs_)
            if (!tabPtr->path.empty()) // an unsaved "untitled" buffer has nothing to reopen
                openPaths.push_back(tabPtr->path);
        settings_.setOpenDocuments(openPaths);
        settings_.save();
    }

    DocumentTab &Editor::openTab(const ig::String &path, const ig::String &initialText)
    {
        ct::Unique<DocumentTab> tab = ct::make_unique<DocumentTab>();
        tab->path = path;
        tab->code.setText(initialText);
        tab->code.setHighlighterForFile(path.empty() ? "untitled.bb" : path);
        classicSyntax(tab->code, theme_);
        tab->symbols.refresh(initialText);

        tabs_.push_back(std::move(tab));
        selectedTab_ = static_cast<int>(tabs_.size()) - 1;
        activeTab_ = tabs_.back().get();
        return *tabs_.back();
    }

    void Editor::openInitial(const char *path)
    {
        if (path && path[0] != '\0')
        {
            ig::String text;
            if (readFile(path, text))
            {
                openTab(path, text);
                settings_.noteFileOpened(path);
                return;
            }
        }
        else
        {
            // No explicit file on the command line - restore whatever was
            // open at the last save() (see EditorSettings::openDocuments).
            // A path that no longer reads (deleted/moved/renamed since)
            // is silently skipped rather than failing the whole restore.
            int opened = 0;
            for (const ig::String &savedPath : settings_.openDocuments())
            {
                ig::String text;
                if (readFile(savedPath.c_str(), text))
                {
                    openTab(savedPath, text);
                    ++opened;
                }
            }
            if (opened > 0)
                return;
        }
        openTab(ig::String(), "Print \"Hello from zenblitz\"\n");
    }

    void Editor::newFile()
    {
        openTab(ig::String(), "");
    }

    void Editor::openFromDialog(const ig::String &path)
    {
        ig::String text;
        if (readFile(path.c_str(), text))
        {
            openTab(path, text);
            settings_.noteFileOpened(path);
            settings_.setLastOpenFolder(fileProvider_.parentDirectory(path));
        }
        // A file the dialog listed but that fails to read (permissions, race
        // with deletion, ...) is silently skipped for now - the dialog's own
        // fileError field is meant for exactly this, wiring it in is a natural
        // follow-up once Open is proven.
    }

    void Editor::save()
    {
        if (!activeTab_)
            return;
        if (activeTab_->path.empty())
            activeTab_->path = "untitled.bb";
        if (writeFile(activeTab_->path.c_str(), activeTab_->code.text()))
            activeTab_->isDirty = false;
    }

    void Editor::play()
    {
        if (!activeTab_)
            return;
        if (activeTab_->isRunning)
            return; // already running this tab's program - see update()'s poll
        if (activeTab_->path.empty())
            activeTab_->path = "untitled.bb";
        if (!writeFile(activeTab_->path.c_str(), activeTab_->code.text()))
        {
            activeTab_->lastOutput = ig::String("zenblitz-editor: could not write ") + activeTab_->path;
            activeTab_->lastExitCode = -1;
            activeTab_->hasRunOnce = true;
            return;
        }
        activeTab_->isDirty = false;
        activeTab_->lastOutput.clear();
        activeTab_->hasRunOnce = true;

        // Runs on a background thread (see Runner.hpp) so zenblitz3d's own
        // window/event loop never blocks this editor's frame loop - update()
        // polls activeTab_->runHandle every frame and applies the result
        // once Runner::poll() reports it done.
        activeTab_->isRunning = true;
        activeTab_->runHandle = runner_.runAsync(activeTab_->path);
    }

    void Editor::buildNative(bool runAfterBuild)
    {
        if (!activeTab_ || activeTab_->isRunning)
            return;
        if (activeTab_->path.empty())
            activeTab_->path = "untitled.bb";
        if (!writeFile(activeTab_->path.c_str(), activeTab_->code.text()))
        {
            activeTab_->lastOutput = ig::String("zenblitz-editor: could not write ") + activeTab_->path;
            activeTab_->lastExitCode = -1;
            activeTab_->hasRunOnce = true;
            return;
        }

        activeTab_->isDirty = false;
        activeTab_->lastOutput.clear();
        activeTab_->hasRunOnce = true;
        activeTab_->runNativeAfterBuild = runAfterBuild;
        activeTab_->nativeCppPath = nativeBasePath(activeTab_->path) + ".native.cpp";
        activeTab_->nativeBinaryPath = nativeBasePath(activeTab_->path) + ".native";
#if defined(_WIN32)
        activeTab_->nativeBinaryPath += ".exe";
#endif

        const ig::String zencc = NativeFileDialogProvider::executableDirectory()
#if defined(_WIN32)
            + "\\zencc.exe";
#else
            + "/zencc";
#endif
        ct::Vector<ig::String> arguments;
        arguments.push_back("--emit-c");
        arguments.push_back(activeTab_->path);
        arguments.push_back(activeTab_->nativeCppPath);
        activeTab_->nativeBuildPhase = 1;
        activeTab_->isRunning = true;
        activeTab_->runHandle = runner_.runAsync(zencc, arguments);
    }

    void Editor::startNativeCompile(DocumentTab& tab)
    {
        ct::Vector<ig::String> arguments;
        arguments.push_back("-std=c++11");
        arguments.push_back("-O2");
        arguments.push_back(ig::String("-I") + ZEN_EDITOR_SOURCE_DIR + "/codegen");
        arguments.push_back(ig::String("-I") + ZEN_EDITOR_SOURCE_DIR + "/engine/include");
        arguments.push_back(ig::String("-I") + ZEN_EDITOR_SOURCE_DIR + "/extern/GPU/gpu/include");
        arguments.push_back(ig::String("-I") + ZEN_EDITOR_SOURCE_DIR + "/libzen/third_party");
        arguments.push_back(ig::String("-I") + ZEN_EDITOR_SOURCE_DIR + "/extern/SDL/include");
        arguments.push_back(ig::String("-I") + ZEN_EDITOR_BUILD_DIR + "/extern/SDL/include");
        arguments.push_back(tab.nativeCppPath);
        arguments.push_back("-o");
        arguments.push_back(tab.nativeBinaryPath);
        arguments.push_back(ZEN_EDITOR_ENGINE_LIBRARY);
        arguments.push_back(ZEN_EDITOR_GPU_SDL_LIBRARY);
        arguments.push_back(ZEN_EDITOR_GPU_LIBRARY);
        arguments.push_back(ZEN_EDITOR_GPU_COMMON_LIBRARY);
        arguments.push_back(ZEN_EDITOR_SDL_LIBRARY);
#if !defined(_WIN32)
        arguments.push_back(ig::String("-Wl,-rpath,") + ZEN_EDITOR_BUILD_DIR + "/extern/SDL");
        arguments.push_back("-lm");
        arguments.push_back("-lpthread");
        arguments.push_back("-ldl");
#endif
        tab.nativeBuildPhase = 2;
        tab.runHandle = runner_.runAsync(ZEN_EDITOR_CXX_COMPILER, arguments);
    }

    void Editor::stop()
    {
        if (activeTab_ && activeTab_->isRunning)
            runner_.stop(activeTab_->runHandle);
    }

    void Editor::requestClose(DocumentTab &tab)
    {
        if (tab.isRunning)
            return; // don't destroy a DocumentTab whose Runner thread is still writing into it
        if (!tab.isDirty)
        {
            closeNow(tab);
            return;
        }
        pendingClose_ = &tab;
        closeConfirmOpen_ = true;
    }

    void Editor::closeNow(DocumentTab &tab)
    {
        for (size_t i = 0; i < tabs_.size(); ++i)
        {
            if (tabs_[i].get() == &tab)
            {
                tabs_.erase(tabs_.begin() + static_cast<ptrdiff_t>(i));
                break;
            }
        }
        if (tabs_.empty())
            newFile();
        selectedTab_ = std::min(selectedTab_, static_cast<int>(tabs_.size()) - 1);
        activeTab_ = tabs_[selectedTab_].get();
    }

    void Editor::update(ig::Context &ui)
    {
        // Every open tab's pending Play run gets checked every frame,
        // regardless of which tab is active right now - the user may have
        // switched away from the tab that's running (see Runner.hpp for
        // why this is polled rather than blocked on).
        for (auto &tabPtr : tabs_)
        {
            DocumentTab &t = *tabPtr;
            if (!t.isRunning)
                continue;
            RunResult result;
            const bool finished = runner_.poll(t.runHandle, result);
            if (!result.output.empty())
            {
                t.lastOutput.append(result.output);
                // The output panel draws the log through a text widget
                // that re-scans the whole buffer every frame to count
                // lines, so an unbounded log makes the editor slower the
                // more a program prints - a run that spams one line per
                // frame drags the UI to a crawl and keeps it there after
                // the program exits. Keep the tail, which is the part
                // worth reading, and say what was dropped.
                const size_t kMaxOutput = 256u * 1024u;
                if (t.lastOutput.size() > kMaxOutput)
                {
                    size_t cut = t.lastOutput.size() - kMaxOutput;
                    // resume at a line boundary so the first kept line
                    // isn't a fragment
                    while (cut < t.lastOutput.size() && t.lastOutput[cut] != '\n') ++cut;
                    if (cut < t.lastOutput.size()) ++cut;
                    t.lastOutput = ig::String("[... earlier output trimmed ...]\n") +
                                   t.lastOutput.substr(cut);
                }
            }
            if (finished)
            {
                t.lastExitCode = result.exitCode;
                t.hasRunOnce = true;
                if (t.nativeBuildPhase == 1 && result.exitCode == 0)
                {
                    startNativeCompile(t);
                    continue;
                }
                if (t.nativeBuildPhase == 2 && result.exitCode == 0 && t.runNativeAfterBuild)
                {
                    ct::Vector<ig::String> arguments;
                    t.nativeBuildPhase = 3;
                    t.runHandle = runner_.runAsync(t.nativeBinaryPath, arguments, parentDirectory(t.path));
                    continue;
                }
                t.runHandle = nullptr;
                t.isRunning = false;
                t.nativeBuildPhase = 0;
                t.runNativeAfterBuild = false;
            }
        }

        ui.setTheme(classicTheme(theme_));
        if (!ui.beginMainWindow("zenblitz editor"))
            return;

        ToolbarState toolbarState;
        if (activeTab_)
        {
            toolbarState.currentPath = activeTab_->path;
            toolbarState.isDirty = activeTab_->isDirty;
            toolbarState.hasRunOnce = activeTab_->hasRunOnce;
            toolbarState.lastExitCode = activeTab_->lastExitCode;
            toolbarState.isRunning = activeTab_->isRunning;
        }
        for (const ig::String &recent : settings_.recentFiles())
            toolbarState.recentFiles.push_back(recent);

        const EditorTheme previousTheme = theme_;
        switch (toolbar_.draw(ui, toolbarState))
        {
        case ToolbarAction::ThemeClassic:
            theme_ = EditorTheme::Classic;
            break;
        case ToolbarAction::ThemeLight:
            theme_ = EditorTheme::Light;
            break;
        case ToolbarAction::ThemeDark:
            theme_ = EditorTheme::Dark;
            break;
        case ToolbarAction::Close:
            if (activeTab_)
                requestClose(*activeTab_);
            break;
        case ToolbarAction::New:
            newFile();
            break;
        case ToolbarAction::Open:
            openDialogOpen_ = true;
            break;
        case ToolbarAction::OpenRecent:
            openFromDialog(toolbar_.lastRecentFilePicked());
            break;
        case ToolbarAction::Save:
            save();
            break;
        case ToolbarAction::Play:
            play();
            break;
        case ToolbarAction::Build:
            buildNative(false);
            break;
        case ToolbarAction::BuildAndRun:
            buildNative(true);
            break;
        case ToolbarAction::Stop:
            stop();
            break;
        case ToolbarAction::Copy:
            if (activeTab_)
                ui.codeEditorCopy(activeTab_->code);
            break;
        case ToolbarAction::Cut:
            if (activeTab_)
            {
                if (ui.codeEditorCut(activeTab_->code))
                    activeTab_->isDirty = true;
            }
            break;
        case ToolbarAction::Paste:
            if (activeTab_)
            {
                if (ui.codeEditorPaste(activeTab_->code))
                    activeTab_->isDirty = true;
            }
            break;
        case ToolbarAction::Find:
            findBar_.open(false);
            break;
        case ToolbarAction::FindReplace:
            findBar_.open(true);
            break;
        case ToolbarAction::None:
            break;
        }

        ui.setTheme(classicTheme(theme_));
        if (theme_ != previousTheme)
        {
            for (auto &document : tabs_)
                classicSyntax(document->code, theme_);
            settings_.setTheme(theme_);
            settings_.save();
        }

        if (openDialogOpen_)
        {
            ig::FileDialogOptions dialogOptions;
            dialogOptions.title = "Open File";
            dialogOptions.filter = ".bb";
            // Only consulted by fileDialog() while its FileDialogState is
            // still uninitialized (first time this dialog opens after the
            // process started) - once open, the dialog remembers wherever
            // the user has navigated to on its own. Prefer wherever Open
            // was last pointed at; a fresh install with no settings yet
            // opens next to the binary (where sample/project .bb files
            // actually live), not the user's home - fileDialog() would
            // fall back to homeDirectory() itself if this were left empty.
            const ig::String initialOpenPath = settings_.lastOpenFolder().empty()
                ? NativeFileDialogProvider::executableDirectory()
                : settings_.lastOpenFolder();
            dialogOptions.initialPath = ig::StringView(initialOpenPath);
            ig::FileDialogResult result = ui.fileDialog("open-bb", openDialogOpen_, openDialogState_,
                                                        dialogOptions, fileProvider_);
            if (result.kind == ig::FileDialogResultKind::Accepted)
                openFromDialog(result.path);
        }

        // A dirty tab's close waited for this prompt (see requestClose) - Save
        // writes it out then closes, Discard closes without saving, Cancel
        // leaves the tab open and clears the pending close.
        if (closeConfirmOpen_ && pendingClose_)
        {
            ig::MessageBoxOptions confirmOptions;
            confirmOptions.acceptLabel = "Save";
            confirmOptions.showDiscard = true;
            confirmOptions.showCancel = true;
            ig::String message = ig::String("\"") + pendingClose_->tabLabel() + "\" has unsaved changes.";
            ig::MessageBoxResult result = ui.messageBox("Close file?", message, closeConfirmOpen_, confirmOptions);
            if (result == ig::MessageBoxResult::Accepted)
            {
                DocumentTab *previousActive = activeTab_;
                activeTab_ = pendingClose_;
                save();
                activeTab_ = previousActive;
                if (!pendingClose_->isDirty)
                {
                    closeNow(*pendingClose_);
                    pendingClose_ = nullptr;
                }
                else
                    closeConfirmOpen_ = true;
            }
            else if (result == ig::MessageBoxResult::Discarded)
            {
                closeNow(*pendingClose_);
                pendingClose_ = nullptr;
            }
            else if (result == ig::MessageBoxResult::Cancelled)
            {
                pendingClose_ = nullptr;
            }
        }

        if (findBar_.visible && activeTab_)
        {
            ui.spacing(4.0f);
            findBar_.draw(ui, activeTab_->code);
        }
        ui.spacing(4.0f);

        // A fixed split matches BlitzIDE and gives every widget its actual viewport.
        const ig::Theme chrome = ui.theme();
        const ig::Vec2 origin = ui.cursor();
        const float width = ui.availableWidth();
        const float height = ui.availableHeight();
        const float statusHeight = 24.0f;
        const float bodyHeight = std::max(1.0f, height - statusHeight);
        const float sideWidth = width >= 500 ? std::min(220.0f, width * 0.23f) : 0.0f;
        const float codeWidth = std::max(1.0f, width - sideWidth - (sideWidth > 0 ? 5 : 0));

        drawDocumentTabs(ui, codeWidth);
        activeTab_ = tabs_[selectedTab_].get();
        DocumentTab &tab = *activeTab_;
        const ig::Vec2 codeOrigin = ui.cursor();
        const float availableBodyHeight = std::max(1.0f, origin.y + bodyHeight - codeOrigin.y);
        const float splitterThickness = 5.0f;
        // Bottom pane's height is user-adjustable (outputPanelHeight_,
        // dragged via the splitter below) once there's actually a run to
        // show - clamped so the code view always keeps a sane minimum, and
        // so shrinking the window can't push the splitter off past either
        // edge (that clamp is also what makes the splitter widget itself
        // safe to call unconditionally below with min < max).
        const float minCodeHeight = 80.0f;
        const float minOutputHeight = 40.0f;
        const float maxOutputHeight = std::max(minOutputHeight,
            availableBodyHeight - minCodeHeight - splitterThickness);
        // Collapsed still reserves the panel's header row, so its expand
        // toggle and Copy/Clear stay reachable (see OutputPanel::draw);
        // only the log body goes away, handing its height to the code view.
        const float collapsedOutputHeight = ui.theme().buttonHeight + 12.0f;
        const float outputHeight = !tab.hasRunOnce
            ? 0.0f
            : (outputVisible_
                   ? ig::clamp(outputPanelHeight_, minOutputHeight, maxOutputHeight)
                   : std::min(collapsedOutputHeight, maxOutputHeight));
        const float codeHeight = std::max(1.0f, availableBodyHeight - outputHeight -
                                          (outputHeight > 0.0f ? splitterThickness : 0.0f));
        const ig::Rect editorBounds(codeOrigin.x, codeOrigin.y, codeWidth, codeHeight);
        ui.pushId(static_cast<uint64_t>(selectedTab_) + 1);
        ui.setTheme(sourceTheme(chrome, theme_));
        ig::CodeEditorOptions options;
        options.showLineNumbers = false;
        options.showFolding = false;
        if (ui.codeEditor("source", tab.code, editorBounds, options))
        {
            tab.isDirty = true;
            tab.symbols.refresh(tab.code.text());
        }
        ui.setTheme(chrome);
        if (ui.beginContextMenu("editor context", editorBounds))
        {
            if (ui.menuItem("Cut"))
            {
                if (ui.codeEditorCut(tab.code))
                    tab.isDirty = true;
            }
            if (ui.menuItem("Copy"))
                ui.codeEditorCopy(tab.code);
            if (ui.menuItem("Paste"))
            {
                if (ui.codeEditorPaste(tab.code))
                    tab.isDirty = true;
            }
            if (ui.menuItem("Find"))
                findBar_.open(false);
            ui.endContextMenu();
        }
        ui.popId();
        if (sideWidth > 0)
        {
            const ig::Rect side(origin.x + codeWidth + 5, codeOrigin.y, sideWidth,
                                bodyHeight - (codeOrigin.y - origin.y));
            const int line = tab.symbols.draw(ui, side, theme_);
            if (line >= 0)
            {
                tab.code.cursorLine = line;
                tab.code.cursorColumn = 0;
                tab.code.clearSelection();
                tab.code.scrollLine = line;
            }
        }
        if (outputHeight > 0)
        {
            // Draggable divider between the code view and the Output pane.
            // splitter's value is a *local* Y inside splitterBounds (see
            // Context::splitter in Gui.cpp) - splitterBounds spans the full
            // code+splitter+output stack, so the splitter's current local Y
            // (codeHeight) converts straight back to a height once dragged:
            // outputPanelHeight_ = availableBodyHeight - newLocalY - thickness.
            // The handle must sit entirely inside the gap between the code
            // view (ends at codeHeight, exclusive) and the Output child (starts
            // at codeHeight + thickness). Centering it on codeHeight put its
            // top half inside the code editor's rect, and since that widget
            // runs first in the frame it claimed every press landing there -
            // half the bar was dead. Centered on the gap's midpoint it overlaps
            // neither, so splitter() is the only widget that sees the press.
            // Only while the log body is actually shown - dragging the
            // divider of a collapsed pane would just fight the fixed
            // header-row height it is pinned to.
            if (outputVisible_)
            {
                const ig::Rect splitterBounds(codeOrigin.x, codeOrigin.y, codeWidth, availableBodyHeight);
                const float halfThickness = splitterThickness * 0.5f;
                float splitterY = codeHeight + halfThickness;
                if (ui.splitter("output-splitter", splitterY,
                                minCodeHeight + halfThickness,
                                availableBodyHeight - minOutputHeight - halfThickness,
                                ig::SplitterAxis::Horizontal, splitterBounds, splitterThickness))
                {
                    const float newCodeHeight = splitterY - halfThickness;
                    outputPanelHeight_ = availableBodyHeight - newCodeHeight - splitterThickness;
                }
            }

            const ig::Vec2 outputOrigin(codeOrigin.x, codeOrigin.y + codeHeight + splitterThickness);
            ui.setCursor(outputOrigin);
            const ig::Rect outputBounds(outputOrigin.x, outputOrigin.y, codeWidth, outputHeight);
            if (ui.beginChild("output", outputHeight, true, codeWidth))
            {
                outputPanel_.draw(ui, tab.lastOutput, tab.hasRunOnce, outputVisible_);
                ui.endChild();
            }
            // Opened against the panel's own outer bounds, not inside the
            // beginChild above - a menu opened while contentClip() is still
            // the child's small clip rect gets its popup clipped to that
            // same tiny area (see OutputPanel::draw's header comment).
            if (ui.beginContextMenu("output context", outputBounds))
            {
                if (ui.menuItem("Copy", !tab.lastOutput.empty()))
                {
                    // Context only exposes clipboard access through a
                    // CodeEditorState (codeEditorCopy) - no direct
                    // "put this string on the clipboard" call - so a
                    // throwaway state with the whole output selected
                    // stands in for one. Nothing here is drawn or kept.
                    ig::CodeEditorState clipboardSource;
                    clipboardSource.setText(tab.lastOutput);
                    clipboardSource.selectionAnchorLine = 0;
                    clipboardSource.selectionAnchorColumn = 0;
                    clipboardSource.cursorLine = clipboardSource.lineCount() - 1;
                    clipboardSource.cursorColumn = static_cast<int>(
                        clipboardSource.lineAt(clipboardSource.cursorLine).size());
                    ui.codeEditorCopy(clipboardSource);
                }
                if (ui.menuItem("Clear", !tab.lastOutput.empty()))
                    tab.lastOutput.clear();
                ui.endContextMenu();
            }
        }
        ui.setCursor(ig::Vec2(origin.x, origin.y + bodyHeight));
        ui.separator();
        char status[128];
        std::snprintf(status, sizeof(status), "Row:%d Col:%d%s%s | %d FPS", tab.code.cursorLine + 1,
                      tab.code.cursorColumn, tab.isDirty ? "  Modified" : "",
                      tab.isRunning ? "  Running..." : "",
                      static_cast<int>(fps_ + 0.5f));
        ui.label(status);
        if (tab.isRunning)
        {
            const float stopWidth = 48.0f;
            const ig::Rect stopRect(origin.x + width - stopWidth, origin.y + bodyHeight, stopWidth, 20.0f);
            if (ui.smallButton("Stop", stopRect))
                runner_.stop(tab.runHandle);
        }
        ui.endWindow();
    }

    void Editor::drawDocumentTabs(ig::Context &ui, float width)
    {
        const ig::Theme &t = ui.theme();
        const ig::Vec2 origin = ui.cursor();
        const float tabHeight = t.widgetHeight;
        const float closeSize = 12.0f;
        const float closePad = 6.0f;
        const float glyphWidth = t.fontSize * 0.6f;
        const float tabChrome = 6.0f + closePad * 2.0f + closeSize;
        const float minTabWidth = 70.0f;
        const float maxTabWidth = 240.0f;
        const ig::Color closeIdleColor = t.borderColor;
        const ig::Color closeHotColor(196, 60, 60, 255); // hand-picked: no dangerColor in Theme
        const float tabCount = static_cast<float>(tabs_.size());

        // Each tab's natural width from its own label, capped so a couple
        // of open files don't stretch unreadably wide - this is what's
        // used as long as everything still fits the strip.
        float naturalTotal = 0.0f;
        for (size_t i = 0; i < tabs_.size(); ++i)
            naturalTotal += ig::clamp(tabs_[i]->tabLabel().size() * glyphWidth + tabChrome,
                                      minTabWidth, maxTabWidth);

        // Doesn't fit even at minTabWidth each: reserve scroll-arrow gutters
        // and let tabScrollOffset_ shift the strip instead of shrinking
        // tabs into something unreadable or unclickable.
        const bool needsScroll = tabCount > 0.0f && minTabWidth * tabCount > width;
        // Both arrows grouped at the strip's right end (Qt Creator's
        // layout) rather than one at each end - keeps them together as
        // one "scroll" control instead of reading as two unrelated edges.
        const float arrowWidth = needsScroll ? 18.0f : 0.0f;
        const float stripWidth = width - arrowWidth * 2.0f;

        // Fits, just not at natural width: shrink every tab by the same
        // factor rather than truncating only the last one, so the strip
        // still reads as one continuous row of equally-important tabs.
        float tabWidth = 0.0f; // used only when !needsScroll
        if (!needsScroll && tabCount > 0.0f)
            tabWidth = naturalTotal > width ? width / tabCount : -1.0f; // -1: use each tab's own natural width

        if (needsScroll)
        {
            const float maxScroll = std::max(0.0f, minTabWidth * tabCount - stripWidth);
            const ig::Rect leftArrow(origin.x + stripWidth, origin.y, arrowWidth, tabHeight);
            const ig::Rect rightArrow(origin.x + stripWidth + arrowWidth, origin.y, arrowWidth, tabHeight);
            if (ui.smallButton("<##tabscroll-left", leftArrow))
                tabScrollOffset_ -= minTabWidth;
            if (ui.smallButton(">##tabscroll-right", rightArrow))
                tabScrollOffset_ += minTabWidth;
            tabScrollOffset_ = ig::clamp(tabScrollOffset_, 0.0f, maxScroll);

            // Keep the active tab in view when it changes from elsewhere
            // (Open, a keyboard shortcut, closing a tab before it) - not
            // just when the strip itself is clicked.
            const float selectedLeft = minTabWidth * static_cast<float>(selectedTab_);
            const float selectedRight = selectedLeft + minTabWidth;
            if (selectedLeft < tabScrollOffset_)
                tabScrollOffset_ = selectedLeft;
            else if (selectedRight > tabScrollOffset_ + stripWidth)
                tabScrollOffset_ = selectedRight - stripWidth;
        }
        else
        {
            tabScrollOffset_ = 0.0f;
        }

        const float stripX = origin.x;
        float x = stripX - (needsScroll ? tabScrollOffset_ : 0.0f);
        int closedTabIndex = -1;
        for (size_t i = 0; i < tabs_.size(); ++i)
        {
            DocumentTab &tab = *tabs_[i];
            ig::String label = tab.tabLabel();
            const float thisWidth = needsScroll ? minTabWidth
                                    : (tabWidth > 0.0f ? tabWidth
                                      : ig::clamp(label.size() * glyphWidth + tabChrome, minTabWidth, maxTabWidth));

            // Off either side of the visible strip: still advances x (so
            // later tabs land in the right place) but draws nothing and
            // can't be clicked - contentClip() alone wouldn't stop
            // isClicked() from reacting to a press on a tab visually
            // hidden behind the scroll arrows.
            if (x + thisWidth > stripX && x < stripX + stripWidth)
            {
                const ig::Rect tabRect(x, origin.y, thisWidth, tabHeight);
                const ig::Rect closeRect(tabRect.right() - closePad - closeSize, tabRect.y + (tabHeight - closeSize) * 0.5f,
                                         closeSize, closeSize);

                ui.pushId(static_cast<uint64_t>(i) + 1);
                const bool selected = static_cast<int>(i) == selectedTab_;
                const bool tabHovered = ui.isHovered("tab", tabRect);
                const bool closeHovered = ui.isHovered("tabclose", closeRect);

                const ig::Color background = selected ? t.selectableSelected
                                            : (tabHovered ? t.selectableHovered : t.buttonBackground);
                ui.drawRectFilled(tabRect, background);
                ui.drawRect(tabRect, t.borderColor);

                // Close hit-tests before the tab body so a click on the x never
                // also selects the tab it's closing.
                if (ui.isClicked("tabclose", closeRect))
                    closedTabIndex = static_cast<int>(i);
                else if (ui.isClicked("tab", tabRect))
                    selectedTab_ = static_cast<int>(i);

                const size_t maxChars = static_cast<size_t>((thisWidth - tabChrome) / glyphWidth);
                if (label.size() > maxChars && maxChars > 1)
                    label = label.substr(0, maxChars - 1) + "~";
                ui.label(label.c_str(), ig::Vec2(tabRect.x + 6.0f, tabRect.y + (tabHeight - t.fontSize) * 0.5f));

                // The x itself: two crossed lines inside closeRect, same rect
                // the click above tests against - one object, not a widget
                // bolted on beside the tab.
                const ig::Color closeColor = closeHovered ? closeHotColor : closeIdleColor;
                ui.drawLine(ig::Vec2(closeRect.x + 2.0f, closeRect.y + 2.0f),
                            ig::Vec2(closeRect.right() - 2.0f, closeRect.bottom() - 2.0f), closeColor, 1.5f);
                ui.drawLine(ig::Vec2(closeRect.right() - 2.0f, closeRect.y + 2.0f),
                            ig::Vec2(closeRect.x + 2.0f, closeRect.bottom() - 2.0f), closeColor, 1.5f);

                ui.popId();
            }
            x += thisWidth;
        }

        ui.setCursor(ig::Vec2(origin.x, origin.y + tabHeight));

        if (closedTabIndex >= 0)
            requestClose(*tabs_[static_cast<size_t>(closedTabIndex)]);
    }

} // namespace zed
