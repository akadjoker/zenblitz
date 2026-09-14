# zenblitz editor

Classic BlitzIDE layout on iGUI/SDL2: original 16px toolbar icons, blue source pane, monospaced text, funcs/types/labels navigation and Row/Col status. Output appears after running a program. The right panel hides below 500px to preserve editing space.

Build from the repository root:

```sh
cmake -B build -DZEN_BUILD_EDITOR=ON
cmake --build build --target zenblitz-editor -j4
cd bin
./zenblitz-editor ../tutorials/basic_tuts/types1.bb
```

Run from `bin` so the editor can find the sibling `zenblitz3d` executable. Programs run in a child process: stdout/stderr appears progressively in Output, and F5 stops the active program while it is running. This is a visual adaptation, not a complete port of MFC BlitzIDE (debugger, executable publishing and integrated help are not implemented).

Toolbar: New, Open, Save, Close, Cut, Copy, Paste, Find and Run. The Program menu also provides Build native and Build and run native: it emits `<script>.native.cpp`, compiles `<script>.native` next to the source, and runs it from the script directory so relative assets resolve normally. Shortcuts: Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+F4, Ctrl+F and F5. Closing a modified tab asks whether to save, discard or cancel.

Focused regressions (from repository root):

```sh
cmake --build build --target zenblitz-editor-tests -j4
./build/editor/zenblitz-editor-tests
```

Tests cover available panel height, Blitz comments, symbol categories, SDL shortcut translation and opening/closing documents. See `assets/README.md` for asset provenance.
