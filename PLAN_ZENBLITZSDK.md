# zenblitzsdk Plan

## Goal

Split the Blitz3D runtime out of zenblitz into `zenblitzsdk`: a pure C++
library (static or shared) with a flat C API, usable from any language and
any host, with no knowledge of the Zen VM. zenblitz keeps the compiler, the
VM, the CLI, the editor and the native codegen, and consumes the SDK.

This is the layering the original had: `bbruntime.dll` and `blitz3d.dll`
exported the command set by symbol; the compiler and the IDE were separate.

```text
zenblitzsdk                      zenblitz
-----------                      --------
engine/        (C++ classes)     libzen/     compiler + VM
sdk/           (zb* C API)  <--  runtime3d/  generated Value adapters over zb*
extern/GPU, SDL, containers      codegen/    native backend calls zb* directly
                                 editor/, cli/, tests/, Samples/
```

## What the numbers say today

Measured on `runtime3d/src/bb_cmds_*.cpp` (branch `engine/marco-3-3d`):

| | lines |
| --- | --- |
| 381 command adapters `c_X(VM*, Value*, int)` | ~3600 |
| helpers outside adapters (handle registries, loaders, canvas state, gfx modes, console) | ~2300 |
| signature tables | ~475 |

345 of the 381 adapters are 12 lines or fewer and are marshalling around one
engine call. 36 carry real logic (`Input`, `LoadMesh`, `LoadAnimMesh`,
`LoadMaterial`, `CameraProject`, `GrabImage`, `CopyRect`, ...). The 2300
helper lines are the Blitz semantics the generic engine does not have.

What the adapters take from the VM:

| use | count | in the SDK |
| --- | --- | --- |
| `vm->backend()` (print, log, files) | 13 | SDK hooks (`zbSetPrintHandler`, ...) |
| `vm->runtime_error()` | 10 | `zbSetErrorHandler` + `zbLastError` |
| `vm->make_string()` | 4 | `const char *` valid until the next call |
| `vm->request_suspend()` | 2 | stays in the VM adapter (`Flip`, `Delay`) |
| `vm->get_gc()` | 1 | stays in the VM adapter |

Per-VM state, all `ct::HashMap<VM *, ...>`:

```text
bb_cmds_graphics.cpp   g_platforms, g_worlds
bb_cmds_canvas.cpp     g_canvasBuffers, g_canvasColors, g_fontSize
bb_cmds_buffer.cpp     g_regions
bb_cmds_world.cpp      g_picks
bb_cmds_console.cpp    g_consoles
```

These eight maps become the fields of one context struct. Everything else
in the adapters is already C-shaped: integers, floats, C strings, integer
handles.

## Decisions

1. **The SDK is the Blitz semantics layer, not the engine.** `engine/`
   stays generic C++ and does not learn about handles, the current buffer or
   the console. `sdk/` is today's `bb_cmds_*.cpp` with the `Value` boundary
   peeled off. Nothing moves down into `engine/`.
2. **Flat C API, `extern "C"`, one function per Blitz command**, named
   `zb<Command>` with the command's own parameter order and defaults
   spelled out (C has none): `zbCreateCube(int parent)`,
   `zbEntityX(int entity, int global)`. The signature tables are the source
   of truth; the header is generated from them.
