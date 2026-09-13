#include "FindBar.hpp"

namespace zed
{

void FindBar::open(bool withReplace)
{
    visible = true;
    showReplace = withReplace;
}

void FindBar::draw(ig::Context& ui, ig::CodeEditorState& code)
{
    if (!visible) return;

    ui.separatorText(showReplace ? "Find & Replace" : "Find");

    ui.inputText("Find", findText_, 260.0f);
    ui.sameLine();
    if (ui.button("Next") && !findText_.empty())
        code.findNext(findText_, caseSensitive_, useRegex_);
    ui.sameLine();
    if (ui.button("Prev") && !findText_.empty())
        code.findPrev(findText_, caseSensitive_, useRegex_);
    ui.sameLine();
    ui.checkbox("Case", caseSensitive_);
    ui.sameLine();
    ui.checkbox("Regex", useRegex_);
    ui.sameLine();
    if (ui.button("Close"))
        close();

    if (showReplace)
    {
        ui.inputText("Replace", replaceText_, 260.0f);
        ui.sameLine();
        if (ui.button("Replace all") && !findText_.empty())
            code.replaceAll(findText_, replaceText_, caseSensitive_, useRegex_);
    }
}

} // namespace zed
