#include "OutputPanel.hpp"

namespace zed
{

void OutputPanel::draw(ig::Context& ui, ig::String& output, bool hasRunOnce, bool& visible)
{
    const ig::Theme& t = ui.theme();
    const ig::Vec2 origin = ui.cursor();
    const float rowHeight = t.buttonHeight;
    const float width = ui.availableWidth();
    const float gap = 4.0f;
    const float buttonWidth = 56.0f;

    // Header row: collapse/expand on the left, Copy/Clear right-aligned.
    // Both buttons stay reachable while collapsed - Clear in particular is
    // the one thing worth doing without having to expand a huge log first.
    const float buttonsX = origin.x + width - (buttonWidth * 2.0f + gap);
    ui.collapsingHeader("Output", visible,
                        ig::Rect(origin.x, origin.y, buttonsX - origin.x - gap, rowHeight));

    // Constant labels: iGUI derives a widget's identity from its label, so
    // swapping the text to fake a disabled state would hand the button a
    // different id every time `output` went from empty to non-empty and
    // lose its hover/press state with it. The empty case is just a no-op.
    if (ui.smallButton("Copy", ig::Rect(buttonsX, origin.y, buttonWidth, rowHeight)) &&
        !output.empty())
    {
        // Context only exposes clipboard access through a CodeEditorState
        // (codeEditorCopy) - no direct "put this string on the clipboard"
        // call - so a throwaway state with everything selected stands in
        // for one. Nothing here is drawn or kept.
        ig::CodeEditorState clipboardSource;
        clipboardSource.setText(output);
        clipboardSource.selectionAnchorLine = 0;
        clipboardSource.selectionAnchorColumn = 0;
        clipboardSource.cursorLine = clipboardSource.lineCount() - 1;
        clipboardSource.cursorColumn =
            static_cast<int>(clipboardSource.lineAt(clipboardSource.cursorLine).size());
        ui.codeEditorCopy(clipboardSource);
    }
    if (ui.smallButton("Clear",
                       ig::Rect(buttonsX + buttonWidth + gap, origin.y, buttonWidth, rowHeight)))
    {
        output.clear();
    }

    ui.setCursor(ig::Vec2(origin.x, origin.y + rowHeight + gap));
    if (!visible) return;

    // Re-checked after the buttons above: Clear may have just emptied it.
    if (output.empty())
    {
        ui.label(hasRunOnce ? "(no output)" : "Press Play to run this program.");
        return;
    }

    // inputTextMultiline as a log view: it already wraps/scrolls long
    // output, which is all a first slice needs. It's editable in place,
    // which is a little more than a log view should allow - acceptable
    // until this becomes a real read-only console.
    //
    // `output` is the caller's own buffer (the tab's lastOutput), handed
    // over directly: this used to copy the whole string into a local
    // scratch every single frame, which got visibly slower the more a
    // program logged, and reset the widget's selection each frame so
    // select-then-Ctrl+C never held. Editing in place costs neither.
    ui.inputTextMultiline("##output", output, 0.0f, ui.availableHeight());
}

} // namespace zed
