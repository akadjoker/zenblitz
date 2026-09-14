# Survey: engine logic living in the bindings

Rule: engine code lives in `engine/`; a binding in `runtime3d/src/bb_cmds_*.cpp`
only converts `Value`s, looks up handles, and does the VM-only parts (runtime
errors, suspension, backend log). Everything else below moves.

Measured on `engine/marco-3-3d` (72b06d3). 381 adapters: 345 are 12 lines or
fewer, 36 carry logic; 2300 lines of helpers sit outside the adapters.

Legend: **move** = body goes to the engine as a method/function, binding
becomes a call; **use** = the engine already has it, the binding should call
it; **stays** = Blitz-only state or glue, correct where it is.

## bb_cmds_world.cpp (1541 lines, 109 adapters)

| what | lines | verdict | destination |
| --- | --- | --- | --- |
| `c_LoadMesh` + `collapse_mesh` + `lowerExt`: pick loader by extension, 3DS axis conversion, collapse hierarchy into one MeshModel | 63+9+8 | move | new `engine/MeshFile.h`: `Entity *MeshFile::loadMesh(const char *file, gpu::Device *)` |
| `c_LoadAnimMesh`: same dispatch, first-frame animate | 36 | move | `Entity *MeshFile::loadAnimMesh(file, dev)` |
| `c_LoadAnimSeq`: load B3D anim-only, `addSeqs` | 14 | move | `int MeshFile::loadAnimSeq(Animator *, file, dev)` |
| `c_LoadMD2` | 16 | move | `MeshFile::loadMD2(file)` (or fold into loadMesh) |
| `c_LoadTerrain`: heightmap pixmap -> heights, power-of-two check | 18 | move | `static Terrain *Terrain::loadHeightmap(const char *file)` |
| `terrain_vector`: bilinear surface point under a world point | 8 | move | `Vector Terrain::surfacePoint(const Vector &world) const` |
| `c_EntityParent`: reparent keeping world tform / resetting motion | 17 | move | `void Entity::reparent(Entity *parent, bool global)` |
| `reset_entity_tree`: force visible+enabled+reset on a subtree | 6 | move | `void Entity::resetTree()`; `insert_entity` stays (parent + resetTree) |
| `c_EntityInView`: box corners through inverse camera tform, frustum cull | 21 | move | `bool Camera::entityInView(Entity *e)` |
| `c_CameraProject`: view-space point -> viewport pixel, ortho/persp | 27 | move | `bool Camera::project(const Vector &world, Vector &out)`; `g_projected` stays (ProjectedX/Y/Z read it) |
| `c_CameraPick`: viewport pixel -> world pick line | 17 | move | `Line Camera::pickLine(float x, float y)`; `doPick` -> `Object *World::pick(const Line &, float radius, ObjCollision &out)`; last-pick state stays |
| `c_CopyEntity` and the rest | <=12 | stays | marshalling |
| `g_entities`/`g_handles`/`store_entity`/`handle_of`/`free_all_entities` | 30 | stays | handle registry |

## bb_cmds_image.cpp (707 lines, 31 adapters)

| what | lines | verdict | destination |
| --- | --- | --- | --- |
| `tform_frame` + `sample_filtered`: 2x2 affine resample with bilinear filter, new handle | 60+23 | move | `bool Image::transform(const float m[2][2], bool filter)`; `Color Pixmap::sample_bilinear(float x, float y) const` |
| `c_TFormImage` (loop over frames keeping the handle) | 14 | move | inside `Image::transform` |
| `apply_mask_alpha` + `ensure_rgba` + `c_MaskImage` | 11+6+19 | move | `void Pixmap::mask_alpha(u8 r, u8 g, u8 b)`; `void Image::applyMask()` rebuilds alpha from the recorded mask on every frame |
| `c_LoadAnimImage`: sheet -> cells | 22 | move | `static Image *Image::loadAnim(file, cellW, cellH, first, count)` (Texture has `loadAnim` already; same shape) |
| `c_CopyImage`: clone frames, handle, mask | 20 | move | `Image *Image::clone() const` |
| `draw_frame`: upload, normalised source rect, solid blend | 16 | move | `void Image::draw(Platform &, float x, float y, int frame, float sx, float sy, float sw, float sh, bool solid)` |
| `tile_cmd`: wrap-around tiling over the viewport | 22 | move | `void Image::drawTiled(Platform &, int x, int y, int frame, bool solid)` |
| `frame->dirty = true; cmMask.clear(); cmPitch = 0` repeated 5 times across files | - | move | `void ImageFrame::touch()` |
| `g_images`/`store_image`/`g_autoMidHandle`/`g_tformFilter`/buffer-handle encoding | 40 | stays | registry and Blitz flags |