3. **Implicit global context by default, explicit context available.**
   Blitz3D was single-context. `zbInit()` creates the default context;
   `zbCreateContext()`/`zbMakeCurrent()` exist for hosts that want more
   than one (the editor's preview, tests). Every `zb*` call acts on the
   current context.
4. **Suspension stays in the VM.** `zbFlip` presents and pumps events;
   the VM adapter adds `request_suspend`. `zbWindowClosed()` reports state;
   the host decides what "End" means (the VM ends the program, a C host
   returns from its loop).
5. **Errors are a handler, not an exception.** `zbSetErrorHandler(fn, ud)`;
   default handler logs and continues. The VM adapter installs one that
   calls `vm->runtime_error`. `zbLastError()` for hosts that poll.
6. **Strings cross as `const char *`**, valid until the next SDK call, as
   `Backend::current_dir` already does. No allocation crosses the boundary.
7. **The C header is the only contract.** No C++ type appears in
   `zenblitzsdk.h`. The C++ engine classes are available to C++ users through
   `engine/include`, but nothing in zenblitz depends on that.
8. **Migrate inside the zenblitz repository first, extract second.** The
   engine changes daily on `engine/marco-3-3d`; two repositories in manual
   sync during the migration is where the time would go. The split happens
   once `sdk/` builds standalone.
9. **The native codegen consumes the SDK directly.** Generated programs
   call `zb*`; the `c_X` wrappers and `zen_codegen_window.hpp` in the codegen
   plan go away. `zenblitzsdk.h` becomes the metadata source for that
   backend too (see PLAN_NATIVE_CODEGEN.md, "Command Bindings").
10. **Headless verification stays as it is.** `ZENBLITZ_HIDDEN` and the
    read-back tests (runtime3d/tests/console_test.cpp) move with the code.
    No test opens a visible window.

## Layout (inside zenblitz during the migration)

```text
sdk/
  CMakeLists.txt              builds zenblitzsdk alone: engine + sdk + GPU + SDL, no libzen
  include/zenblitzsdk.h       generated: the C API
  include/zenblitzsdk_hooks.h hand-written: context, hooks, error handler, version
  src/zb_context.cpp          zbContext, zbInit/zbShutdown, current context
  src/zb_graphics.cpp         from bb_cmds_graphics.cpp
  src/zb_canvas.cpp           from bb_cmds_canvas.cpp
  src/zb_buffer.cpp           from bb_cmds_buffer.cpp
  src/zb_image.cpp            ...
  src/zb_input.cpp
  src/zb_gfxmode.cpp
  src/zb_world.cpp
  src/zb_entityquery.cpp
  src/zb_texture.cpp
  src/zb_collision.cpp
  src/zb_meshutil.cpp
  src/zb_surface.cpp
  src/zb_sprite.cpp
  src/zb_particle.cpp
  src/zb_sound.cpp
  src/zb_console.cpp          from bb_cmds_console.cpp
  tools/gen_sdk.py            signature tables -> zenblitzsdk.h + VM adapters
  tests/                      headless C tests (hidden window, pixel read-back)
  examples/cube.c             the standalone proof: window, cube, N flips, exit
runtime3d/
  src/gen/bb_cmds_*.cpp       generated Value adapters: unpack, call zb*, pack
  src/main3d.cpp              unchanged in spirit
```

## The context

```c
typedef struct zbContext zbContext;

zbContext *zbCreateContext(void);
void       zbDestroyContext(zbContext *ctx);
void       zbMakeCurrent(zbContext *ctx);
zbContext *zbCurrentContext(void);

int  zbInit(void);          /* default context, made current */
void zbShutdown(void);      /* what shutdown_graphics() does today */
```

`struct zbContext` (C++ side) holds what the eight maps hold today:

```cpp
struct zbContext
{
    engine::Platform *platform;
    engine::World *world;
    long long canvasBuffer;      unsigned canvasColor;   float fontSize;
    CanvasRegion region;
    engine::ObjCollision pick;
    Console console;
    zbErrorHandler onError;  void *errorUd;
    zbPrintHandler onPrint;  void *printUd;
    zbLogHandler   onLog;    void *logUd;
    char stringResult[...];  /* zbEntityName etc. */
};
```

Handle registries (`entity_of`/`store_entity`, images, textures, fonts,
brushes, sounds) are per-context too, because `zbShutdown` frees them.
Today they are process-global; moving them into the context is the one
change that touches every file, and it is done first.

## Hooks

```c
typedef void (*zbErrorHandler)(const char *message, void *ud);
typedef void (*zbPrintHandler)(const char *text, int length, void *ud);
typedef void (*zbLogHandler)(int level, const char *message, void *ud);

void zbSetErrorHandler(zbErrorHandler fn, void *ud);
void zbSetPrintHandler(zbPrintHandler fn, void *ud);
void zbSetLogHandler(zbLogHandler fn, void *ud);
const char *zbLastError(void);
```

File I/O inside the SDK (loaders) keeps using `SDL_RWops`, as the engine
does today; the Blitz file commands (`OpenFile`, `ReadLine`, ...) are libzen's
and are not part of the SDK.

## Generated API

`tools/gen_sdk.py` reads the same `{"sig", fn}` tables `gen_native_cmds.py`
already parses and emits:

1. `zenblitzsdk.h`: one prototype per command, with a comment carrying the
   Blitz signature and defaults.

   ```c
   /* PositionEntity%entity#x#y#z%global=0 */
   ZB_API void  zbPositionEntity(int entity, float x, float y, float z, int global);
   /* $EntityName%entity */
   ZB_API const char *zbEntityName(int entity);
   ```

2. `runtime3d/src/gen/bb_cmds_<family>.cpp`: the VM adapters.

   ```cpp
   static int c_PositionEntity(VM *vm, Value *args, int nargs)
   {
       (void)vm; (void)nargs;
       zbPositionEntity((int)arg_int(args[0]), arg_float(args[1]), arg_float(args[2]),
                        arg_float(args[3]), (int)arg_int(args[4]));
       return 0;
   }
   ```

   Commands that need the VM (`Flip`, `Delay`, `WaitKey` for suspension;
   `Handle`/`Object` for the GC) are listed by name in the generator and
   keep a hand-written adapter that calls the `zb*` function and then does
   the VM part.

3. The `zen_native_cmds.hpp` table for the codegen, now pointing at `zb*`
   symbols that exist.

Type mapping across the C boundary:

| Blitz | C |
| --- | --- |
| `%` | `int` (handles, flags, counts); `long long` where the VM semantics need 64 bits (`MilliSecs`) |
| `#` | `float` |
| `$` in | `const char *` |
| `$` out | `const char *`, valid until the next call on the context |

`ZB_API` is `__declspec(dllexport)`/`dllimport` on Windows and
`__attribute__((visibility("default")))` elsewhere; the library is built
with `-fvisibility=hidden`.

## Migration order

Each step leaves `zenblitz3d` building and the tests passing.

1. **Context.** Add `sdk/src/zb_context.cpp` and `zbContext`. Replace the
   eight `HashMap<VM *, ...>` maps and the process-global handle registries
   with context fields. `runtime3d` maps `VM *` to `zbContext *` in one
   place (`context_for(vm)`) and calls `zbMakeCurrent` before dispatching
   into a command. Nothing else changes yet.
2. **Hooks.** Route `vm->backend()` uses through the print/log hooks and
   `vm->runtime_error` through the error handler; the VM adapter installs
   handlers that forward to the VM. Tests: the console test's captured
   output now comes through `zbSetPrintHandler`.
3. **Generator.** Write `tools/gen_sdk.py` and generate `zenblitzsdk.h`
   from the current tables. Nothing implements it yet; the header is a
   build artefact only.
4. **Convert one family end to end** (`bb_cmds_meshutil.cpp`, 10 adapters,
   57 helper lines): rename to `sdk/src/zb_meshutil.cpp`, turn each
   `c_X` into `zbX` with typed parameters, generate the adapter file,
   delete the hand-written one. This fixes the pattern and the generator.
5. **Convert the rest**, one file per step, largest last: input, sound,
   sprite, surface, collision, entityquery, particle, gfxmode, canvas,
   buffer, texture, image, graphics, console, world. The 36 adapters with
   logic keep their bodies; only the signature changes.
6. **Standalone build.** `sdk/CMakeLists.txt` builds `zenblitzsdk` (static
   and shared) with no libzen in the include path. `examples/cube.c`
   compiles against `zenblitzsdk.h` only and runs hidden for N frames in
   the test suite.
7. **Codegen switch.** The native backend emits `zb*` calls from the
   generated table; `zen_codegen_window.hpp` and the `c_X` wrappers are
   removed from PLAN_NATIVE_CODEGEN.md's scope.
8. **Extract.** `git subtree split` on `sdk/`, `engine/` and the three
   `extern/` submodules into the `zenblitzsdk` repository, keeping history.
   zenblitz takes it back as a submodule; `runtime3d/CMakeLists.txt` links
   `zenblitzsdk` instead of `zenblitz_engine`.

Steps 1 to 3 touch every file and are done on a branch of their own, with
nothing else going on in `runtime3d/` meanwhile. Steps 4 and 5 can overlap
with engine work on `engine/marco-3-3d` (a second Claude session in its own
worktree, as the console work was done): a file converted on the SDK branch
and edited on the engine branch conflicts on that file only, so convert a
family only when nothing is pending on it.

## Platforms

- Linux: static and shared. SDL2 static inside the shared library by
  default, so a consumer ships one file.
- Windows: MSVC and MinGW; `extern "C"` avoids mangling questions. Same
  8 MB stack note as the CLI does not apply (no compiler in the SDK).
- Web: Emscripten builds the static library as is; the shared variant is
  not offered.
- Android: not in this plan (see PLAN_NATIVE_CODEGEN.md's scope).

## Tests

- Every headless test in `runtime3d/tests` moves to `sdk/tests` and runs
  against the C API (hidden window, `zbReadPixel`, pushed SDL events).
- `zen_snapshot_suite` is unchanged: it runs the console CLI, which never
  links the SDK.
- A parity test: for every command in the tables, the generated adapter
  compiles and the `zb*` symbol resolves (a link test of the generated
  adapter file is enough).
- `examples/cube.c` in the suite, hidden, fixed frame count, exits by
  itself.

## Out of scope for the first release

- A C++ convenience wrapper over the C API.
- Bindings for other languages (the C header is what they bind to).
- Moving Blitz file, bank and stream commands (libzen's) into the SDK.
- Multiple simultaneous windows per context.

## Success criteria

1. `cmake -S sdk -B build-sdk && cmake --build build-sdk` produces
   `libzenblitzsdk.a` and `libzenblitzsdk.so` with no libzen source in the
   tree.
2. `examples/cube.c` builds against `zenblitzsdk.h` alone and passes hidden.
3. `zenblitz3d` links the SDK, and every existing test passes unchanged.
4. `runtime3d/src` contains no hand-written adapter except the suspension
   and GC ones listed in the generator.
5. The `zenblitzsdk` repository exists with history, and zenblitz builds
   with it as a submodule.
