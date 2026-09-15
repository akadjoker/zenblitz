#ifndef ZEN_ENGINE_SDK_H
#define ZEN_ENGINE_SDK_H

#include "engine/Platform.h"
#include "engine/BlitzKeys.h"
#include "engine/World.h"
#include "engine/MeshUtil.h"
#include "engine/Camera.h"
#include "engine/Light.h"
#include "engine/Entity.h"
#include "engine/Object.h"
#include "engine/Brush.h"
#include "engine/Texture.h"
#include "engine/Geom.h"
#include "engine/MeshModel.h"
#include "engine/PlaneModel.h"
#include "engine/MeshLoader.h"
#include "engine/LoaderB3D.h"
#include "engine/LoaderB3DS.h"
#include "engine/LoaderX.h"
#include "engine/LoaderGltf.h"
#include "engine/LoaderFbx.h"
#include "engine/MD2Model.h"
#include "engine/Mirror.h"
#include "engine/Surface.h"
#include "engine/Animator.h"
#include "engine/Pixmap.h"
#include "engine/Image.h"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_video.h>

/* ================= Engine-backed Blitz graphics =================
** Wraps engine::Platform (Device + Graphics + Batch) so generated
** C++ programs use the real engine for Graphics/Flip/Cls/Color/Line/
** Rect/Oval/Plot/Text/KeyDown/KeyHit/MouseX/MouseY/MouseDown/MilliSecs.
** No raw SDL calls, no second renderer — the same engine the runtime3d
** path uses. */

static engine::Platform z_platform;
static engine::World z_world;
static bool z_graphics_open = false;
static bool z_world_init = false;
static std::map<int64_t, engine::Entity *> z_entities;
static int64_t z_next_entity = 0;
/* Back/front buffer selection: the engine has no software double buffer, so
   nothing reads this yet, but programs that never touch it must still compile
   under -Wall -Wextra -Werror. */
static int64_t __attribute__((unused)) z_draw_buffer = 1;

/* Buffers. Blitz3D hands a buffer around as one integer: positive values are
   the screen - kScreenBuffer is 1, the same value runtime3d uses - while an
   image or a texture canvas gets a negative handle that packs the resource
   and its frame. Same encoding as runtime3d's image_buffer() reads. */
static std::map<int64_t, engine::Image *> z_images;
static std::map<int64_t, engine::Texture *> z_textures;
static int64_t z_next_image = 0;
static int64_t z_canvas_buffer = 1;

/* The size text is drawn at: LoadFont makes one of these and SetFont picks
   it, the point size being all this runtime's single bitmap font needs.
   The "_state" suffix matters: the generator names a script's own `font`
   variable z_font, and this header is compiled into the same translation
   unit as that code. */
struct z_font_state { float size; };
static float z_font_size = 16.0f;
static unsigned z_draw_color = 0xff000000u;
static int z_origin_x = 0, z_origin_y = 0;
static int z_viewport_x = 0, z_viewport_y = 0, z_viewport_width = 0, z_viewport_height = 0;
static unsigned z_rand_seed_value = 1;
struct z_gfx_mode { int width; int height; int depth; };
static std::vector<z_gfx_mode> z_gfx_modes;

static inline void z_reset_canvas_region()
{
    z_origin_x = z_origin_y = 0;
    z_viewport_x = z_viewport_y = z_viewport_width = z_viewport_height = 0;
    if (z_graphics_open) z_platform.batch().clearClipRect();
}

static inline bool z_in_viewport(int x, int y)
{
    if (z_viewport_width <= 0 || z_viewport_height <= 0) return true;
    return x >= z_viewport_x && y >= z_viewport_y && x < z_viewport_x + z_viewport_width &&
           y < z_viewport_y + z_viewport_height;
}

static inline void z_world_ensure()
{
    if (!z_world_init)
    {
        z_world.init(z_platform.device(), z_platform.shaderDialect());
        z_world_init = true;
    }
}

static inline engine::Entity *z_entity(int64_t h)
{
    auto it = z_entities.find(h);
    return it != z_entities.end() ? it->second : nullptr;
}

static inline int64_t z_store_entity(engine::Entity *e, engine::Entity *parent = nullptr)
{
    if (parent) e->setParent(parent);
    e->setVisible(true);
    e->setEnabled(true);
    if (engine::Object *o = e->getObject()) o->reset();
    int64_t h = ++z_next_entity;
    z_entities[h] = e;
    return h;
}

/* Buffer handles and the frame they resolve to - see the state block. */
static inline int64_t z_buffer_handle(int64_t resource, int frame, int kind)
{
    return -((((resource * 256) + frame) << 1) | kind) - 1;
}

static inline engine::Image *z_image_of(int64_t handle)
{
    auto it = z_images.find(handle);
    return it != z_images.end() ? it->second : nullptr;
}

static inline engine::Texture *z_texture(int64_t handle)
{
    auto it = z_textures.find(handle);
    return it != z_textures.end() ? it->second : nullptr;
}

static inline engine::ImageFrame *z_buffer_frame(int64_t handle)
{
    if (handle >= 0) return nullptr;                    /* the screen */
    const int64_t encoded = -handle - 1;
    const int kind = (int)(encoded & 1);
    const int64_t rest = encoded >> 1;
    const int64_t resource = rest / 256;
    const int frame = (int)(rest % 256);
    if (kind == 0)
    {
        engine::Image *image = z_image_of(resource);
        return image && frame >= 0 && frame < image->frameCount() ? &image->frame(frame) : nullptr;
    }
    engine::Texture *texture = z_texture(resource);
    return texture ? texture->canvas(frame) : nullptr;
}

/* The frame the 2D commands are painting into; null means the screen, which
   goes through the batch renderer instead of a pixmap. */
static inline engine::ImageFrame *z_canvas_target() { return z_buffer_frame(z_canvas_buffer); }

static constexpr float kDegToRad = 0.0174532925199432957692369076848861f;

inline void zen_graphics_open(int width, int height, int64_t depth = 0, int64_t mode = 0)
{
    (void)depth;
    (void)mode;
    if (z_graphics_open) return;
    const char *hidden = getenv("ZENBLITZ_HIDDEN");
    bool visible = !(hidden && hidden[0] && hidden[0] != '0');
    if (z_platform.open(width, height, "zenblitz", false, visible))
    {
        z_graphics_open = true;
        z_platform.beginFrame();
    }
}

