#include "SymbolPanel.hpp"
#include "ClassicTheme.hpp"
#include <algorithm>

namespace zed {
void SymbolPanel::refresh(const ig::String& text) { symbols_ = scanSymbols(text); }
int SymbolPanel::draw(ig::Context& ui, const ig::Rect& bounds, EditorTheme theme) {
    const auto chrome = ui.theme();
    ui.setCursor(ig::Vec2(bounds.x,bounds.y));
    const ig::StringView labels[] = {"funcs","types","labels"};
    ui.tabBar("symbol categories",selectedKind_,ig::Span<const ig::StringView>(labels,3),bounds.width);
    const float remaining = std::max(1.0f,bounds.y+bounds.height-ui.cursor().y);
    ui.setCursor(ig::Vec2(bounds.x,ui.cursor().y));
    ui.setTheme(sourceTheme(chrome,theme));
    int result = -1;
    if (ui.beginChild("symbol list",remaining,true,bounds.width)) {
        const SymbolKind kinds[] = {SymbolKind::Function,SymbolKind::Type,SymbolKind::Label};
        for (const auto& symbol : symbols_) {
            if (symbol.kind != kinds[selectedKind_]) continue;
            ui.pushId(static_cast<uint64_t>(symbol.line)+1);
            if (ui.selectable(symbol.name,false)) result=symbol.line;
            ui.popId();
        }
        ui.endChild();
    }
    ui.setTheme(chrome);
    return result;
}
}
