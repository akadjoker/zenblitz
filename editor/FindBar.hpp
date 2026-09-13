#pragma once

// Find/replace bar for the active document's CodeEditorState, toggled by
// Ctrl+F (find) or Ctrl+H (find+replace). Thin UI over
// CodeEditorState::findNext/findPrev/replaceAll - all the search logic
// (literal or ct::Regex, case sensitivity) already lives there.

#include <igui/Gui.hpp>

namespace zed
{

class FindBar
{
public:
    bool visible = false;
    bool showReplace = false; // Ctrl+H shows the replace row too; Ctrl+F hides it

    void open(bool withReplace);
    void close() { visible = false; }

    // Draws the bar (if visible) and applies Enter/F3-style navigation.
    // Operates directly on `code` since find/replace only makes sense
    // against the currently active document.
    void draw(ig::Context& ui, ig::CodeEditorState& code);

private:
    ig::String findText_;
    ig::String replaceText_;
    bool caseSensitive_ = false;
    bool useRegex_ = false;
};

} // namespace zed
