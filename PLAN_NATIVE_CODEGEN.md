# Native C++ Codegen Plan

## Goal

Add a native C++ backend for Windows, Linux and Web while keeping the current
bytecode and VM backend unchanged. The interpreter stays the default and stays
the reference implementation.

The native path generates the program's control flow directly in C++. It does
not generate bytecode and does not run the VM dispatch loop. It does reuse the
`libzen` runtime library (GC, strings, arrays, structs, command bindings) as an
ordinary library.

```text
program.bb -> parser -> AST -> semantic analysis -> CGen -> program.cpp -> C++ compiler -> executable
```

The existing path remains available and untouched:

```text
program.bb -> parser -> AST -> semantic analysis -> BBGen -> bytecode -> VM
```

This is the Haxe/hxcpp model: one shared typed front-end, one generator per
target, one runtime library per platform, `extern`-style metadata for native
bindings.

## Scope

Initial targets:

- Linux x86-64 (host C++ compiler)
- Windows x86-64 (MSVC or MinGW)
- Web (Emscripten, same generated C++)

Out of scope for the first version:

- Android
- ARM targets
- LLVM IR backend
- Direct assembly backend
- A JavaScript emitter (Web is served by compiling the C++ with Emscripten)

## Decisions

These were settled while reviewing the first draft:

1. **Emit C++, not C.** Destructors make GC rooting automatic (see Memory
   Model). Compile with `-fno-exceptions -fno-rtti`; no STL in generated code.
2. **`CGen` is an external walker over the typed AST.** It does not add
   virtual methods to the AST nodes. `BBGen` keeps its `emit()` virtuals as
   they are.
3. **Reuse `libzen` as the runtime.** No `zen_native` runtime written from
   scratch. The generated program links `libzen`, `engine` and SDL. Only the
   bytecode and the dispatch loop are excluded.
4. **Existing command adapters are reused** through generated typed wrappers.
   Rewriting the ~470 `c_X(VM*, Value*, int)` adapters is not required for
   the first release.
5. **Web = same `.cpp` + `emcc`.** Platform differences live in CMake and in
   the runtime, never in the generated source.
6. **The interpreter, `BBGen`, `Emitter`, `VM` and `memory.cpp` behaviour do
   not change.** The only `libzen` edits are two small hooks listed under
   "Required changes to libzen".
7. **Reuse the existing `__bb*` helpers for Types and Data/Read.** The
   helpers in `bb_runtime.cpp` already implement conversions, type instance
   lists, object allocation/deletion, arrays and Data/Read state. CGen calls
   them through typed wrappers instead of reimplementing their semantics.
8. **Commands are emitted uniformly from generated metadata.** The command
   signature table is the source of truth for name, return kind, parameters
   and defaults. CGen must not grow one handwritten branch per command; the
   generated wrappers own that mapping.
9. **The support header is the boundary.** `zen_codegen_support.hpp` contains
   only shared generated-program helpers. It is included by every emitted
   `.cpp`, while CGen emits program control flow and calls the support API.
   Platform/runtime bindings must be added behind that boundary rather than
   copied into each generated file.

## Compiler Architecture

Shared front-end, unchanged:

1. `Toker` reads source tokens.
2. `Parser` builds the AST.
3. Semantic analysis resolves names, types, defaults, includes and constants.
   After this pass every `ExprNode`/`VarNode` carries `sem_type`.
4. A selected backend emits either bytecode (`BBGen`) or C++ (`CGen`).

New files:

```text
libzen/src/c_codegen.h
libzen/src/c_codegen.cpp
native/include/zen_native.hpp      handles, program state, wrappers header
native/src/zen_native.cpp          root stack, runtime init/shutdown, Print, Data/Read
native/src/zen_platform_posix.cpp
native/src/zen_platform_win32.cpp
native/src/zen_platform_web.cpp
native/gen/zen_cmds.hpp            generated typed wrappers (see Command Bindings)
```

Interface:

```cpp
class CGen
{
public:
    CGen(std::ostream &out, const std::string &filename);
    void compile(bb::ProgNode *program);
};
```