## bb_cmds_buffer.cpp (471 lines, 20 adapters)

| what | lines | verdict | destination |
| --- | --- | --- | --- |
| `c_CopyRect`: overlap-safe rect copy between buffers | 22 | move | `void Pixmap::copy_rect(const Pixmap &src, int sx, int sy, int w, int h, int dx, int dy)` (temp copy for self-overlap); screen source through `Platform::grabScreen` |
| `c_GrabImage`: screen/buffer rect -> image frame | 25 | move | `bool Platform::grabScreen(int x, int y, Pixmap &out)` + `ImageFrame::touch()` |
| `c_SaveBuffer` (screen path) | 20 | move | `Platform::grabScreen` + `Pixmap::save` |
| `c_LoadBuffer`: load + nearest scale into the buffer | 19 | use | `Pixmap::load` + existing `Pixmap::resize` |
| `read_pixel`/`write_pixel`/`buffer_size` | 29 | stays | choose screen vs ImageFrame (Blitz buffer model); pixel access is already Pixmap/Platform |
| `c_Viewport` clamp, `CanvasRegion`, `apply_clip`, origin | 40 | later | becomes `engine::Canvas` (see below) |

## bb_cmds_texture.cpp (482 lines, 28 adapters)

| what | lines | verdict | destination |
| --- | --- | --- | --- |
| atlas maths in `c_LoadMaterial`, `c_EmitterTexture`, `c_VoxelSpriteTexture` (three copies) | 32+15+17 | move | `struct AtlasLayout { int columns, rows, first, count; static AtlasLayout compute(const Texture *, int frameW, int frameH, int first, int count); }` in `engine/Texture.h` |
| `struct VoxelMaterial` | 6 | move | `engine/VoxelSprite.h`; the handle map stays |
| `c_LoadBrush`: texture + scale + brush | 18 | move | `static Brush *Brush::load(file, flags, float uScale, float vScale, gpu::Device &)`; `g_brushes` stays |
| `c_EntityTexture` | 16 | stays | 3 lines of logic, rest is error + lookup |
| `g_textures`/`store_texture`/`free_all_textures`/`warn_load_failed` | 25 | stays | registry |

## bb_cmds_sprite.cpp (137 lines, 6 adapters)

| what | lines | verdict | destination |
| --- | --- | --- | --- |
| `c_LoadSprite`: TextureCache + blend from flags + fullbright | 21 | move | `static Sprite *Sprite::load(file, flags, gpu::Device &)` |

## bb_cmds_graphics.cpp (518 lines, 26 adapters)

| what | lines | verdict | destination |
| --- | --- | --- | --- |
| `c_WaitKey`, `c_MouseWait`, `c_VWait`, `vblank_sleep` (`SDL_Delay`, `SDL_GetCurrentDisplayMode` in a binding) | 13+20+10+9 | move | `int Platform::waitKey()`, `int Platform::waitMouse()`, `void Platform::vwait(int frames)` |
| `collectGfxModes`/`gfx_mode_at` (`SDL_GetDisplayMode` in a binding) | 24 | move | `int Device::displayModeCount(int display)`, `bool Device::displayMode(int display, int i, int &w, int &h, int &depth)`; the cached `g_gfxModes` list stays (Blitz 1-based numbering) |
| `profile_dump`: formats engine stats once a second | 47 | move | `bool Platform::profileLine(char *buf, int cap)` (returns false until a second passed); the env check stays |
| `c_Graphics` mode table (0/1/2/3/6/7) | 21 | stays | Blitz mode numbers |
| `shutdown_graphics`, `g_platforms`/`g_worlds` | 30 | stays | per-VM ownership |

## bb_cmds_gfxmode.cpp (278 lines, 18 adapters)

