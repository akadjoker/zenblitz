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
    void draw(ig::Context& ui, ig::String& output, bool hasRunOnce, bool& visible);
};

} // namespace zed
