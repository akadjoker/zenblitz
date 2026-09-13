#include "SymbolIndex.hpp"

namespace zed
{

namespace
{

bool isIdentStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool isIdentChar(char c)
{
    return isIdentStart(c) || (c >= '0' && c <= '9') || c == '$' || c == '#' || c == '%';
}

// Case-insensitive match of `word` against text starting at i, requiring a
// non-identifier character (or end of line) right after it - so "Functional"
// doesn't match the "Function" keyword.
bool matchesKeyword(const ig::String& text, size_t i, const char* word)
{
    size_t n = 0;
    while (word[n])
    {
        if (i + n >= text.size()) return false;
        char a = text[i + n];
        char w = word[n];
        char lowerA = (a >= 'A' && a <= 'Z') ? static_cast<char>(a - 'A' + 'a') : a;
        char lowerW = (w >= 'A' && w <= 'Z') ? static_cast<char>(w - 'A' + 'a') : w;
        if (lowerA != lowerW) return false;
        ++n;
    }
    if (i + n < text.size() && isIdentChar(text[i + n])) return false;
    return true;
}

size_t skipSpaces(const ig::String& text, size_t i)
{
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
    return i;
}

// Reads one identifier (with an optional trailing $/#/% type sigil) starting
// at i. Returns an empty string if there isn't one there.
ig::String readIdentifier(const ig::String& text, size_t& i)
{
    if (i >= text.size() || !isIdentStart(text[i])) return ig::String();
    size_t start = i;
    while (i < text.size() && isIdentChar(text[i])) ++i;
    return text.substr(start, i - start);
}

// Function declarations name the function right after the keyword: this
// also captures the return-type sigil ($/#/%) when the name itself doesn't
// already have one applied via isIdentChar, matching how the tests spell it
// (see funcs.bb: "Function Greet$(name$, ...)").
void scanFunctionLine(const ig::String& text, size_t i, int lineIndex, ct::Vector<Symbol>& out)
{
    i = skipSpaces(text, i);
    ig::String name = readIdentifier(text, i);
    if (name.empty()) return;
    { Symbol sym; sym.kind = SymbolKind::Function; sym.name = name; sym.line = lineIndex; out.push_back(sym); }
}

void scanTypeLine(const ig::String& text, size_t i, int lineIndex, ct::Vector<Symbol>& out)
{
    i = skipSpaces(text, i);
    ig::String name = readIdentifier(text, i);
    if (name.empty()) return;
    { Symbol sym; sym.kind = SymbolKind::Type; sym.name = name; sym.line = lineIndex; out.push_back(sym); }
}

// Global/Const lines can name several variables, each optionally followed
// by "= expr": "Global a, b$ = 1, c# = 2.0". Only the names are symbols;
// the expressions are skipped by scanning to the next top-level comma.
void scanVariableLine(const ig::String& text, size_t i, int lineIndex, SymbolKind kind,
                      ct::Vector<Symbol>& out)
{
    while (i < text.size())
    {
        i = skipSpaces(text, i);
        ig::String name = readIdentifier(text, i);
        if (name.empty()) break;
        { Symbol sym; sym.kind = kind; sym.name = name; sym.line = lineIndex; out.push_back(sym); }

        // Skip to the next comma at this "depth" (bracket/paren-aware, so an
        // array size or call in the initializer doesn't end the scan early).
        int depth = 0;
        while (i < text.size())
        {
            char c = text[i];
            if (c == '(' || c == '[') ++depth;
            else if (c == ')' || c == ']') --depth;
            else if (c == ',' && depth <= 0) { ++i; break; }
            else if (c == '\'') { i = text.size(); break; } // rest of line is a comment
            ++i;
        }
        if (i >= text.size()) break;
    }
}

} // anonymous namespace

ct::Vector<Symbol> scanSymbols(const ig::String& text)
{
    ct::Vector<Symbol> out;

    int lineIndex = 0;
    size_t lineStart = 0;
    const size_t n = text.size();
    for (size_t pos = 0; pos <= n; ++pos)
    {
        if (pos < n && text[pos] != '\n') continue;

        ig::String line = text.substr(lineStart, pos - lineStart);
        size_t i = skipSpaces(line, 0);

        if (i < line.size() && line[i] == '.') {
            ++i;
            ig::String name = readIdentifier(line,i);
            if (!name.empty()) { Symbol sym; sym.kind=SymbolKind::Label; sym.name=name; sym.line=lineIndex; out.push_back(sym); }
        }
        else if (matchesKeyword(line, i, "Function"))
            scanFunctionLine(line, i + 8, lineIndex, out);
        else if (matchesKeyword(line, i, "Type"))
            scanTypeLine(line, i + 4, lineIndex, out);
        else if (matchesKeyword(line, i, "Global"))
            scanVariableLine(line, i + 6, lineIndex, SymbolKind::Global, out);
        else if (matchesKeyword(line, i, "Const"))
            scanVariableLine(line, i + 5, lineIndex, SymbolKind::Const, out);

        lineStart = pos + 1;
        ++lineIndex;
    }

    return out;
}

} // namespace zed