inline void zen_graphics3d_open(int width, int height, int64_t depth = 0, int64_t mode = 0)
{
    if (!z_graphics_open) zen_graphics_open(width, height, depth, mode);
    if (z_graphics_open) z_world_ensure();
}

inline void zen_flip(int64_t vwait = 1)
{
    if (!z_graphics_open) return;
    /* Flip's vwait is Blitz3D's vsync switch: "Flip 0" presents without
       waiting for the vertical blank. runtime3d maps it straight onto the
       swap interval and only touches it when it actually changes. */
    const bool vsync = vwait != 0;
    if (z_platform.isVSync() != vsync) z_platform.setVSync(vsync);
    z_platform.pumpEvents();
    if (!z_platform.isOpen()) { std::fflush(stdout); std::exit(0); }
    /* Present only. The next frame's Cls reopens the pass (beginFrame); doing
       it here cleared the screen texture the moment it had been presented,
       so nothing a script drew was left to read back. runtime3d's c_Flip
       stops at endFrame for the same reason. */
    z_platform.endFrame();
}

inline void zen_graphics_close()
{
    if (!z_graphics_open) return;
    if (z_world_init) { z_world.shutdown(); z_world_init = false; }
    z_platform.close();
    z_graphics_open = false;
    z_platform.flushKeyHits();
    engine::AudioSystem::get().shutdown();
}

inline void zen_cls()
{
    /* Cls is where a Blitz3D frame's 2D begins: it reopens the screen pass
       with a clear in the current ClsColor, which is what Platform's own
       beginFrame is for ("Cls: (re)open the screen pass with a clear").
       runtime3d's c_Cls calls beginFrame too - flushing here instead closed
       the pass the frame's 2D was about to draw into, so nothing a script
       drew after Cls ever reached the screen. */
    if (!z_graphics_open) return;
    z_platform.beginFrame();
}