| what | lines | verdict | destination |
| --- | --- | --- | --- |
| `video_ready`, `driver_count`, `c_GfxDriverName` (`SDL_GetNumVideoDisplays`, `SDL_GetDisplayName`) | 12 | move | `int Device::displayCount()`, `const char *Device::displayName(int)` |
| `c_UpdateGamma` (`SDL_SetWindowGammaRamp`, `SDL_GL_GetCurrentWindow`) | 8 | move | `void Device::setGammaRamp(const Uint16 *r, *g, *b)`; the ramp arrays stay (Blitz SetGamma state) |
| `c_GfxModeExists` | 13 | stays | loop over the cached list |

## bb_cmds_input.cpp (197 lines, 17 adapters)

| what | lines | verdict | destination |
| --- | --- | --- | --- |
| `g_lastX/Y/Z` + `c_MouseXSpeed/YSpeed/ZSpeed` + `MoveMouse` resync | 20 | move | `int Platform::mouseXSpeed()` etc.; Platform already owns the mouse state |

## bb_cmds_collision.cpp, entityquery, particle, meshutil, sound, surface

| what | lines | verdict | destination |
| --- | --- | --- | --- |
| `c_AlignToVector` (entityquery) | 24 | move | `void Entity::alignToVector(const Vector &v, int axis, float rate)` |
| `c_EntityCollided` (collision) | 13 | move | `Object *Object::collidedWith(int type) const` |
| `setTypeRecursive` (collision) | 3 | move | `void Object::setCollisionTypeTree(int type)` |
| `c_TFormPoint`/`g_tformed` (entityquery) | 8 | stays | one line of maths, Blitz last-result state |
| everything else | <=12 | stays | marshalling |

## bb_cmds_canvas.cpp + the buffer/region state: `engine::Canvas` (separate step)

`g_canvasBuffers`, `g_canvasColors`, `g_fontSize`, `Bb3dFont`, `CanvasRegion`
(origin, viewport, clip) and the console (`bb_cmds_console.cpp` on
`engine/console-window`: cursor, scroll, Input$ line editor) are Blitz3D's
`gxCanvas` state. By the rule they belong in an `engine::Canvas` class: the
current target (screen or ImageFrame), colour, font, origin, viewport, text
cursor, and the Print/Write/Input operations. This is a design step of its
own, after the mechanical moves above, and it is also what `zenblitzsdk`
would expose as `zbSetBuffer`/`zbColor`/`zbPrint`.

## What stays in the bindings by design

- `arg_int`/`arg_float`/`arg_cstr` and `val_*` packing.
- Handle registries and their `free_all_*` (entities, images, textures,
  fonts, brushes, voxel materials, sounds) and the `Handle`/`Object` GC
  glue.
- `vm->runtime_error`, `vm->request_suspend` (Flip/Delay), backend log.
- Blitz "last result" state: `g_projected`, `g_tformed`, the last pick, the
  gamma ramp, the cached gfx-mode list with its 1-based indices.
- Blitz numbering tables: Graphics modes, driver indices.

## Order of work

Each group builds, and `ctest` in `build-3d` passes, before the next.
Branch `engine/thin-bindings` (worktree `zenblitz-thin/`).

1. **World / camera / entity**: MeshFile, Terrain, Entity::reparent/
   resetTree/alignToVector, Camera::entityInView/project/pickLine,
   World::pick, Object::collidedWith/setCollisionTypeTree.
2. **Images / pixmaps**: ImageFrame::touch, Pixmap::mask_alpha/
   sample_bilinear/copy_rect, Image::transform/applyMask/loadAnim/clone/
   draw/drawTiled, Platform::grabScreen.
3. **Textures / sprites**: AtlasLayout, VoxelMaterial, Brush::load,
   Sprite::load.
4. **Platform / device**: waitKey/waitMouse/vwait, display modes and
   names, gamma ramp, mouse speeds, profileLine. After this no binding
   includes `<SDL2/SDL.h>` (only `SDL_getenv` for the two env flags, or
   `Platform::envFlag`).
5. **engine::Canvas** (design first).

## Tests

No 3D headless test exists on `marco-3-3d` yet. Each group adds a hidden-
window test in `runtime3d/tests/` in the style of `console_test.cpp`
(ZENBLITZ_HIDDEN, compile a .bb in-process, check through ReadPixel or
printed values): group 1 loads a real `.b3d` from the reference models and
checks `EntityInView`/`CameraProject` numbers; group 2 checks `TFormImage`,
`MaskImage`, `CopyRect`, `GrabImage` pixels; group 3 checks the atlas
counts; group 4 pushes SDL events for `WaitKey`/`MouseWait`.
