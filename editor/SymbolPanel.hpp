#pragma once

// Classic funcs/types/labels tabs with navigation to source declarations.

#include <ct/vector.hpp>
#include <igui/Gui.hpp>

#include "SymbolIndex.hpp"
#include "ClassicTheme.hpp"

namespace zed
{

class SymbolPanel
{
public:
    // Rescans text for symbols. Call whenever the editor buffer changes;
    // cheap enough (a single line-oriented pass) to call every time the
    // code editor reports a change, no debouncing needed at this scale.
    void refresh(const ig::String& text);

    // Draws the panel in bounds. Returns the zero-based line to jump to, or
    // -1 if nothing was clicked this frame - the caller (Editor) applies
    // that to its CodeEditorState, since this panel doesn't own the buffer.
    int draw(ig::Context& ui, const ig::Rect& bounds, EditorTheme theme = EditorTheme::Classic);

private:
    ct::Vector<Symbol> symbols_;
    int selectedKind_ = 0;
};

} // namespace zed