`CGen` dispatches on node kind. Preferred: add a `NodeKind kind` field to
`Node` set by each constructor (one line per struct in `bb_nodes.h`, no
behaviour change). Fallback: `dynamic_cast`.

`CGen` must not include `emitter.h`, `bytecode.h`, `opcodes.h` or
`vm_dispatch`. It may include `object.h`/`memory.h` only through
`zen_native.hpp` names.

## Native Runtime

The native runtime is a thin layer over `libzen`:

- A `zen::VM` instance is created as **context only** (GC owner, adapter
  argument, backend table). `VM::run` is never called.
- `zen_native.hpp` defines the handle types and the program state.
- Platform files provide window/timer/file glue and the main loop.

Initial API:

```cpp
void zen_runtime_init(int argc, char **argv);
void zen_runtime_shutdown(void);
void zen_print(int64_t v);
void zen_print(double v);
void zen_print(const ZString &v);
void zen_end(void);                 // Blitz End: stop program, platform-defined
```

## Types

| BlitzBasic | Native C++ |
| --- | --- |
| `%` | `int64_t` |
| `#` | `double` |
| `$` | `ZString` (handle to `zen::ObjString`) |
| user `Type` | `ZObj<T>` (handle to a generated struct) |
| `Dim` array | `ZArray<T>` (handle to `zen::ObjArray`, typed accessors) |
| entity / handles | `int64_t` (as in Blitz: commands take and return ints) |

Integers and floats are plain C++ values. Handles are the only things the GC
sees. Generated code never spells `zen::Value` outside the wrappers.

User `Type`s become generated structs with a `zen::Obj` header and a
generated mark function:

```cpp
struct T_Player : zen::Obj
{
    int64_t x, y;
    ZString name;
    ZObj<T_Player> next;
};
static void mark_T_Player(zen::GC *gc, zen::Obj *o);
```

Field access is `p->x`, no lookup. Each `Type` also owns its instance list
(needed for `Each`, `First`, `Last`, `Insert`, `Delete Each`). That list is a
global root, so instances live until `Delete`, matching Blitz semantics.

The first native implementation reuses the existing `BBTypeInfo` state and
typed wrappers around `__bbNew`, `__bbDelete`, `__bbDeleteEach`, `__bbAfter`,
`__bbBefore`, `__bbInsBefore` and `__bbInsAfter`. Generated `OBJ_FOREIGN`
structs remain a later optimization after parity is established.

## Globals and Functions

Program state is explicit:

```cpp
struct ZenProgram
{
    int64_t score;
    double speed;
    ZString title;
    ZArray<int64_t> map;
};
static ZenProgram G;   // registered once as a GC root set
```

Functions are plain static C++ functions. Prototypes are emitted for all user
functions before any body, so call order does not matter.

```cpp
static int64_t f_add(int64_t a, int64_t b) { return a + b; }
```

`Goto`/`Gosub` map to C++ `goto`/labels; `Gosub` uses a small return stack in
`main`. `Data`/`Read`/`Restore` initially reuse the existing Data state and
typed wrappers for `__bbReadInt`, `__bbReadFloat`, `__bbReadStr` and
`__bbGosubPop`. A generated static table plus cursor is only needed if the
shared helper path proves too coupled to the VM context.

## Reusing `libzen` Helpers

`bb_codegen.h` shows that the bytecode backend already lowers several language
features through helpers registered by `bb_runtime.cpp`. The native backend
reuses those semantics at a typed boundary:

```text
CGen -> generated typed wrapper -> __bb* helper -> libzen runtime/GC state
```

This is not a call to `VM::run` or to the bytecode dispatch loop. The native
program owns a VM context for GC and adapter state, and wrappers marshal
arguments into the existing helper contract. The affected helpers include
`__bbFtoI`, `__bbStoI`, `__bbStoF`, `__bbFtoStr`, `__bbNew`, `__bbDelete`,
`__bbDeleteEach`, `__bbAfter`, `__bbBefore`, `__bbInsBefore`, `__bbInsAfter`,
`__bbDim`, `__bbReadInt`, `__bbReadFloat`, `__bbReadStr` and
`__bbGosubPop`.

## Command Bindings

