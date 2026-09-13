#pragma once

// Read-only-in-spirit log view for the last Play run's captured output.
// A follow-up (color per line, click-to-jump on "file:line:col: error:")
// is worth doing once the plain loop below is proven - see main.cpp's
// header comment for the scope this first slice sticks to.

#include <igui/Gui.hpp>

namespace zed
{

class OutputPanel
{
public:
    // Draws the header row (label + Copy/Clear buttons) and, while
    // *visible, the log body underneath. `output` is edited in place
    // (handed straight to inputTextMultiline instead of a fresh per-frame
    // copy - see .cpp) and Clear empties it directly; Copy puts it on the
    // clipboard via the same throwaway-CodeEditorState trick the output
    // context menu already used. `visible` is toggled by the header's
    // show/hide button so the panel can be collapsed without losing
    // hasRunOnce/lastOutput state.
    // `reserveInputRow` keeps a row's worth of height free at the bottom
    // for drawInputRow, so the log does not draw underneath it.
    void draw(ig::Context& ui, ig::String& output, bool hasRunOnce, bool& visible,
              bool reserveInputRow = false);

    // While a program is running, draws a one-line entry under the log and
    // returns true once the user submits it (Enter or Send), with the text
    // in `line` and the field cleared. Scripts ask questions with Input$
    // before opening a window (start.bb's graphics-mode menu is the usual
    // one), and without somewhere to type the answer they just hang.
    bool drawInputRow(ig::Context& ui, ig::String& line);

private:
    ig::String inputBuffer_;
};

} // namespace zed
