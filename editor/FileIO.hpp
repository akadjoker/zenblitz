#pragma once

// Small, dependency-free file read/write helpers shared by the editor
// classes (Editor's New/Save, Runner's saved-script-before-running step).

#include <igui/Types.hpp>

namespace zed
{

// Reads an entire file into out. Returns false (leaving out untouched) when
// the file can't be opened - the caller decides whether that's an error
// (Open) or fine (first launch with no file yet).
bool readFile(const char* path, ig::String& out);

bool writeFile(const char* path, const ig::String& text);

} // namespace zed
