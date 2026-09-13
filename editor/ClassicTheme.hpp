#pragma once
#include <igui/Gui.hpp>

namespace zed {
enum class EditorTheme { Classic, Light, Dark };
inline ig::Theme classicTheme(EditorTheme mode = EditorTheme::Classic) {
    auto t = ig::makeTheme(mode == EditorTheme::Dark ? ig::ThemePreset::VSCode : ig::ThemePreset::Light);
    t.fontSize = 16; t.menuBarHeight = 22; t.widgetHeight = 24;
    t.buttonHeight = 24; t.itemSpacing = 3; t.windowPadding = 3;
    t.padding = 3; t.borderRadius = 0; t.lineSpacing = 2;
    if (mode == EditorTheme::Classic) {
    t.panelColor = t.windowBackground = t.menuBarBg = ig::Color(212,208,200,255);
    t.buttonBackground = t.buttonNormal = t.panelColor;
    t.borderColor = t.buttonBorder = ig::Color(128,128,128,255);
    t.selectableSelected = ig::Color(245,243,238,255);
    t.selectableHovered = ig::Color(230,228,220,255);
    }
    t.dialogScrim = ig::Color(0,0,0,32);
    return t;
}
inline ig::Theme sourceTheme(const ig::Theme& base, EditorTheme mode = EditorTheme::Classic) {
    auto t = base;
    if (mode != EditorTheme::Classic) {
        t.panelColor = t.bgColor = t.inputBg;
        return t;
    }
    t.inputBg = t.panelColor = t.bgColor = ig::Color(34,85,136,255);
    t.textColor = t.labelText = t.buttonText = ig::Color(255,255,255,255);
    t.selectableBackground = t.inputBg;
    t.selectableHovered = ig::Color(49,106,162,255);
    t.selectableSelected = t.selectionColor = ig::Color(12,49,95,255);
    return t;
}
inline void classicSyntax(ig::CodeEditorState& code, EditorTheme mode = EditorTheme::Classic) {
    auto* h = code.highlighter();
    if (!h) return;
    using T = ig::syntax::SyntaxHighlighter::TokenType;
    if (mode != EditorTheme::Classic) {
        const bool light = mode == EditorTheme::Light;
        for (int i=0; i<ig::syntax::SyntaxHighlighter::TokenTypeCount; ++i)
            h->setColor(static_cast<T>(i), light ? ig::Color(35,38,44,255) : ig::Color(220,220,220,255));
        h->setColor(T::Keyword, light ? ig::Color(112,35,155,255) : ig::Color(198,140,225,255));
        h->setColor(T::Type, light ? ig::Color(0,95,145,255) : ig::Color(90,185,215,255));
        h->setColor(T::String, light ? ig::Color(150,55,20,255) : ig::Color(215,160,130,255));
        h->setColor(T::Comment, light ? ig::Color(40,110,45,255) : ig::Color(125,170,105,255));
        h->setColor(T::Number, light ? ig::Color(0,105,110,255) : ig::Color(160,205,180,255));
        return;
    }
    for (int i=0; i<ig::syntax::SyntaxHighlighter::TokenTypeCount; ++i)
        h->setColor(static_cast<T>(i), ig::Color(238,238,238,255));
    h->setColor(T::Keyword, ig::Color(170,255,255,255));
    h->setColor(T::Type, ig::Color(170,255,255,255));
    h->setColor(T::String, ig::Color(0,255,102,255));
    h->setColor(T::Comment, ig::Color(255,238,0,255));
    h->setColor(T::Number, ig::Color(51,255,221,255));
}
}