The existing signature strings (`"#EntityX%entity%global=0"`) already encode
return and argument types. A generator (`tools/gen_native_cmds.py` or a small
C++ tool run at build time) reads every registered table and emits
`native/gen/zen_cmds.hpp`:

```cpp
inline double cmd_EntityX(int64_t entity, int64_t global = 0)
{
    zen::Value a[2] = { zen::int_val(entity), zen::int_val(global) };
    c_EntityX(zen_vm(), a, 2);
    return zen::as_float(a[0]);   // adapter return convention
}
```

Wrappers marshal only at the call boundary. Strings cross as `ObjString *`
from the same GC, so no copying.

Optional later stage: a `NativeDecl` table mapping a signature to a direct
native symbol for hot commands. When present, `CGen` calls the symbol instead
of the wrapper. Not required for the first release.

Metadata to add per command (in the same generator, not in the adapters):

- `suspends` flag for `Flip`, `Delay`, `WaitTimer`, `WaitKey`, `WaitMouse`
  (used by the Web target, see below).

## Memory Model

Reuse the tri-color mark & sweep GC in `libzen/src/memory.cpp`. What the
generated program must provide is **roots**.

Mechanism: a shadow root stack managed by handle destructors.

```cpp
class ZString
{
    zen::ObjString *p;
public:
    ZString(zen::ObjString *s) : p(s) { zen_root_push(&p); }
    ZString(const ZString &o) : p(o.p) { zen_root_push(&p); }
    ~ZString() { zen_root_pop(); }
    ...
};
```

- Every local, temporary and parameter of type `ZString`/`ZObj`/`ZArray` is
  rooted for exactly its C++ lifetime. Temporaries inside `a$ + b$ + c$` are
  covered by the language, not by `CGen`.
- Globals live in `ZenProgram`, registered once.
- `Type` instance lists are global roots.
- The root stack is LIFO, so push/pop is a pointer bump.

`CGen` therefore emits ordinary C++ and never emits frame bookkeeping.

Rejected alternatives, recorded for later:

- Conservative stack scanning (hxcpp/Boehm style): no generated code cost,
  but needs an O(1) "is this a heap object" test, does not work cleanly on
  Wasm, and produces rare hard-to-reproduce bugs.
- Pure refcounting (original Blitz3D): no GC needed, but forces a separate
  string/object runtime and breaks reuse of the adapters.

SDL and engine resources keep explicit destruction or engine-owned lifetime.
They must not rely on GC collection.

## Required changes to libzen

Both are additive and leave the interpreter's behaviour unchanged.

1. **Root marking hook.** `gc_collect` calls `vm->gc_mark_roots()` directly
   (`memory.cpp` around line 1084). Replace with a function pointer on `GC`
   (`gc->mark_roots(gc, gc->mark_roots_ud)`). The VM installs its current
   method; the native runtime installs its root-stack walker.
2. **Foreign object kind.** Add `OBJ_FOREIGN` to `ObjType` with a per-object
   or per-kind mark callback, so generated `Type` structs can be GC objects.
   Alternatively phase 1 uses `ObjStruct` with indexed fields and typed
   structs come in phase 2; decide when reaching milestone 8.

## Platforms and Web

The generated `.cpp` is identical for every target. Selection happens in
CMake and in the runtime, following the pattern already used by
`engine/src/core/Platform.cpp` (`#if defined(__EMSCRIPTEN__)`).

CMake per target:

- platform source file (`posix` / `win32` / `web`)
- compiler and flags
- Web only: `-sASYNCIFY` with the list of suspending commands generated from
  the `suspends` metadata, `-sALLOW_MEMORY_GROWTH`, `--preload-file` for
  assets, SDL2 port or vendored SDL, output `.html` + `.js` + `.wasm`
- Web only: dynamic user libraries disabled; static ones linked in

Runtime per target (`#ifdef` inside `zen_platform_*.cpp` only):

- `Flip`/`Delay`/`WaitTimer` yield to the browser instead of blocking
- files go through the Emscripten virtual FS; saves through IndexedDB
- `End` stops the main loop instead of calling `exit()`
- GL vs GLES already handled by the engine