inline void zen_cls_color(int64_t r, int64_t g, int64_t b)
{
    z_platform.setClearColor((float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f);
}

inline void zen_color(int64_t r, int64_t g, int64_t b)
{
    z_draw_color = 0xff000000u | ((unsigned)(r & 255) << 16) | ((unsigned)(g & 255) << 8) | (unsigned)(b & 255);
    z_platform.batch().setColor((unsigned char)r, (unsigned char)g, (unsigned char)b);
}

/* The 2D commands split the same way runtime3d's do: with an image or texture
   buffer current they rasterise into that pixmap, otherwise they go through
   the batch renderer and land on the screen. */
inline void zen_plot(int64_t x, int64_t y)
{
    if (!z_graphics_open) return;
    if (engine::ImageFrame *f = z_canvas_target())
    {
        f->pixels.set_pixel((engine::u32)x, (engine::u32)y, z_draw_color);
        f->dirty = true;
        return;
    }
    z_platform.batch().drawRect((float)x, (float)y, 1.0f, 1.0f, true);
}

inline void zen_line(int64_t x1, int64_t y1, int64_t x2, int64_t y2)
{
    if (!z_graphics_open) return;
    if (engine::ImageFrame *f = z_canvas_target())
    {
        f->pixels.draw_line((int)x1, (int)y1, (int)x2, (int)y2, engine::Color(z_draw_color));
        f->dirty = true;
        return;
    }
    z_platform.batch().drawLine((float)x1, (float)y1, (float)x2, (float)y2);
}

inline void zen_rect(int64_t x, int64_t y, int64_t w, int64_t h, int64_t solid = 1)
{
    if (!z_graphics_open) return;
    if (engine::ImageFrame *f = z_canvas_target())
    {
        f->pixels.draw_rect((int)x, (int)y, (int)w, (int)h, engine::Color(z_draw_color), solid != 0);
        f->dirty = true;
        return;
    }
    z_platform.batch().drawRect((float)x, (float)y, (float)w, (float)h, solid != 0);
}

inline void zen_oval(int64_t x, int64_t y, int64_t w, int64_t h, int64_t solid = 1)
{
    if (!z_graphics_open) return;
    if (engine::ImageFrame *f = z_canvas_target())
    {
        f->pixels.draw_circle((int)(x + w / 2), (int)(y + h / 2), (int)((w < h ? w : h) / 2),
                              engine::Color(z_draw_color), solid != 0);
        f->dirty = true;
        return;
    }
    z_platform.batch().drawEllipse(x + w * 0.5f, y + h * 0.5f, w * 0.5f, h * 0.5f, solid != 0);
}

inline void zen_text(int64_t x, int64_t y, const std::string &text, int64_t centre_x = 0, int64_t centre_y = 0)
{
    if (!z_graphics_open) return;
    float fx = (float)x, fy = (float)y;
    /* SetFont picks the size the text is drawn at - see the font block. */
    if (centre_x) fx -= z_platform.batch().textWidth(z_font_size, text.c_str()) * 0.5f;
    if (centre_y) fy -= z_font_size * 0.5f;
    z_platform.batch().drawText(fx, fy, z_font_size, text.c_str());
}

inline int64_t zen_key_down(int64_t key) { return z_platform.keyDown((int)key) ? 1 : 0; }
inline int64_t zen_key_hit(int64_t key) { return z_platform.keyHit((int)key) ? 1 : 0; }
inline int64_t zen_get_key() { return z_platform.popKey(); }
inline void zen_flush_keys() { z_platform.flushKeyHits(); }
inline void zen_hide_pointer() { z_platform.showPointer(false); }
inline void zen_show_pointer() { z_platform.showPointer(true); }

/* 2D canvas buffer. kScreenBuffer is 1 in this runtime: a positive buffer is
   the screen, a negative one a pixmap (see the state block). */
static constexpr int64_t kScreenBuffer = 1;

inline int64_t zen_back_buffer() { return kScreenBuffer; }
inline int64_t zen_front_buffer() { return kScreenBuffer; }
inline void zen_set_buffer(int64_t buffer)
{
    /* bbSetBuffer rejected a buffer that does not resolve: "SetBuffer:
       buffer does not exist". */
    if (buffer != kScreenBuffer && !z_buffer_frame(buffer))
    {
        zen_runtime_error("SetBuffer: buffer does not exist");
        return;
    }
    z_canvas_buffer = buffer;
    /* bbSetBuffer reset the origin and the viewport to the new buffer, so a
       region set for one never clipped another. */
    z_origin_x = 0;
    z_origin_y = 0;
}
inline void zen_origin(int64_t x, int64_t y)
{
    z_origin_x = (int)x;
    z_origin_y = (int)y;
}

/* Random — #Rnd#from#to=0, %Rand%from%to=1 */
inline double zen_rnd(double from, double to = 0.0)
{
    double r = (double)std::rand() / (double)RAND_MAX;
    if (to > from) return from + r * (to - from);
    return r * from;
}
inline int64_t zen_rand(int64_t from, int64_t to = 1)
{
    if (to > from) return from + (int64_t)(std::rand() % (int)(to - from));
    return (int64_t)(std::rand() % (int)(from > 0 ? from : 1));
}
inline void zen_seed_rnd(int64_t seed) { z_rand_seed_value = (unsigned)seed; std::srand(z_rand_seed_value); }

/* Timing */
/* One display refresh, which is what Blitz3D's VWait waited for. SDL has no
   vblank wait without a swap, so sleep the period instead; runtime3d's
   vblank_sleep does exactly this. */
inline void z_vblank_sleep()
{
    SDL_DisplayMode mode;
    const int hz = (SDL_GetCurrentDisplayMode(0, &mode) == 0 && mode.refresh_rate > 0) ? mode.refresh_rate : 60;
    SDL_Delay((Uint32)((1000 + hz / 2) / hz));
}
inline void zen_delay(int64_t ms) { if (ms > 0) SDL_Delay((Uint32)ms); }
inline void zen_vwait(int64_t frames = 1)
{
    if (!z_graphics_open) return;
    z_platform.pumpEvents();
    while (frames-- > 0 && z_platform.isOpen())
    {
        z_vblank_sleep();
        z_platform.pumpEvents();
    }
}


/* Graphics mode queries */
inline int64_t zen_windowed_3d() { return 1; }
/* Wait mouse */
inline int64_t zen_wait_mouse()
{
    if (!z_graphics_open) return 0;
    bool wasDown[4] = {false, false, false, false};
    for (int b = 1; b <= 3; ++b) wasDown[b] = z_platform.mouseDown(b);
    for (;;)
    {
        z_platform.pumpEvents();
        if (!z_platform.isOpen()) break;
        for (int b = 1; b <= 3; ++b)
            if (z_platform.mouseDown(b) && !wasDown[b]) return b;
    }
    return 0;
}

/* AppTitle (the close prompt is accepted and ignored: the engine's window
   always closes without asking) */
inline void zen_app_title(const std::string &, const std::string & = "") {}

/* Viewport */
inline void zen_viewport(int64_t x, int64_t y, int64_t w, int64_t h)
{
    z_viewport_x = std::max<int64_t>(0, x);
    z_viewport_y = std::max<int64_t>(0, y);
    const int right = std::min<int64_t>(z_platform.width(), x + w);
    const int bottom = std::min<int64_t>(z_platform.height(), y + h);
    z_viewport_width = std::max(0, right - z_viewport_x);
    z_viewport_height = std::max(0, bottom - z_viewport_y);
    if (z_graphics_open && z_viewport_width > 0 && z_viewport_height > 0)
        z_platform.batch().setClipRect((float)z_viewport_x, (float)z_viewport_y, (float)z_viewport_width, (float)z_viewport_height);
}

/* Fonts. The engine ships one embedded bitmap font, so a "font" here is just
   the point size to draw with - the family name is accepted and ignored
   rather than pretending to have TrueType families, the same bargain
   runtime3d's Bb3dFont makes. The handle is the pointer, as there. The state
   itself sits with the other globals, because zen_text uses it. */
inline int64_t zen_load_font(const std::string &, int64_t height = 12, int64_t bold = 0,
                             int64_t italic = 0, int64_t underline = 0)
{
    (void)bold;
    (void)italic;
    (void)underline;
    z_font_state *font = new z_font_state{(float)(height > 0 ? height : 16)};
    return (int64_t)(std::intptr_t)font;
}
inline void zen_set_font(int64_t font)
{
    if (font) z_font_size = ((z_font_state *)(std::intptr_t)font)->size;
}
inline void zen_free_font(int64_t font)
{
    delete (z_font_state *)(std::intptr_t)font;
}
inline int64_t zen_font_width() { return (int64_t)z_platform.batch().textWidth(z_font_size, "M"); }
inline int64_t zen_font_height() { return (int64_t)z_font_size; }
inline int64_t zen_string_width(const std::string &s)
{
    return (int64_t)z_platform.batch().textWidth(z_font_size, s.c_str());
}
inline int64_t zen_string_height(const std::string &) { return (int64_t)z_font_size; }

/* Rand seed readback */
inline int64_t zen_rand_seed() { return (int64_t)z_rand_seed_value; }

/* Clear collisions */
inline void zen_clear_collisions() {}

/* Images 2D */
inline int64_t zen_load_image(const std::string &file)
{
    engine::Pixmap pixels;
    if (!pixels.load(file.c_str())) return 0;
    engine::Image *image = new engine::Image();
    image->addFrame(std::move(pixels));
    int64_t handle = ++z_next_image;
    z_images[handle] = image;
    return handle;
}

inline void zen_draw_image(int64_t image, int64_t x, int64_t y, int64_t frame = 0)
{
    engine::Image *img = z_image_of(image);
    if (!img || !img->ensureUploaded(z_platform.device(), (int)frame)) return;
    const engine::ImageFrame &f = img->frame((int)frame);
    z_platform.batch().drawTexture(f.texture, (float)x - (float)img->handleX(),
                                   (float)y - (float)img->handleY(),
                                   (float)f.pixels.width, (float)f.pixels.height);
}
inline void zen_draw_image_rect(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t = 0) {}
inline void zen_grab_image(int64_t, int64_t, int64_t, int64_t = 0) {}
inline int64_t zen_load_anim_image(const std::string &, int64_t cellwidth, int64_t cellheight,
                                   int64_t first, int64_t count)
{
    (void)cellwidth;
    (void)cellheight;
    (void)first;
    (void)count;
    return 0;
}

/* Gfx drivers */
static inline void z_collect_gfx_modes()
{
    if (!z_gfx_modes.empty()) return;
    /* SDL_GetNumDisplayModes needs the video subsystem up, and a script may
       not have triggered it yet: CountGfxModes3D and Windowed3D have to be
       callable before the first Graphics - start.bb, the shared menu every
       3D sample runs through, calls them exactly that way. runtime3d's own
       collectGfxModes brings the subsystem up here too. */
    if (!SDL_WasInit(SDL_INIT_VIDEO) && SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) return;
    const int modeCount = SDL_GetNumDisplayModes(0);
    for (int index = 0; index < modeCount; ++index)
    {
        SDL_DisplayMode mode;
        if (SDL_GetDisplayMode(0, index, &mode) == 0)
            z_gfx_modes.push_back({mode.w, mode.h, (int)SDL_BITSPERPIXEL(mode.format)});
    }
}

inline int64_t zen_count_gfx_drivers() { return SDL_GetNumVideoDisplays(); }
inline int64_t zen_count_gfx_modes() { z_collect_gfx_modes(); return (int64_t)z_gfx_modes.size(); }
inline int64_t zen_gfx_mode_width(int64_t mode) { z_collect_gfx_modes(); return mode > 0 && (size_t)mode <= z_gfx_modes.size() ? z_gfx_modes[(size_t)mode - 1].width : 0; }
inline int64_t zen_gfx_mode_height(int64_t mode) { z_collect_gfx_modes(); return mode > 0 && (size_t)mode <= z_gfx_modes.size() ? z_gfx_modes[(size_t)mode - 1].height : 0; }
inline int64_t zen_gfx_mode_depth(int64_t mode) { z_collect_gfx_modes(); return mode > 0 && (size_t)mode <= z_gfx_modes.size() ? z_gfx_modes[(size_t)mode - 1].depth : 0; }
inline int64_t zen_gfx_mode3d_exists(int64_t width, int64_t height, int64_t depth)
{
    z_collect_gfx_modes();
    for (const z_gfx_mode &mode : z_gfx_modes)
        if (mode.width == width && mode.height == height && mode.depth == depth) return 1;
    return 0;
}

/* Buffer lock (no-op) */
inline void zen_lock_buffer(int64_t = 0) {}
inline void zen_unlock_buffer(int64_t = 0) {}
inline int64_t zen_texture_buffer(int64_t texture, int64_t frame = 0)
{
    /* The buffer a texture's own pixels live in: this is what a script hands
       to SetBuffer to paint art with the 2D commands (Tron draws its grid
       texture that way). */
    engine::Texture *t = z_texture(texture);
    return t && t->canvas((int)frame) ? z_buffer_handle(texture, (int)frame, 1) : 0;
}
/* SaveBuffer: the image side is not ported yet (LoadImage and friends are
   stubs), so a buffer handle cannot be resolved to pixels. The screen case is
   implemented the way runtime3d's c_SaveBuffer does it, row by row. */
inline int64_t zen_save_buffer(int64_t buffer, const std::string &bmpfile)
{
    /* kScreenBuffer is 1, so BackBuffer() is what saves the screen - the
       same readback c_SaveBuffer does. A negative handle is an image or
       texture buffer, which has no pixels to write back yet. */
    if (buffer < 0 || !z_graphics_open) return 0;
    const int w = z_platform.width(), h = z_platform.height();
    if (w <= 0 || h <= 0) return 0;
    engine::Pixmap shot(w, h, 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const unsigned argb = z_platform.readScreenPixel(x, y);
            shot.set_pixel((engine::u32)x, (engine::u32)y, (engine::u8)((argb >> 16) & 0xff),
                           (engine::u8)((argb >> 8) & 0xff), (engine::u8)(argb & 0xff), 255);
        }
    return shot.save(bmpfile.c_str()) ? 1 : 0;
}

/* Entity extras */
inline void zen_entity_type(int64_t entity, int64_t type, int64_t = 0)
{
    engine::Entity *e = z_entity(entity);
    if (e && e->getObject()) e->getObject()->setCollisionType((int)type);
}
inline int64_t zen_get_entity_type(int64_t entity)
{
    engine::Entity *e = z_entity(entity);
    return (e && e->getObject()) ? e->getObject()->getCollisionType() : 0;
}
inline void zen_entity_pick_mode(int64_t, int64_t, int64_t = 1) {}
inline int64_t zen_copy_entity(int64_t entity, int64_t parent = 0)
{
    /* Object::copy() clones the whole subtree - children, geometry and
       animator - so a copied pivot brings its cubes along. Reparent the copy
       the way every Create* does rather than duplicating that here, exactly
       as runtime3d's c_CopyEntity does. */
    engine::Entity *src = z_entity(entity);
    if (!src || !src->getObject()) return 0;
    engine::Object *copy = src->getObject()->copy();
    return z_store_entity(copy, z_entity(parent));
}

/* Mesh extras */
inline void zen_scale_mesh(int64_t mesh, double xs, double ys, double zs)
{
    /* ScaleMesh scales the mesh's own geometry, not the entity transform
       (that is ScaleEntity). runtime3d's c_ScaleMesh calls MeshUtil::scaleMesh
       the same way; engine::Model has no setMeshScale. */
    engine::Entity *e = z_entity(mesh);
    engine::MeshModel *m = (e && e->getModel()) ? e->getModel()->getMeshModel() : nullptr;
    if (m) engine::MeshUtil::scaleMesh(m, engine::Vector((float)xs, (float)ys, (float)zs));
}
inline void zen_flip_mesh(int64_t) {}

/* Sprites */
inline void zen_scale_sprite(int64_t, double, double) {}
inline void zen_sprite_view_mode(int64_t, int64_t) {}

/* Input */
inline std::string zen_input(const std::string & = "") { return ""; }
inline void zen_enable_direct_input(int64_t) {}
inline int64_t zen_direct_input_enabled() { return 0; }

/* Mouse extras */
inline void zen_move_mouse(int64_t x, int64_t y) { (void)x; (void)y; }

/* Sound (stubs) */
inline int64_t zen_load_sound(const std::string &) { return 0; }
inline int64_t zen_load_3d_sound(const std::string &) { return 0; }

/* Terrain/MD2 (stubs) */
inline int64_t zen_load_terrain(const std::string &, int64_t = 0) { return 0; }
inline int64_t zen_load_md2(const std::string &, int64_t = 0) { return 0; }

/* Brushes and surfaces.
   runtime3d hands the engine pointer itself across as the Blitz3D handle
   (c_CreateBrush, c_CreateSurface and c_VertexCoords all cast the argument
   instead of looking it up), so these wrappers do the same. */
static inline engine::Brush *z_brush(int64_t handle) { return (engine::Brush *)(std::intptr_t)handle; }
static inline engine::Surface *z_surface(int64_t handle) { return (engine::Surface *)(std::intptr_t)handle; }

inline int64_t zen_create_brush(int64_t red = 255, int64_t green = 255, int64_t blue = 255)
{
    engine::Brush *brush = new engine::Brush();
    brush->setColor(engine::Vector((float)red / 255.0f, (float)green / 255.0f, (float)blue / 255.0f));
    return (int64_t)(std::intptr_t)brush;
}
inline void zen_brush_color(int64_t brush, double red, double green, double blue)
{
    engine::Brush *b = z_brush(brush);
    if (b) b->setColor(engine::Vector((float)red / 255.0f, (float)green / 255.0f, (float)blue / 255.0f));
}
inline void zen_brush_blend(int64_t brush, int64_t blend)
{
    engine::Brush *b = z_brush(brush);
    if (b) b->setBlend((int)blend);
}
inline void zen_brush_fx(int64_t brush, int64_t fx)
{
    engine::Brush *b = z_brush(brush);
    if (b) b->setFX((int)fx);
}

/* Vertex */
inline int64_t zen_add_vertex(int64_t surface, double x, double y, double z, double u = 0, double v = 0,
                              double w = 1)
{
    /* Mirrors c_AddVertex, which ignores w as well. */
    (void)w;
    engine::Surface *s = z_surface(surface);
    if (!s) return 0;
    engine::Surface::Vertex vertex;
    vertex.coords = engine::Vector((float)x, (float)y, (float)z);
    vertex.texCoords[0][0] = (float)u;
    vertex.texCoords[0][1] = (float)v;
    return s->addVertex(vertex);
}
inline int64_t zen_add_triangle(int64_t surface, int64_t v0, int64_t v1, int64_t v2)
{
    engine::Surface *s = z_surface(surface);
    if (!s) return 0;
    engine::Surface::Triangle triangle{{(unsigned short)v0, (unsigned short)v1, (unsigned short)v2}};
    return s->addTriangle(triangle);
}
inline void zen_update_normals(int64_t mesh)
{
    /* UpdateNormals takes a mesh, not a surface: runtime3d's c_UpdateNormals
       walks entity -> model -> MeshModel the same way. */
    engine::Entity *e = z_entity(mesh);
    engine::MeshModel *m = (e && e->getModel()) ? e->getModel()->getMeshModel() : nullptr;
    if (m) m->updateNormals();
}
inline void zen_vertex_coords(int64_t surface, int64_t index, double x, double y, double z)
{
    engine::Surface *s = z_surface(surface);
    if (s) s->setCoords((int)index, engine::Vector((float)x, (float)y, (float)z));
}

/* Camera extras */
inline void zen_camera_viewport(int64_t cam, int64_t x, int64_t y, int64_t w, int64_t h)
{
    engine::Entity *e = z_entity(cam);
    if (e && e->getCamera()) e->getCamera()->setViewport((int)x, (int)y, (int)w, (int)h);
}
inline void zen_camera_fog_mode(int64_t cam, int64_t mode)
{
    engine::Entity *e = z_entity(cam);
    if (e && e->getCamera()) e->getCamera()->setFogMode((int)mode);
}
inline void zen_camera_fog_color(int64_t cam, double r, double g, double b)
{
    engine::Entity *e = z_entity(cam);
    if (e && e->getCamera()) e->getCamera()->setFogColor(engine::Vector((float)r/255.0f, (float)g/255.0f, (float)b/255.0f));
}
inline void zen_camera_fog_range(int64_t cam, double near, double far)
{
    engine::Entity *e = z_entity(cam);
    if (e && e->getCamera()) e->getCamera()->setFogRange((float)near, (float)far);
}
inline void zen_camera_proj_mode(int64_t cam, int64_t mode)
{
    engine::Entity *e = z_entity(cam);
    if (e && e->getCamera()) e->getCamera()->setProjMode((int)mode);
}
inline int64_t zen_camera_pick(int64_t, double, double) { return 0; }

/* Entity extras */
inline void zen_entity_radius(int64_t, double, double = 0) {}
inline void zen_entity_autofade(int64_t, double, double) {}
inline void zen_entity_parent(int64_t entity, int64_t parent, int64_t = 1)
{
    engine::Entity *e = z_entity(entity);
    engine::Entity *p = z_entity(parent);
    if (e) e->setParent(p);
}
inline void zen_name_entity(int64_t, const std::string &) {}
inline std::string zen_entity_name(int64_t) { return ""; }
inline std::string zen_entity_class(int64_t h)
{
    engine::Entity *e = z_entity(h);
    if (!e) return "";
    return e->getCamera() ? "Camera" : e->getLight() ? "Light" :
           e->getModel() ? "Mesh" : e->getSprite() ? "Sprite" : "Pivot";
}
inline void zen_paint_mesh(int64_t, int64_t) {}
inline void zen_paint_entity(int64_t, int64_t) {}

/* Animation — mirrors c_Animate / c_AddAnimSeq / c_SetAnimKey. */
inline void zen_animate(int64_t entity, int64_t mode = 1, double speed = 1.0, int64_t sequence = 0,
                        double transition = 0.0)
{
    engine::Entity *e = z_entity(entity);
    engine::Object *object = e ? e->getObject() : nullptr;
    if (object)
        if (engine::Animator *animator = object->getAnimator())
            animator->animate((int)mode, (float)speed, (int)sequence, (float)transition);
}
inline int64_t zen_add_anim_seq(int64_t entity, int64_t length)
{
    engine::Entity *e = z_entity(entity);
    engine::Object *object = e ? e->getObject() : nullptr;
    if (!object) return -1;
    engine::Animator *animator = object->getAnimator();
    if (animator) animator->addSeq((int)length);
    else { animator = new engine::Animator(object, (int)length); object->setAnimator(animator); }
    return animator->numSeqs() - 1;
}
inline void zen_set_anim_key(int64_t entity, int64_t frame, int64_t pos_key = 1, int64_t rot_key = 1,
                             int64_t scale_key = 1)
{
    engine::Entity *e = z_entity(entity);
    engine::Object *object = e ? e->getObject() : nullptr;
    if (!object) return;
    engine::Animation animation = object->getAnimation();
    if (pos_key) animation.setPositionKey((int)frame, object->getLocalPosition());
    if (rot_key) animation.setRotationKey((int)frame, object->getLocalRotation());
    if (scale_key) animation.setScaleKey((int)frame, object->getLocalScale());
    object->setAnimation(animation);
}

/* Gfx driver name */
inline std::string zen_gfx_driver_name(int64_t driver)
{
    const int count = SDL_GetNumVideoDisplays();
    if (driver < 1 || driver > count) return "";
    const char *name = SDL_GetDisplayName((int)driver - 1);
    return name ? name : "";
}

/* Images extras (stubs) */
inline void zen_mask_image(int64_t, int64_t, int64_t, int64_t) {}
inline void zen_mid_handle(int64_t image)
{
    engine::Image *img = z_image_of(image);
    if (img) img->setMidHandle();
}
inline void zen_auto_mid_handle(int64_t) {}
inline int64_t zen_copy_image(int64_t) { return 0; }
inline void zen_resize_image(int64_t, int64_t, int64_t) {}
inline void zen_save_image(int64_t, const std::string &, int64_t = 0) {}
inline void zen_handle_image(int64_t image, int64_t x, int64_t y)
{
    engine::Image *img = z_image_of(image);
    if (img) img->setHandle((int)x, (int)y);
}
inline void zen_draw_block(int64_t image, int64_t x, int64_t y, int64_t frame = 0)
{
    /* DrawBlock is the opaque form: no blending while it lands. */
    engine::Image *img = z_image_of(image);
    if (!img || !img->ensureUploaded(z_platform.device(), (int)frame)) return;
    const engine::ImageFrame &f = img->frame((int)frame);
    z_platform.batch().setBlend(false);
    z_platform.batch().drawTexture(f.texture, (float)x - (float)img->handleX(),
                                   (float)y - (float)img->handleY(),
                                   (float)f.pixels.width, (float)f.pixels.height);
    z_platform.batch().setBlend(true);
}
inline void zen_draw_block_rect(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t = 0) {}
inline void zen_tile_block(int64_t, int64_t = 0, int64_t = 0, int64_t = 0) {}
inline void zen_write_pixel_fast(int64_t, int64_t, int64_t, int64_t = 0) {}
inline void zen_wireframe(int64_t) {}

inline int64_t zen_mouse_x() { return z_platform.mouseX(); }
inline int64_t zen_mouse_y() { return z_platform.mouseY(); }
inline int64_t zen_mouse_down(int64_t b) { return z_platform.mouseDown((int)b) ? 1 : 0; }
inline int64_t zen_mouse_hit(int64_t b) { return z_platform.mouseHit((int)b); }
inline int64_t zen_mouse_z() { return z_platform.mouseZ(); }

inline int64_t zen_graphics_width() { return z_platform.width(); }
inline int64_t zen_graphics_height() { return z_platform.height(); }

/* Sprites & images (stubs for now) */
inline int64_t zen_load_sprite(const std::string &, int64_t texture_flags = 1, int64_t parent = 0)
{
    (void)texture_flags;
    (void)parent;
    return 0;
}
inline int64_t zen_create_image(int64_t width, int64_t height, int64_t frames = 1)
{
    if (width <= 0 || height <= 0 || frames <= 0) return 0;
    engine::Image *image = new engine::Image();
    for (int64_t i = 0; i < frames; ++i)
    {
        engine::Pixmap pixels((int)width, (int)height, 4);
        pixels.clear();
        image->addFrame(std::move(pixels));
    }
    int64_t handle = ++z_next_image;
    z_images[handle] = image;
    return handle;
}

inline int64_t zen_image_buffer(int64_t image, int64_t frame = 0)
{
    engine::Image *img = z_image_of(image);
    return img && frame >= 0 && frame < img->frameCount() ? z_buffer_handle(image, (int)frame, 0) : 0;
}

inline void zen_free_image(int64_t image)
{
    auto it = z_images.find(image);
    if (it == z_images.end()) return;
    delete it->second;
    z_images.erase(it);
}

/* Mesh extras (stubs) */
inline void zen_fit_mesh(int64_t, double, double, double, double, double, double, int64_t = 0) {}
inline int64_t zen_create_surface(int64_t mesh, int64_t brush = 0)
{
    /* The handle is the engine::Surface pointer, as in c_CreateSurface. */
    engine::Entity *e = z_entity(mesh);
    engine::MeshModel *m = (e && e->getModel()) ? e->getModel()->getMeshModel() : nullptr;
    if (!m) return 0;
    engine::Brush *b = z_brush(brush);
    return (int64_t)(std::intptr_t)m->createSurface(b ? *b : engine::Brush());
}

/* Collisions (stub) */
inline void zen_collisions(int64_t, int64_t, int64_t, int64_t) {}

inline int64_t zen_engine_millisecs() { return (int64_t)z_platform.milliSecs(); }

inline void zen_wait_key()
{
    if (!z_graphics_open) return;
    z_platform.flushKeyHits();
    while (z_platform.isOpen())
    {
        z_platform.pumpEvents();
        for (int k = 1; k < 256; ++k)
            if (z_platform.keyHit(k)) return;
    }
}

/* ================= 3D entities ================= */

inline int64_t zen_create_camera(int64_t parent = 0)
{
    z_world_ensure();
    auto *cam = new engine::Camera();
    cam->setViewport(0, 0, z_platform.width(), z_platform.height());
    return z_store_entity(cam, z_entity(parent));
}

inline int64_t zen_create_light(int64_t type = 1, int64_t parent = 0)
{
    z_world_ensure();
    auto *light = new engine::Light(type ? (int)type : engine::Light::LightPoint);
    return z_store_entity(light, z_entity(parent));
}

inline int64_t zen_create_cube(int64_t parent = 0)
{
    z_world_ensure();
    engine::Brush b;
    return z_store_entity(engine::MeshUtil::createCube(b), z_entity(parent));
}

inline int64_t zen_create_sphere(int64_t segments = 8, int64_t parent = 0)
{
    z_world_ensure();
    engine::Brush b;
    return z_store_entity(engine::MeshUtil::createSphere(b, (int)segments), z_entity(parent));
}

inline int64_t zen_create_cylinder(int64_t segments = 8, int64_t solid = 1, int64_t parent = 0)
{
    z_world_ensure();
    engine::Brush b;
    return z_store_entity(engine::MeshUtil::createCylinder(b, (int)segments, solid != 0), z_entity(parent));
}

inline int64_t zen_create_cone(int64_t segments = 8, int64_t solid = 1, int64_t parent = 0)
{
    z_world_ensure();
    engine::Brush b;
    return z_store_entity(engine::MeshUtil::createCone(b, (int)segments, solid != 0), z_entity(parent));
}

inline int64_t zen_create_plane(int64_t segments = 1, int64_t parent = 0)
{
    z_world_ensure();
    auto *plane = new engine::PlaneModel((int)segments);
    return z_store_entity(plane, z_entity(parent));
}

inline int64_t zen_create_pivot(int64_t parent = 0)
{
    z_world_ensure();
    return z_store_entity(new engine::Object(), z_entity(parent));
}

inline int64_t zen_create_mesh(int64_t parent = 0)
{
    z_world_ensure();
    return z_store_entity(new engine::MeshModel(), z_entity(parent));
}

inline int64_t zen_create_mirror(int64_t parent = 0)
{
    z_world_ensure();
    return z_store_entity(new engine::Mirror(), z_entity(parent));
}

inline void zen_free_entity(int64_t h)
{
    auto it = z_entities.find(h);
    if (it == z_entities.end()) return;
    engine::Entity *e = it->second;
    if (z_graphics_open) e->freeGpuTree(z_platform.device());
    z_entities.erase(it);
    delete e;
}

inline void zen_hide_entity(int64_t h)
{
    engine::Entity *e = z_entity(h);
    if (e) { e->setEnabled(false); e->setVisible(false); }
}

inline void zen_show_entity(int64_t h)
{
    engine::Entity *e = z_entity(h);
    if (e) { e->setVisible(true); e->setEnabled(true); if (e->getObject()) e->getObject()->reset(); }
}

/* ================= Transforms ================= */

inline void zen_position_entity(int64_t h, double x, double y, double z, int64_t global = 0)
{
    engine::Entity *e = z_entity(h);
    if (!e) return;
    engine::Vector v((float)x, (float)y, (float)z);
    global ? e->setWorldPosition(v) : e->setLocalPosition(v);
}

inline void zen_rotate_entity(int64_t h, double pitch, double yaw, double roll, int64_t global = 0)
{
    engine::Entity *e = z_entity(h);
    if (!e) return;
    engine::Quat q = blitz::rotationQuat((float)pitch * kDegToRad, (float)yaw * kDegToRad, (float)roll * kDegToRad);
    global ? e->setWorldRotation(q) : e->setLocalRotation(q);
}

inline void zen_scale_entity(int64_t h, double xs, double ys, double zs, int64_t global = 0)
{
    engine::Entity *e = z_entity(h);
    if (!e) return;
    engine::Vector v((float)xs, (float)ys, (float)zs);
    global ? e->setWorldScale(v) : e->setLocalScale(v);
}

inline void zen_move_entity(int64_t h, double x, double y, double z)
{
    engine::Entity *e = z_entity(h);
    if (!e) return;
    engine::Vector v((float)x, (float)y, (float)z);
    e->setLocalPosition(e->getLocalPosition() + e->getLocalRotation() * v);
}

inline void zen_turn_entity(int64_t h, double pitch, double yaw, double roll, int64_t global = 0)
{
    engine::Entity *e = z_entity(h);
    if (!e) return;
    engine::Quat q = blitz::rotationQuat((float)pitch * kDegToRad, (float)yaw * kDegToRad, (float)roll * kDegToRad);
    global ? e->setWorldRotation(q * e->getWorldRotation()) : e->setLocalRotation(e->getLocalRotation() * q);
}

inline void zen_point_entity(int64_t h, int64_t target, double roll = 0)
{
    engine::Entity *e = z_entity(h);
    engine::Entity *t = z_entity(target);
    if (!e || !t) return;
    engine::Vector v = t->getWorldTform().v - e->getWorldTform().v;
    e->setWorldRotation(blitz::rotationQuat(v.pitch(), v.yaw(), (float)roll * kDegToRad));
}

/* ================= Entity queries ================= */

inline double zen_entity_x(int64_t h, int64_t global = 0)
{
    engine::Entity *e = z_entity(h);
    return e ? (global ? e->getWorldPosition().x : e->getLocalPosition().x) : 0.0;
}
inline double zen_entity_y(int64_t h, int64_t global = 0)
{
    engine::Entity *e = z_entity(h);
    return e ? (global ? e->getWorldPosition().y : e->getLocalPosition().y) : 0.0;
}
inline double zen_entity_z(int64_t h, int64_t global = 0)
{
    engine::Entity *e = z_entity(h);
    return e ? (global ? e->getWorldPosition().z : e->getLocalPosition().z) : 0.0;
}
static constexpr float kRadToDeg = 57.2957795130823208767981548141052f;

inline double zen_entity_pitch(int64_t h, int64_t global = 0)
{
    engine::Entity *e = z_entity(h);
    if (!e) return 0.0;
    auto q = global ? e->getWorldRotation() : e->getLocalRotation();
    return (double)(blitz::quatPitch(q) * kRadToDeg);
}
inline double zen_entity_yaw(int64_t h, int64_t global = 0)
{
    engine::Entity *e = z_entity(h);
    if (!e) return 0.0;
    auto q = global ? e->getWorldRotation() : e->getLocalRotation();
    return (double)(blitz::quatYaw(q) * kRadToDeg);
}
inline double zen_entity_roll(int64_t h, int64_t global = 0)
{
    engine::Entity *e = z_entity(h);
    if (!e) return 0.0;
    auto q = global ? e->getWorldRotation() : e->getLocalRotation();
    return (double)(blitz::quatRoll(q) * kRadToDeg);
}

/* ================= Entity properties ================= */

inline void zen_entity_color(int64_t h, double r, double g, double b)
{
    engine::Entity *e = z_entity(h);
    if (e && e->getModel()) e->getModel()->setColor(engine::Vector((float)r/255.0f, (float)g/255.0f, (float)b/255.0f));
}
inline void zen_entity_alpha(int64_t h, double alpha)
{
    engine::Entity *e = z_entity(h);
    if (e && e->getModel()) e->getModel()->setAlpha((float)alpha);
}
inline void zen_entity_fx(int64_t h, int64_t fx)
{
    engine::Entity *e = z_entity(h);
    if (e && e->getModel()) e->getModel()->setFX((int)fx);
}
inline void zen_entity_blend(int64_t h, int64_t blend)
{
    engine::Entity *e = z_entity(h);
    if (e && e->getModel()) e->getModel()->setBlend((int)blend);
}
inline void zen_entity_order(int64_t h, int64_t order)
{
    engine::Entity *e = z_entity(h);
    if (e && e->getObject()) e->getObject()->setOrder((int)order);
}
inline void zen_entity_shininess(int64_t h, double shininess)
{
    engine::Entity *e = z_entity(h);
    if (e && e->getModel()) e->getModel()->setShininess((float)shininess);
}

/* ================= Camera ================= */

inline void zen_camera_cls_color(int64_t cam, double r, double g, double b)
{
    engine::Entity *e = z_entity(cam);
    if (e && e->getCamera()) e->getCamera()->setClsColor(engine::Vector((float)r/255.0f, (float)g/255.0f, (float)b/255.0f));
}
inline void zen_camera_range(int64_t cam, double near, double far)
{
    engine::Entity *e = z_entity(cam);
    if (e && e->getCamera()) e->getCamera()->setRange((float)near, (float)far);
}
inline void zen_camera_zoom(int64_t cam, double zoom)
{
    engine::Entity *e = z_entity(cam);
    if (e && e->getCamera()) e->getCamera()->setZoom((float)zoom);
}

/* ================= Light ================= */

inline void zen_ambient_light(double r, double g, double b)
{
    z_world_ensure();
    z_world.setAmbient(engine::Vector((float)r/255.0f, (float)g/255.0f, (float)b/255.0f));
}
inline void zen_light_color(int64_t h, double r, double g, double b)
{
    engine::Entity *e = z_entity(h);
    if (e && e->getLight()) e->getLight()->setColor(engine::Vector((float)r/255.0f, (float)g/255.0f, (float)b/255.0f));
}
inline void zen_light_range(int64_t h, double range)
{
    engine::Entity *e = z_entity(h);
    if (e && e->getLight()) e->getLight()->setRange((float)range);
}

/* ================= World update / render ================= */

inline void zen_update_world(double elapsed = 1.0)
{
    z_world_ensure();
    z_world.update((float)elapsed);
}

inline void zen_render_world(double tween = 1.0)
{
    if (!z_graphics_open || !z_world_init) return;
    z_platform.flushCanvasPass();
    z_world.prepare(z_platform.device(), (float)tween);
    if (z_platform.beginWorldRender())
    {
        z_world.draw(z_platform.device());
        z_platform.endWorldRender();
    }
}

inline void zen_capture_world()
{
    z_world_ensure();
    z_world.capture();
}

/* ================= Textures ================= */

static int64_t z_next_texture = 0;

inline int64_t zen_load_texture(const std::string &file, int64_t flags = 1)
{
    z_world_ensure();
    engine::Texture *t = engine::Texture::load(file.c_str(), (int)flags);
    if (!t) return 0;
    int64_t h = ++z_next_texture;
    z_textures[h] = t;
    return h;
}

inline int64_t zen_create_texture(int64_t w, int64_t h, int64_t flags = 1, int64_t frames = 1)
{
    /* Blank and paintable: this is what a script draws its own art into,
       through the buffer TextureBuffer() hands back. */
    engine::Texture *t = engine::Texture::create((int)w, (int)h, (int)flags, (int)frames);
    if (!t) return 0;
    int64_t handle = ++z_next_texture;
    z_textures[handle] = t;
    return handle;
}

inline void zen_scale_texture(int64_t texture, double u, double v)
{
    /* runtime3d stores the reciprocal, so ScaleTexture 10,10 tiles ten
       times rather than shrinking to a tenth. */
    engine::Texture *t = z_texture(texture);
    if (t && u != 0.0 && v != 0.0) t->setScale(1.0f / (float)u, 1.0f / (float)v);
}

inline void zen_free_texture(int64_t h)
{
    auto it = z_textures.find(h);
    if (it == z_textures.end()) return;
    engine::Texture::release(it->second);
    z_textures.erase(it);
}

inline void zen_entity_texture(int64_t entity, int64_t texture, int64_t frame = 0, int64_t index = 0)
{
    engine::Entity *e = z_entity(entity);
    auto it = z_textures.find(texture);
    if (!e || !e->getModel() || it == z_textures.end()) return;
    e->getModel()->setTexture((int)index,
        engine::BrushTexture::fromTexture(z_platform.device(), it->second, (int)frame));
}

/* ================= Mesh loading ================= */

inline int64_t zen_load_mesh(const std::string &file, int64_t parent = 0)
{
    z_world_ensure();
    std::string ext = file.substr(file.find_last_of('.') + 1);
    for (char &c : ext) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    engine::Entity *e = nullptr;
    if (ext == "md2")
    {
        auto *m = new engine::MD2Model(file);
        if (!m->getValid()) delete m;
        else e = m;
    }
    else if (ext == "3ds")
    {
        static const engine::Transform kConv3ds(
            blitz::Matrix(blitz::Vector(1,0,0), blitz::Vector(0,0,1), blitz::Vector(0,1,0)));
        engine::LoaderB3DS loader;
        e = loader.load(file, kConv3ds, engine::MeshLoader::HintCollapse, &z_platform.device());
    }
    else if (ext == "x")
    {
        engine::LoaderX loader;
        e = loader.load(file, engine::Transform(), engine::MeshLoader::HintCollapse, &z_platform.device());
    }
    else if (ext == "gltf" || ext == "glb")
    {
        engine::LoaderGltf loader;
        e = loader.load(file, engine::Transform(), engine::MeshLoader::HintCollapse, &z_platform.device());
    }
    else if (ext == "fbx")
    {
        engine::LoaderFbx loader;
        e = loader.load(file, engine::Transform(), engine::MeshLoader::HintCollapse, &z_platform.device());
    }
    else
    {
        engine::LoaderB3D loader;
        e = loader.load(file, engine::Transform(), engine::MeshLoader::HintCollapse, &z_platform.device());
    }
    if (!e) return 0;
    return z_store_entity(e, z_entity(parent));
}

inline int64_t zen_load_animmesh(const std::string &file, int64_t parent = 0)
{
    return zen_load_mesh(file, parent);
}

#endif
