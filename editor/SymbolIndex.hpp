#pragma once

// A lightweight, line-oriented scanner over a .bb buffer that extracts the
// symbols the editor's outliner shows: Function/Type declarations and
// Global/Const names. Deliberately not a real parser (no expressions, no
// nesting beyond top-level Function/Type blocks) - it only needs to find
// declaration keywords and the identifier that follows them well enough for
// navigation, the same job BlitzIDE's funclist.cpp did for the original
// editor (see blitzide/funclist.cpp).

#include <ct/vector.hpp>
#include <igui/Types.hpp>

namespace zed
{

enum class SymbolKind
{
    Label,
    Function,
    Type,
    Global,
    Const,
};

struct Symbol
{
    SymbolKind kind;
    ig::String name;     // as written, including a $/#/% suffix if any
    int line = 0;         // zero-based, matches CodeEditorState::cursorLine
};

// Scans every line of text for a leading declaration keyword (Function,
// Type, Global, Const) and records one Symbol per name found - a `Global`
// or `Const` line naming several variables ("Global a, b$, c") yields one
// Symbol per name. Symbols keep the source order; callers wanting them
// grouped by kind for display do that grouping themselves (see
// SymbolPanel), so this stays a single, order-preserving pass over the text.
ct::Vector<Symbol> scanSymbols(const ig::String& text);

} // namespace zed