Why Asyncify and not a state machine: the VM already suspends on
`Flip`/`Delay` (see `cli/host_loop.h`), but generated native loops cannot
return. Asyncify restores that ability without touching `CGen`. JSPI is the
zero-cost successor once browser support is broad; `-sPROXY_TO_PTHREAD` is
the fallback but needs COOP/COEP headers on the host site.

## User Libraries

Keep the current declarations and add native symbol metadata.

Dynamic loading:

- Windows: `LoadLibrary` and `GetProcAddress`.
- Linux: `dlopen` and `dlsym`.
- Web: not available; static only.

Static user libraries link into the final executable. Dynamic ones stay as
files beside the executable.

## CLI

First add source emission:

```bash
zenblitz --emit-cpp game.bb -o game.cpp
```

Then native compilation, selecting only toolchain and platform file:

```bash
zenblitz --build-native game.bb --target=linux   -o game
zenblitz --build-native game.bb --target=windows -o game.exe
zenblitz --build-native game.bb --target=web     -o game.html
```

Backend selection never changes the default:

```text
--backend=bytecode   current default
--backend=cpp        native C++ emission
```

## CMake

```cmake
option(ZEN_BUILD_CPP_BACKEND "Build native C++ backend and runtime" OFF)
```

The normal build must continue to work with the option OFF. The native
runtime, the wrapper generator and `c_codegen.cpp` build only when ON. No
LLVM dependency; the host C++ compiler (or `emcc`) is used.

## Runtime error parity

The VM raises Blitz runtime errors (division by zero, null object, array
index out of range, string to number failures). Generated code must emit the
same checks, otherwise the comparison tests below diverge. `CGen` emits
`zen_check_*` calls at the same points `BBGen` emits the checking opcodes.

## Tests

Every native test compares its output and exit code with the VM.

```text
run with VM -> capture output
run native executable -> capture output
compare output and exit code
```

Milestones:

1. `Print 2 + 3`
2. Variables and assignments
3. Arithmetic with integers and floats
4. `If` and `Else`
5. `While`, `For`, `Repeat`, `Exit`
6. User functions and `Return`
7. Strings (concat, compare, `Left$`, `Str$`, etc. through wrappers)
8. `Type`, `New`, `Delete`, `Each`, `First`, `Last`, `Insert` through
   `BBTypeInfo` and the existing `__bb*` helpers
9. `Dim` arrays
10. `Goto`, `Gosub`, `Data`/`Read`/`Restore` through existing Data state and
   `__bbRead*`/`__bbGosubPop` wrappers
11. Runtime error parity
12. Generated command wrappers (a sample across every table)
13. User libraries
14. SDL window lifecycle
15. Web build of milestones 1 to 14 (headless where possible)

## Implementation Order

1. Add `NodeKind` to `bb_nodes.h` (or settle on `dynamic_cast`).
2. Add `CGen` with integer and float constants and `Print`.
3. Add `zen_native.hpp` with `zen_runtime_init`, `zen_print`, VM context.
4. Add `--emit-cpp` to the CLI; compile and run the Linux smoke test.
5. Variables, expressions, assignments, control flow.
6. Functions and prototypes.
7. Root-marking hook in `memory.cpp`; `ZString` handle; string milestone.
8. Typed wrappers over `BBTypeInfo` and `__bbNew`/`__bbDelete`/list helpers;
   defer generated `OBJ_FOREIGN` structs until parity is established.
9. `Dim` arrays via `ZArray`.
10. Wrapper generator from signature tables; `suspends` metadata.
11. `Goto`/`Gosub`/`Data` using `__bbGosubPop` and existing Data/Read helpers;
   runtime error checks.
12. Platform files; SDL and engine wrappers; `--build-native` for Linux.
13. Windows build validation.
14. Web build with Emscripten and Asyncify.
15. User libraries (static, then dynamic on desktop).

## Success Criteria

First milestone:

```text
zenblitz --emit-cpp print.bb -o print.cpp
c++ -fno-exceptions -fno-rtti print.cpp -lzen_native -lzen ... -o print
./print
```

produces the same output and exit code as the VM, without loading bytecode
and without running the VM dispatch loop.

Release milestone: milestones 1 to 14 pass on Linux, Windows and Web from the
same generated source, with the interpreter build and its tests unchanged.
