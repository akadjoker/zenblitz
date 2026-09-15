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
#include "engine/VoxelSprite.h"
#include "engine/Sprite.h"
#include "engine/Terrain.h"
#include "engine/FilePath.h"
#include "engine/Pixmap.h"
#include "engine/Image.h"
#include "engine/Collision.h"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_video.h>

/* Every wrapper the generated C++ calls lives in this namespace, with a
   using-directive at the bottom of the file so the unqualified calls the
   emitter writes still resolve. Reason: a script variable named e.g. brush
   arrives as a global z_brush_of, and a global declaration now simply hides the
   SDK name instead of clashing with it. */
namespace zen_sdk {

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
static std::map<int64_t, engine::Image *> z_image_list;
static std::map<int64_t, engine::Texture *> z_texture_list;
static int64_t z_next_image = 0;
static int64_t z_canvas_buffer = 1;

/* Voxel materials: LoadMaterial keeps its own registry, exactly as runtime3d
   does with g_voxel_materials, because a VoxelSprite is handed the whole
   atlas description rather than just a texture handle. Declared up here
   because the teardown below has to release what it owns. */
struct z_voxel_material
{
    engine::Texture *texture = nullptr;
    int columns = 1, rows = 1;
    int firstFrame = 0, frameCount = 1;
};
static std::map<int64_t, z_voxel_material *> z_voxel_materials;
static int64_t z_next_voxel_material = 0;

/* The size text is drawn at: LoadFont makes one of these and SetFont picks
   it, the point size being all this runtime's single bitmap font needs.
   The "_state" suffix matters: the generator names a script's own `font`
   variable z_font, and this header is compiled into the same translation
   unit as that code. */
struct z_font_state { float size; };
static float z_font_size = 16.0f;

/* TFormPoint/Vector/Normal leave their answer here for TFormedX/Y/Z to read,
   one global rather than per-VM state, exactly as runtime3d keeps g_tformed.
   The pick CameraPick fills is here for the same reason (pick_for's result). */
static engine::Vector z_tformed_state;
static engine::ObjCollision z_pick_state;
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
    auto it = z_image_list.find(handle);
    return it != z_image_list.end() ? it->second : nullptr;
}

static inline engine::Texture *z_texture_of(int64_t handle)
{
    auto it = z_texture_list.find(handle);
    return it != z_texture_list.end() ? it->second : nullptr;
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
    engine::Texture *texture = z_texture_of(resource);
    return texture ? texture->canvas(frame) : nullptr;
}

/* The frame the 2D commands are painting into; null means the screen, which
   goes through the batch renderer instead of a pixmap. */
static inline engine::ImageFrame *z_canvas_target() { return z_buffer_frame(z_canvas_buffer); }

/* The mesh behind a handle, and the inverse of z_entities: runtime3d keeps a
   second map for that (g_handles), scanning is enough here. */
static inline engine::MeshModel *z_mesh_of(int64_t handle)
{
    engine::Entity *e = z_entity(handle);
    return (e && e->getModel()) ? e->getModel()->getMeshModel() : nullptr;
}

static inline int64_t z_handle_of(engine::Entity *e)
{
    for (auto &entry : z_entities)
        if (entry.second == e) return entry.first;
    return 0;
}

/* Pixel access. Only an image or a texture buffer can be *written*; the
   screen can only be read back, which is why runtime3d's write_pixel says so
   out loud instead of silently doing nothing. */
static inline int64_t z_buffer_arg(int64_t handle) { return handle ? handle : z_canvas_buffer; }

static inline void z_buffer_size(int64_t handle, int &w, int &h)
{
    if (engine::ImageFrame *f = z_buffer_frame(handle))
    {
        w = f->pixels.width;
        h = f->pixels.height;
        return;
    }
    w = z_platform.width();
    h = z_platform.height();
}

static inline unsigned z_read_pixel(int64_t handle, int x, int y)
{
    if (engine::ImageFrame *f = z_buffer_frame(handle))
    {
        if (x < 0 || y < 0 || x >= f->pixels.width || y >= f->pixels.height) return 0;
        const engine::Color c = f->pixels.get_pixel_color((engine::u32)x, (engine::u32)y);
        return ((unsigned)c.a() << 24) | ((unsigned)c.r() << 16) | ((unsigned)c.g() << 8) | c.b();
    }
    /* Reading the screen mid-frame answers 0 while a pass is still open on it:
       the GPU cannot sample a texture it is writing. runtime3d's own read goes
       straight through, so a script reading between drawing and Flip sees
       nothing here and the clear colour there. Closing the pass to read it was
       tried and reopens it over the frame, losing the 3D layer, so the plain
       read stays. */
    return z_platform.readScreenPixel(x, y);
}

static inline bool z_write_pixel(int64_t handle, int x, int y, unsigned argb)
{
    engine::ImageFrame *f = z_buffer_frame(handle);
    if (!f) return false;
    if (x < 0 || y < 0 || x >= f->pixels.width || y >= f->pixels.height) return true;
    f->pixels.set_pixel((engine::u32)x, (engine::u32)y, (engine::u8)((argb >> 16) & 0xff),
                        (engine::u8)((argb >> 8) & 0xff), (engine::u8)(argb & 0xff),
                        (engine::u8)((argb >> 24) & 0xff));
    f->dirty = true;
    return true;
}

static inline void z_warn_screen_write(const char *command)
{
    std::fprintf(stderr, "%s: can only write to an image or texture buffer, not the screen\n", command);
}

static inline bool z_pixel_inside_viewport(int x, int y)
{
    /* The clip is the canvas region's, which starts as the whole buffer and
       is only narrowed by Viewport; runtime3d keeps it per buffer too, reset
       on SetBuffer. A zero size means "no clip set", not "clip everything". */
    if (z_viewport_width <= 0 || z_viewport_height <= 0) return true;
    return x >= z_viewport_x && x < z_viewport_x + z_viewport_width &&
           y >= z_viewport_y && y < z_viewport_y + z_viewport_height;
}

/* Teardown. The GPU has to be wound down before the driver starts its own:
   every GL object here is released through the device, and if they are still
   alive when Mesa tears the context down the driver faults - the same crash
   runtime3d avoids by calling shutdown_graphics before returning. Registering
   it with atexit at open time covers every exit a script has: End,
   RuntimeError, the window being closed, or falling off the end of main. */
static void z_shutdown_graphics()
{
    if (!z_graphics_open) return;
    /* GPU buffers first: a destructor cannot release them, it has no device,
       so the whole tree is swept before anything is deleted. */
    for (engine::Entity *root = engine::Entity::orphans(); root; root = root->successor())
        root->freeGpuTree(z_platform.device());
    for (auto &entry : z_image_list) delete entry.second;
    z_image_list.clear();
    for (auto &entry : z_voxel_materials)
    {
        engine::Texture::release(entry.second->texture);
        delete entry.second;
    }
    z_voxel_materials.clear();
    for (auto &entry : z_texture_list) engine::Texture::release(entry.second);
    z_texture_list.clear();
    /* ~Entity deletes its children, so deleting the orphan roots walks the
       whole scene. */
    while (engine::Entity *root = engine::Entity::orphans()) delete root;
    z_entities.clear();
    if (z_world_init) { z_world.shutdown(); z_world_init = false; }
    z_platform.close();
    z_graphics_open = false;
    engine::AudioSystem::get().shutdown();
}

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
        /* No beginFrame here: runtime3d's c_Graphics only opens the platform,
           and the frame's first Cls is what opens (and clears) the screen
           pass. Opening one here made that Cls a no-op - beginFrame returns
           false while a pass is already open - so the ClsColor never reached
           the first frame and 2D drawn before RenderWorld survived it. */
        /* See z_shutdown_graphics: without this the GL objects outlive the
           context and the driver takes the process down on exit. */
        std::atexit(z_shutdown_graphics);
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
    z_shutdown_graphics();
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
inline void zen_clear_collisions() { z_world.clearCollisions(); }

/* Images 2D */
/* AutoMidHandle: from here on every image stored is centred, and the flag is
   read where the image is stored - runtime3d's store_image does the same. */
static bool z_auto_mid_handle = false;

static inline int64_t z_store_image(engine::Image *image)
{
    if (z_auto_mid_handle) image->setMidHandle();
    const int64_t handle = ++z_next_image;
    z_image_list[handle] = image;
    return handle;
}

inline int64_t zen_load_image(const std::string &file)
{
    engine::Pixmap pixels;
    if (!pixels.load(file.c_str())) return 0;
    engine::Image *image = new engine::Image();
    image->addFrame(std::move(pixels));
    return z_store_image(image);
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
inline void zen_draw_image_rect(int64_t image, int64_t x, int64_t y, int64_t rectX, int64_t rectY,
                                int64_t rectW, int64_t rectH, int64_t frame = 0)
{
    /* Blend, and draw the given sub-rectangle of the frame at x,y - the batch
       renderer takes normalised source coordinates. */
    engine::Image *img = z_image_of(image);
    if (!img || !img->ensureUploaded(z_platform.device(), (int)frame)) return;
    const engine::ImageFrame &f = img->frame((int)frame);
    z_platform.batch().drawTexture(f.texture, (float)x - (float)img->handleX(),
                                   (float)y - (float)img->handleY(), (float)rectW, (float)rectH,
                                   (float)rectX / (float)f.pixels.width,
                                   (float)rectY / (float)f.pixels.height,
                                   (float)rectW / (float)f.pixels.width,
                                   (float)rectH / (float)f.pixels.height);
}
inline void zen_grab_image(int64_t image, int64_t x, int64_t y, int64_t frame = 0)
{
    /* Read the current buffer back into the image, offset by the canvas origin
       and centred on the image's handle - runtime3d's c_GrabImage. */
    engine::Image *img = z_image_of(image);
    const int n = (int)frame;
    if (!img || n < 0 || n >= img->frameCount()) return;
    engine::ImageFrame &f = img->frame(n);
    const int x0 = (int)x + z_origin_x - img->handleX();
    const int y0 = (int)y + z_origin_y - img->handleY();
    const int w = f.pixels.width, h = f.pixels.height;
    for (int py = 0; py < h; ++py)
        for (int px = 0; px < w; ++px)
        {
            const unsigned argb = z_read_pixel(z_canvas_buffer, x0 + px, y0 + py);
            f.pixels.set_pixel((engine::u32)px, (engine::u32)py, (engine::u8)((argb >> 16) & 0xff),
                               (engine::u8)((argb >> 8) & 0xff), (engine::u8)(argb & 0xff), 255);
        }
    f.dirty = true;
    f.cmMask.clear();
    f.cmPitch = 0;
}
inline int64_t zen_load_anim_image(const std::string &file, int64_t cellwidth, int64_t cellheight,
                                   int64_t first, int64_t count)
{
    /* An animation image is a grid cut out of one bitmap: frames run across
       each row, and the whole requested range has to fit - c_LoadAnimImage. */
    const int cellW = (int)cellwidth, cellH = (int)cellheight;
    const int firstFrame = (int)first, frameCount = (int)count;
    if (frameCount < 1 || firstFrame < 0 || cellW <= 0 || cellH <= 0) return 0;
    engine::Pixmap sheet;
    if (!sheet.load(file.c_str())) return 0;
    const int perRow = sheet.width / cellW;
    const int total = perRow > 0 ? (sheet.height / cellH) * perRow : 0;
    if (perRow <= 0 || firstFrame + frameCount > total) return 0;

    engine::Image *img = new engine::Image();
    for (int k = 0; k < frameCount; ++k)
    {
        const int idx = firstFrame + k;
        engine::IntRect rect{(idx % perRow) * cellW, (idx / perRow) * cellH, cellW, cellH};
        engine::Pixmap frame(sheet, rect);
        img->addFrame(std::move(frame));
    }
    return z_store_image(img);
}

/* Gfx drivers. SetGfxDriver picks which display's modes CountGfxModes3D and
   GfxMode3DExists enumerate, which is what runtime3d keeps in g_driver. */
static int64_t z_gfx_driver = 0;

static inline void z_collect_gfx_modes()
{
    /* Cleared and refilled on every call, as runtime3d's collectGfxModes does:
       the list changes when Graphics3D sets a display mode, and only the two
       commands that call this (CountGfxModes3D, GfxMode3DExists) may see the
       new one. GfxMode3D and GfxModeWidth/Height/Depth only read the cache. */
    z_gfx_modes.clear();
    /* SDL_GetNumDisplayModes needs the video subsystem up, and a script may
       not have triggered it yet: CountGfxModes3D and Windowed3D have to be
       callable before the first Graphics - start.bb, the shared menu every
       3D sample runs through, calls them exactly that way. runtime3d's own
       collectGfxModes brings the subsystem up here too. */
    if (!SDL_WasInit(SDL_INIT_VIDEO) && SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) return;
    const int display = (int)z_gfx_driver;
    const int modeCount = SDL_GetNumDisplayModes(display);
    for (int index = 0; index < modeCount; ++index)
    {
        SDL_DisplayMode mode;
        if (SDL_GetDisplayMode(display, index, &mode) == 0)
            z_gfx_modes.push_back({mode.w, mode.h, (int)SDL_BITSPERPIXEL(mode.format)});
    }
}
inline void zen_set_gfx_driver(int64_t driver)
{
    const int n = (int)driver;
    /* driver_count() in runtime3d is the same count. */
    if (n < 1 || n > SDL_GetNumVideoDisplays())
    {
        zen_runtime_error("Illegal graphics driver index");
        return;
    }
    z_gfx_driver = n - 1;
    z_collect_gfx_modes();
}

inline int64_t zen_count_gfx_drivers() { return SDL_GetNumVideoDisplays(); }
inline int64_t zen_count_gfx_modes() { z_collect_gfx_modes(); return (int64_t)z_gfx_modes.size(); }
inline int64_t zen_gfx_mode_width(int64_t mode) { return mode > 0 && (size_t)mode <= z_gfx_modes.size() ? z_gfx_modes[(size_t)mode - 1].width : 0; }
inline int64_t zen_gfx_mode_height(int64_t mode) { return mode > 0 && (size_t)mode <= z_gfx_modes.size() ? z_gfx_modes[(size_t)mode - 1].height : 0; }
inline int64_t zen_gfx_mode_depth(int64_t mode) { return mode > 0 && (size_t)mode <= z_gfx_modes.size() ? z_gfx_modes[(size_t)mode - 1].depth : 0; }
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
    engine::Texture *t = z_texture_of(texture);
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

/* GraphicsBuffer/GraphicsDepth/GfxMode3D: the screen buffer is 1 and the
   depth 32, both constants here as in runtime3d. */
inline int64_t zen_graphics_buffer() { return kScreenBuffer; }
inline int64_t zen_graphics_depth() { return 32; }
inline int64_t zen_gfx_mode_3d(int64_t mode)
{
    /* GfxMode3D takes a mode *index*, 1..CountGfxModes3D(), not a size -
       GfxMode3DExists is the size test, and it is the wrapper above. Only
       reads the cache: runtime3d's c_GfxMode3D does not collect either, so a
       script that never asked for the count sees no modes. */
    return mode >= 1 && (size_t)mode <= z_gfx_modes.size() ? 1 : 0;
}
/* LoadBuffer: scale the image about to be read into the target buffer, one
   pixel at a time, and drop the colour-mask cache the same way runtime3d
   does. Only an image or texture buffer can be filled. */
inline int64_t zen_load_buffer(int64_t buffer, const std::string &file)
{
    engine::ImageFrame *f = z_buffer_frame(buffer);
    if (!f)
    {
        z_warn_screen_write("LoadBuffer");
        return 0;
    }
    engine::Pixmap loaded;
    if (!loaded.load(file.c_str())) return 0;

    const int w = f->pixels.width, h = f->pixels.height;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const int sx = loaded.width > 0 ? (x * loaded.width) / w : 0;
            const int sy = loaded.height > 0 ? (y * loaded.height) / h : 0;
            const engine::Color c = loaded.get_pixel_color((engine::u32)sx, (engine::u32)sy);
            f->pixels.set_pixel((engine::u32)x, (engine::u32)y, c.r(), c.g(), c.b(), c.a());
        }
    f->dirty = true;
    f->cmMask.clear();
    f->cmPitch = 0;
    return 1;
}
/* CopyRect: copied through a temporary so a rectangle overlapping itself
   inside one buffer still reads the original pixels. */
inline void zen_copy_rect(int64_t sx, int64_t sy, int64_t w, int64_t h, int64_t dx, int64_t dy,
                          int64_t src, int64_t dst)
{
    const int64_t source = z_buffer_arg(src), target = z_buffer_arg(dst);
    if (w <= 0 || h <= 0) return;
    if (!z_buffer_frame(target))
    {
        z_warn_screen_write("CopyRect");
        return;
    }
    int sw = 0, sh = 0;
    z_buffer_size(source, sw, sh);
    if (sx + w > sw) w = sw - sx;
    if (sy + h > sh) h = sh - sy;
    if (w <= 0 || h <= 0) return;

    std::vector<unsigned> tmp((size_t)w * (size_t)h, 0u);
    for (int64_t y = 0; y < h; ++y)
        for (int64_t x = 0; x < w; ++x)
            tmp[(size_t)y * (size_t)w + (size_t)x] = z_read_pixel(source, (int)(sx + x), (int)(sy + y));

    for (int64_t y = 0; y < h; ++y)
        for (int64_t x = 0; x < w; ++x)
            z_write_pixel(target, (int)(dx + x), (int)(dy + y), tmp[(size_t)y * (size_t)w + (size_t)x]);
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
inline void zen_entity_pick_mode(int64_t entity, int64_t mode, int64_t obscurer = 1)
{
    engine::Entity *e = z_entity(entity);
    engine::Object *o = e ? e->getObject() : nullptr;
    if (o)
    {
        o->setPickGeometry((int)mode);
        o->setObscurer(obscurer != 0);
    }
}
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
inline void zen_flip_mesh(int64_t mesh)
{
    engine::MeshModel *m = z_mesh_of(mesh);
    if (m) m->flipTriangles();
}

/* Sprites */
/* Sprites. The model behind the handle is an engine::Sprite, reached the way
   runtime3d's sprite_of does, and CreateSprite makes a fullbright one because
   a sprite has no lighting of its own. */
static inline engine::Sprite *z_sprite_of(int64_t handle)
{
    engine::Entity *e = z_entity(handle);
    return e ? e->getSprite() : nullptr;
}
inline int64_t zen_create_sprite(int64_t parent = 0)
{
    engine::Sprite *s = new engine::Sprite();
    s->setFX(engine::FxFullbright);
    return z_store_entity(s, z_entity(parent));
}
inline void zen_rotate_sprite(int64_t sprite, double angle)
{
    engine::Sprite *s = z_sprite_of(sprite);
    if (s) s->setRotation((float)angle * blitz::PI / 180.0f);
}
inline void zen_scale_sprite(int64_t sprite, double x, double y)
{
    engine::Sprite *s = z_sprite_of(sprite);
    if (s) s->setScale((float)x, (float)y);
}
inline void zen_handle_sprite(int64_t sprite, double x, double y)
{
    engine::Sprite *s = z_sprite_of(sprite);
    if (s) s->setHandle((float)x, (float)y);
}
inline void zen_sprite_view_mode(int64_t sprite, int64_t mode)
{
    engine::Sprite *s = z_sprite_of(sprite);
    if (s) s->setViewmode((int)mode);
}
/* AlignToVector: turn the entity's world rotation a fraction `rate` of the way
   towards the requested axis, every step mirroring runtime3d's c_AlignToVector
   including the antiparallel special case. */
inline void zen_align_to_vector(int64_t entity, double vx, double vy, double vz, int64_t axis,
                                double rate)
{
    engine::Entity *e = z_entity(entity);
    if (!e) return;

    engine::Vector axisVec((float)vx, (float)vy, (float)vz);
    const float len = axisVec.length();
    if (len <= blitz::EPSILON) return;
    axisVec = axisVec / len;

    engine::Quat q = e->getWorldRotation();
    const engine::Vector tv = axis == 1 ? q.i() : axis == 2 ? q.j() : q.k();
    const float dp = axisVec.dot(tv);
    if (dp >= 1.0f - blitz::EPSILON) return;

    if (dp <= -1.0f + blitz::EPSILON)
    {
        const float an = blitz::PI * (float)rate * 0.5f;
        const engine::Vector cp = axis == 1 ? q.j() : axis == 2 ? q.k() : q.i();
        e->setWorldRotation(engine::Quat(cosf(an), cp * sinf(an)) * q);
        return;
    }
    const float an = acosf(dp) * (float)rate * 0.5f;
    const engine::Vector cp = axisVec.cross(tv).normalized();
    e->setWorldRotation(engine::Quat(cosf(an), cp * sinf(an)) * q);
}

/* Input */
inline std::string zen_input(const std::string & = "") { return ""; }
inline void zen_enable_direct_input(int64_t) {}
inline int64_t zen_direct_input_enabled() { return 0; }

/* Mouse extras */
/* bbinput.cpp kept one previous value per axis and returned the difference, so
   the first call after a jump reports the whole move - and MoveMouse updates the
   reference, so warping the pointer does not read back as a huge MouseXSpeed.
   One set of state shared by the speed queries and MoveMouse, as runtime3d has. */
static int z_mouse_last_x = 0, z_mouse_last_y = 0, z_mouse_last_z = 0;

inline void zen_move_mouse(int64_t x, int64_t y)
{
    /* Only when the pointer actually moved, as runtime3d's c_MoveMouse does. */
    if (z_platform.moveMouse((int)x, (int)y))
    {
        z_mouse_last_x = (int)x;
        z_mouse_last_y = (int)y;
    }
}

/* Sound (stubs) */
inline int64_t zen_load_sound(const std::string &) { return 0; }
inline int64_t zen_load_3d_sound(const std::string &) { return 0; }

/* Terrain and MD2. terrain_of/md2_of are the model casts runtime3d performs,
   and LoadTerrain reads a square, power-of-two heightmap the same way. */
static inline engine::Terrain *z_terrain_of(int64_t handle)
{
    engine::Entity *e = z_entity(handle);
    auto *model = e ? e->getModel() : nullptr;
    return model ? model->getTerrain() : nullptr;
}
static inline engine::MD2Model *z_md2_of(int64_t handle)
{
    engine::Entity *e = z_entity(handle);
    auto *model = e ? e->getModel() : nullptr;
    return model ? model->getMD2Model() : nullptr;
}
/* The world-space point on the terrain surface under x,z: bilinear between the
   four heights around it, exactly as runtime3d's terrain_vector computes it. */
static inline engine::Vector z_terrain_vector(engine::Terrain *terrain, float x, float y, float z)
{
    engine::Vector local = -terrain->getWorldTform() * engine::Vector(x, y, z);
    const int ix = (int)floorf(local.x), iz = (int)floorf(local.z);
    const float tx = local.x - (float)ix, tz = local.z - (float)iz;
    const float h0 = terrain->getHeight(ix, iz), h1 = terrain->getHeight(ix + 1, iz);
    const float h2 = terrain->getHeight(ix, iz + 1), h3 = terrain->getHeight(ix + 1, iz + 1);
    const float ha = (h1 - h0) * tx + h0, hb = (h3 - h2) * tx + h2;
    return terrain->getWorldTform() * engine::Vector(local.x, (hb - ha) * tz + ha, local.z);
}
inline double zen_terrain_x(int64_t terrain, double x, double y, double z)
{
    engine::Terrain *t = z_terrain_of(terrain);
    return t ? z_terrain_vector(t, (float)x, (float)y, (float)z).x : 0.0;
}
inline double zen_terrain_y(int64_t terrain, double x, double y, double z)
{
    engine::Terrain *t = z_terrain_of(terrain);
    return t ? z_terrain_vector(t, (float)x, (float)y, (float)z).y : 0.0;
}
inline double zen_terrain_z(int64_t terrain, double x, double y, double z)
{
    engine::Terrain *t = z_terrain_of(terrain);
    return t ? z_terrain_vector(t, (float)x, (float)y, (float)z).z : 0.0;
}
inline double zen_terrain_height(int64_t terrain, int64_t x, int64_t y)
{
    engine::Terrain *t = z_terrain_of(terrain);
    return t ? t->getHeight((int)x, (int)y) : 0.0;
}
inline int64_t zen_terrain_size(int64_t terrain)
{
    engine::Terrain *t = z_terrain_of(terrain);
    return t ? t->getSize() : 0;
}
inline void zen_modify_terrain(int64_t terrain, int64_t x, int64_t y, double height, int64_t realtime)
{
    if (engine::Terrain *t = z_terrain_of(terrain))
        t->setHeight((int)x, (int)y, (float)height, realtime != 0);
}
inline void zen_terrain_shading(int64_t terrain, int64_t enable)
{
    if (engine::Terrain *t = z_terrain_of(terrain)) t->setShading(enable != 0);
}
inline void zen_terrain_detail(int64_t terrain, int64_t detail, int64_t enable)
{
    if (engine::Terrain *t = z_terrain_of(terrain)) t->setDetail((int)detail, enable != 0);
}
inline void zen_animate_md2(int64_t md2, int64_t mode = 1, double speed = 1.0, int64_t firstFrame = 0,
                            int64_t lastFrame = 9999, double transition = 0.0)
{
    /* runtime3d passes them to startMD2Anim as first, last, mode, speed and
       transition - not in the order the command takes them. */
    if (engine::MD2Model *m = z_md2_of(md2))
        m->startMD2Anim((int)firstFrame, (int)lastFrame, (int)mode, (float)speed, (float)transition);
}
inline int64_t zen_load_terrain(const std::string &file, int64_t parent = 0)
{
    /* Blitz3D shipped on Windows, so scripts assume case-insensitive paths -
       the same resolveCaseInsensitive step runtime3d's c_LoadTerrain takes. */
    engine::Pixmap pixels;
    if (!pixels.load(engine::resolveCaseInsensitive(file.c_str()).c_str()) ||
        pixels.width != pixels.height || pixels.width < 2)
        return 0;
    int shift = 0;
    while ((1 << shift) < pixels.width) ++shift;
    if (shift > 12 || (1 << shift) != pixels.width) return 0;

    /* Only when a device exists: runtime3d's c_LoadTerrain never touches the
       world, and a script may legitimately load a heightmap before the first
       Graphics3D, when there is no device to build the world's renderers
       with. The terrain model uploads itself at the first render. */
    if (z_graphics_open) z_world_ensure();
    engine::Terrain *terrain = new engine::Terrain(shift);
    for (int y = 0; y < pixels.height; ++y)
        for (int x = 0; x < pixels.width; ++x)
        {
            const engine::Color color = pixels.get_pixel_color((engine::u32)x, (engine::u32)y);
            int top = color.r() > color.g() ? color.r() : color.g();
            if (color.b() > top) top = color.b();
            terrain->setHeight(x, pixels.height - 1 - y, top / 255.0f, false);
        }
    return z_store_entity(terrain, z_entity(parent));
}
/* Defined with the rest of the loaders further down; LoadMD2 is LoadMesh for a
   .md2, which is what engine::MD2Model is picked for by extension. */
inline int64_t zen_load_mesh(const std::string &file, int64_t parent);
inline int64_t zen_load_md2(const std::string &file, int64_t parent = 0)
{
    /* zen_load_mesh already routes .md2 to engine::MD2Model and warns on a
       model that failed to validate. */
    return zen_load_mesh(file, parent);
}

/* Brushes and surfaces.
   runtime3d hands the engine pointer itself across as the Blitz3D handle
   (c_CreateBrush, c_CreateSurface and c_VertexCoords all cast the argument
   instead of looking it up), so these wrappers do the same. */
static inline engine::Brush *z_brush_of(int64_t handle) { return (engine::Brush *)(std::intptr_t)handle; }
static inline engine::Surface *z_surface_of(int64_t handle) { return (engine::Surface *)(std::intptr_t)handle; }

inline int64_t zen_create_brush(double red = 255.0, double green = 255.0, double blue = 255.0)
{
    engine::Brush *brush = new engine::Brush();
    brush->setColor(engine::Vector((float)red / 255.0f, (float)green / 255.0f, (float)blue / 255.0f));
    return (int64_t)(std::intptr_t)brush;
}
inline void zen_brush_color(int64_t brush, double red, double green, double blue)
{
    engine::Brush *b = z_brush_of(brush);
    if (b) b->setColor(engine::Vector((float)red / 255.0f, (float)green / 255.0f, (float)blue / 255.0f));
}
inline void zen_brush_blend(int64_t brush, int64_t blend)
{
    engine::Brush *b = z_brush_of(brush);
    if (b) b->setBlend((int)blend);
}
inline void zen_brush_fx(int64_t brush, int64_t fx)
{
    engine::Brush *b = z_brush_of(brush);
    if (b) b->setFX((int)fx);
}

/* Vertex */
inline int64_t zen_add_vertex(int64_t surface, double x, double y, double z, double u = 0, double v = 0,
                              double w = 1)
{
    /* Mirrors c_AddVertex, which ignores w as well. */
    (void)w;
    engine::Surface *s = z_surface_of(surface);
    if (!s) return 0;
    engine::Surface::Vertex vertex;
    vertex.coords = engine::Vector((float)x, (float)y, (float)z);
    vertex.texCoords[0][0] = (float)u;
    vertex.texCoords[0][1] = (float)v;
    return s->addVertex(vertex);
}
inline int64_t zen_add_triangle(int64_t surface, int64_t v0, int64_t v1, int64_t v2)
{
    engine::Surface *s = z_surface_of(surface);
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
    engine::Surface *s = z_surface_of(surface);
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
inline int64_t zen_camera_pick(int64_t camera, double x, double y);

/* Entity extras */
inline void zen_entity_radius(int64_t entity, double x_radius, double y_radius = 0)
{
    /* bbEntityRadius: a zero y_radius means "use x_radius", and radii.z
       mirrors x because collision is an ellipsoid of revolution around Y. */
    engine::Object *o = z_entity(entity) ? z_entity(entity)->getObject() : nullptr;
    if (!o) return;
    const float rx = (float)x_radius;
    const float ry = (float)(y_radius != 0.0 ? y_radius : x_radius);
    o->setCollisionRadii(engine::Vector(rx, ry, rx));
}
inline void zen_entity_autofade(int64_t entity, double near, double far)
{
    engine::Entity *e = z_entity(entity);
    engine::Model *m = e ? e->getModel() : nullptr;
    if (m) m->setAutoFade((float)near, (float)far);
}
inline void zen_entity_parent(int64_t entity, int64_t parent, int64_t = 1)
{
    engine::Entity *e = z_entity(entity);
    engine::Entity *p = z_entity(parent);
    if (e) e->setParent(p);
}
inline void zen_name_entity(int64_t entity, const std::string &name)
{
    engine::Entity *e = z_entity(entity);
    if (e) e->setName(name.c_str());
}
inline std::string zen_entity_name(int64_t entity)
{
    engine::Entity *e = z_entity(entity);
    return e ? std::string(e->getName().c_str()) : std::string();
}
inline std::string zen_entity_class(int64_t h)
{
    engine::Entity *e = z_entity(h);
    if (!e) return "";
    return e->getCamera() ? "Camera" : e->getLight() ? "Light" :
           e->getModel() ? "Mesh" : e->getSprite() ? "Sprite" : "Pivot";
}
inline void zen_paint_entity(int64_t entity, int64_t brush)
{
    engine::Entity *e = z_entity(entity);
    engine::Model *m = e ? e->getModel() : nullptr;
    engine::Brush *b = z_brush_of(brush);
    if (m && b) m->setBrush(*b);
}

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

/* Images extras, each mirroring runtime3d's command of the same name. */
inline void zen_mid_handle(int64_t image)
{
    engine::Image *img = z_image_of(image);
    if (img) img->setMidHandle();
}
inline void zen_auto_mid_handle(int64_t enable) { z_auto_mid_handle = enable != 0; }
inline int64_t zen_copy_image(int64_t image)
{
    engine::Image *src = z_image_of(image);
    if (!src || src->frameCount() == 0) return 0;
    engine::Image *img = new engine::Image();
    for (int k = 0; k < src->frameCount(); ++k)
    {
        const engine::Pixmap &sp = src->frame(k).pixels;
        engine::IntRect all{0, 0, sp.width, sp.height};
        engine::Pixmap copy(sp, all);
        img->addFrame(std::move(copy));
    }
    /* The copy carries the handle and the mask across, and AutoMidHandle must
       not override an explicit handle - runtime3d disables the flag for the
       store and restores it afterwards. */
    const bool saved = z_auto_mid_handle;
    z_auto_mid_handle = false;
    const int64_t handle = z_store_image(img);
    z_auto_mid_handle = saved;
    img->setHandle(src->handleX(), src->handleY());
    img->setMask(src->mask());
    return handle;
}
inline int64_t zen_save_image(int64_t image, const std::string &bmpfile, int64_t frame = 0)
{
    engine::Image *img = z_image_of(image);
    bool ok = false;
    if (img && img->frameCount() > 0) ok = img->frame((int)frame).pixels.save(bmpfile.c_str());
    return ok ? 1 : 0;
}
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
/* ================= Image tiling and transform ================= */

/* The engine's draw_frame: upload the frame, then draw a sub-rectangle of it at
   x,y - the batch renderer takes normalised source coordinates - centred on the
   handle. Solid lands without blending, which is what DrawBlock and TileBlock
   ask for. */
static inline void z_draw_frame(engine::Image *img, float x, float y, int frame, float sx, float sy,
                                float sw, float sh, bool solid)
{
    if (!img->ensureUploaded(z_platform.device(), frame)) return;
    const engine::ImageFrame &f = img->frame(frame);
    const float fw = (float)f.pixels.width, fh = (float)f.pixels.height;
    if (fw <= 0.0f || fh <= 0.0f) return;

    if (sw <= 0.0f || sh <= 0.0f) { sx = 0.0f; sy = 0.0f; sw = fw; sh = fh; }

    engine::BatchRenderer &batch = z_platform.batch();
    if (solid) batch.setBlend(false);
    batch.setColor((unsigned char)255, (unsigned char)255, (unsigned char)255);
    batch.drawTexture(f.texture, x - (float)img->handleX(), y - (float)img->handleY(), sw, sh,
                      sx / fw, sy / fh, sw / fw, sh / fh);
    if (solid) batch.setBlend(true);
}
/* The tiling both TileImage and TileBlock use: start one whole image up and to
   the left of the target, stepping by the image size until the viewport is
   covered, with the origin shifted so the pattern starts at the given point. */
static inline void z_tile(engine::Image *img, int64_t x, int64_t y, int64_t frame, bool solid)
{
    if (!img) return;
    const int w = img->width(), h = img->height();
    if (w <= 0 || h <= 0) return;

    const int vpW = z_platform.width(), vpH = z_platform.height();
    int px = (int)x, py = (int)y;
    int dx = img->handleX(), dy = img->handleY();
    px -= dx;
    py -= dy;
    dx += (px >= 0 ? px % w : w - (-px % w));
    dy += (py >= 0 ? py % h : h - (-py % h));

    for (int ty = -h; ty < vpH; ty += h)
        for (int tx = -w; tx < vpW; tx += w)
            z_draw_frame(img, (float)(tx + dx + img->handleX()),
                         (float)(ty + dy + img->handleY()), (int)frame, 0.0f, 0.0f, 0.0f, 0.0f,
                         solid);
}
inline void zen_tile_image(int64_t image, int64_t x = 0, int64_t y = 0, int64_t frame = 0)
{
    z_tile(z_image_of(image), x, y, frame, false);
}
inline void zen_tile_block(int64_t image, int64_t x = 0, int64_t y = 0, int64_t frame = 0)
{
    z_tile(z_image_of(image), x, y, frame, true);
}

/* TFormImage and the two commands built on it. Each frame is resampled through
   the inverse of the 2x2 matrix, bilinearly while TFormFilter is on (it starts
   on, as runtime3d's g_tformFilter does) or by nearest pixel when it is off, and
   the handle moves with the new bounding box. */
static bool z_tform_filter = true;

static inline engine::Color z_sample_filtered(const engine::Pixmap &p, float x, float y)
{
    x -= 0.5f;
    y -= 0.5f;
    const float fx0 = std::floor(x), fy0 = std::floor(y);
    const int ix = (int)fx0, iy = (int)fy0;
    const float fx = x - fx0, fy = y - fy0;

    auto at = [&](int px, int py) {
        if (px < 0) px = 0; else if (px >= p.width) px = p.width - 1;
        if (py < 0) py = 0; else if (py >= p.height) py = p.height - 1;
        return p.get_pixel_color((engine::u32)px, (engine::u32)py);
    };
    const engine::Color tl = at(ix, iy), tr = at(ix + 1, iy);
    const engine::Color bl = at(ix, iy + 1), br = at(ix + 1, iy + 1);

    const float w1 = (1 - fx) * (1 - fy), w2 = fx * (1 - fy);
    const float w3 = (1 - fx) * fy, w4 = fx * fy;
    auto mix = [&](int c1, int c2, int c3, int c4) {
        return (unsigned char)(c1 * w1 + c2 * w2 + c3 * w3 + c4 * w4 + 0.5f);
    };
    return engine::Color(mix(tl.r(), tr.r(), bl.r(), br.r()), mix(tl.g(), tr.g(), bl.g(), br.g()),
                         mix(tl.b(), tr.b(), bl.b(), br.b()), mix(tl.a(), tr.a(), bl.a(), br.a()));
}
static inline void z_ensure_rgba(engine::Pixmap &p)
{
    if (!p.is_valid() || p.components == 4) return;
    engine::Pixmap *rgba = p.convert_to_rgba();
    if (!rgba) return;
    p = std::move(*rgba);
    delete rgba;
}
static inline bool z_tform_frame(engine::Image *img, int k, const float m[2][2])
{
    engine::ImageFrame &f = img->frame(k);
    engine::Pixmap &src = f.pixels;
    if (!src.is_valid()) return false;

    const float det = m[0][0] * m[1][1] - m[1][0] * m[0][1];
    if (std::fabs(det) < 1e-8f) return false;
    const float dt = 1.0f / det;
    float inv[2][2];
    inv[0][0] = dt * m[1][1];
    inv[1][0] = -dt * m[1][0];
    inv[0][1] = -dt * m[0][1];
    inv[1][1] = dt * m[0][0];

    /* Where the four corners land decides the size of the result. */
    const float ox = (float)img->handleX(), oy = (float)img->handleY();
    const float cx[4] = {-ox, (float)src.width - ox, (float)src.width - ox, -ox};
    const float cy[4] = {-oy, -oy, (float)src.height - oy, (float)src.height - oy};
    float minX = 0, minY = 0, maxX = 0, maxY = 0;
    for (int i = 0; i < 4; ++i)
    {
        const float tx = m[0][0] * cx[i] + m[0][1] * cy[i];
        const float ty = m[1][0] * cx[i] + m[1][1] * cy[i];
        if (i == 0)
        {
            minX = maxX = tx;
            minY = maxY = ty;
        }
        else
        {
            if (tx < minX) minX = tx;
            if (tx > maxX) maxX = tx;
            if (ty < minY) minY = ty;
            if (ty > maxY) maxY = ty;
        }
    }
    minX = std::floor(minX);
    minY = std::floor(minY);
    maxX = std::ceil(maxX);
    maxY = std::ceil(maxY);
    const int iw = (int)(maxX - minX), ih = (int)(maxY - minY);
    if (iw <= 0 || ih <= 0) return false;

    z_ensure_rgba(src);
    engine::Pixmap out(iw, ih, 4);
    for (int y = 0; y < ih; ++y)
    {
        const float vy = minY + (float)y + 0.5f;
        for (int x = 0; x < iw; ++x)
        {
            const float vx = minX + (float)x + 0.5f;
            const float qx = inv[0][0] * vx + inv[0][1] * vy;
            const float qy = inv[1][0] * vx + inv[1][1] * vy;
            engine::Color c;
            if (z_tform_filter) c = z_sample_filtered(src, qx + ox, qy + oy);
            else
            {
                const int sx = (int)std::floor(qx + ox), sy = (int)std::floor(qy + oy);
                c = (sx >= 0 && sy >= 0 && sx < src.width && sy < src.height)
                        ? src.get_pixel_color((engine::u32)sx, (engine::u32)sy)
                        : engine::Color(0, 0, 0, 0);
            }
            out.set_pixel((engine::u32)x, (engine::u32)y, c.r(), c.g(), c.b(), c.a());
        }
    }
    f.pixels = std::move(out);
    f.dirty = true;
    f.cmMask.clear();
    f.cmPitch = 0;
    img->setHandle((int)-minX, (int)-minY);
    return true;
}
inline void zen_tform_image(int64_t image, double a, double b, double c, double d)
{
    engine::Image *img = z_image_of(image);
    if (!img) return;
    float m[2][2];
    m[0][0] = (float)a;
    m[1][0] = (float)b;
    m[0][1] = (float)c;
    m[1][1] = (float)d;
    /* The handle moves with the first frame; every frame is the same size, so
       they all land on the same new handle. */
    const int hx = img->handleX(), hy = img->handleY();
    for (int k = 0; k < img->frameCount(); ++k)
    {
        img->setHandle(hx, hy);
        z_tform_frame(img, k, m);
    }
}
inline void zen_scale_image(int64_t image, double xscale, double yscale)
{
    zen_tform_image(image, xscale, 0.0, 0.0, yscale);
}
inline void zen_rotate_image(int64_t image, double angle)
{
    /* Negated and converted to radians, as runtime3d's c_RotateImage does. */
    const float d = -(float)angle * kDegToRad;
    zen_tform_image(image, std::cos(d), -std::sin(d), std::sin(d), std::cos(d));
}
inline void zen_tform_filter(int64_t enable) { z_tform_filter = enable != 0; }

/* MaskImage and ResizeImage build on the transform above, and DrawBlockRect on
   the frame blit, which is why they live here rather than with the other
   image extras. */
static inline void z_apply_mask_alpha(engine::Pixmap &p, unsigned char r, unsigned char g,
                                      unsigned char b)
{
    z_ensure_rgba(p);
    if (!p.is_valid() || p.components != 4) return;
    for (int y = 0; y < p.height; ++y)
        for (int x = 0; x < p.width; ++x)
        {
            const engine::Color c = p.get_pixel_color((engine::u32)x, (engine::u32)y);
            const bool masked = c.r() == r && c.g() == g && c.b() == b;
            p.set_pixel((engine::u32)x, (engine::u32)y, c.r(), c.g(), c.b(), masked ? 0 : 255);
        }
}
inline void zen_mask_image(int64_t image, int64_t red, int64_t green, int64_t blue)
{
    /* Blitz3D masked at blit time through a colour key, so MaskImage could be
       called again with a different colour and the original pixels were never
       lost: the key is recorded and the alpha rebuilt from the untouched RGB
       each time, exactly as runtime3d does it. */
    engine::Image *img = z_image_of(image);
    if (!img) return;
    const unsigned char r = (unsigned char)red, g = (unsigned char)green, b = (unsigned char)blue;
    img->setMask(((std::uint32_t)r << 16) | ((std::uint32_t)g << 8) | b);
    for (int i = 0; i < img->frameCount(); ++i)
    {
        engine::ImageFrame &f = img->frame(i);
        z_apply_mask_alpha(f.pixels, r, g, b);
        f.dirty = true;
    }
}
inline void zen_resize_image(int64_t image, double width, double height)
{
    engine::Image *img = z_image_of(image);
    if (!img || img->width() <= 0 || img->height() <= 0) return;
    const float sx = (float)width / (float)img->width();
    const float sy = (float)height / (float)img->height();
    zen_tform_image(image, sx, 0.0, 0.0, sy);
}
inline void zen_draw_block_rect(int64_t image, int64_t x, int64_t y, int64_t rectX, int64_t rectY,
                                int64_t rectW, int64_t rectH, int64_t frame = 0)
{
    engine::Image *img = z_image_of(image);
    if (!img) return;
    z_draw_frame(img, (float)x, (float)y, (int)frame, (float)rectX, (float)rectY, (float)rectW,
                 (float)rectH, true);
}

/* ================= Odds and ends ================= */

inline int64_t zen_scan_line() { return 0; }   /* no raster line to report */
inline void zen_camera_cls_mode(int64_t camera, int64_t clsColor, int64_t clsZBuffer)
{
    engine::Entity *e = z_entity(camera);
    if (e && e->getCamera()) e->getCamera()->setClsMode(clsColor != 0, clsZBuffer != 0);
}
inline int64_t zen_create_terrain(int64_t gridSize, int64_t parent = 0)
{
    /* The grid size has to be a power of two, as in runtime3d's c_CreateTerrain;
       the world is only built when there is a device to build it with. */
    const int size = (int)gridSize;
    int shift = 0;
    while ((1 << shift) < size) ++shift;
    if (size < 2 || shift > 12 || (1 << shift) != size) return 0;
    if (z_graphics_open) z_world_ensure();
    engine::Terrain *terrain = new engine::Terrain(shift);
    return z_store_entity(terrain, z_entity(parent));
}
inline int64_t zen_meshes_intersect(int64_t meshA, int64_t meshB)
{
    engine::MeshModel *a = z_mesh_of(meshA);
    engine::MeshModel *b = z_mesh_of(meshB);
    return (a && b && a->intersects(*b)) ? 1 : 0;
}

/* Pixels. The plain forms honour the canvas origin and viewport and the Fast
   ones do not, exactly as runtime3d splits them; a 0 buffer means the one
   SetBuffer selected. */
inline void zen_write_pixel_fast(int64_t x, int64_t y, int64_t argb, int64_t buffer = 0)
{
    if (!z_write_pixel(z_buffer_arg(buffer), (int)x, (int)y, (unsigned)argb))
        z_warn_screen_write("WritePixelFast");
}
inline void zen_write_pixel(int64_t x, int64_t y, int64_t argb, int64_t buffer = 0)
{
    const int px = (int)x + z_origin_x, py = (int)y + z_origin_y;
    if (!z_pixel_inside_viewport(px, py)) return;
    if (!z_write_pixel(z_buffer_arg(buffer), px, py, (unsigned)argb)) z_warn_screen_write("WritePixel");
}
inline int64_t zen_read_pixel(int64_t x, int64_t y, int64_t buffer = 0)
{
    const int px = (int)x + z_origin_x, py = (int)y + z_origin_y;
    if (!z_pixel_inside_viewport(px, py)) return 0;
    return (int64_t)(int)z_read_pixel(z_buffer_arg(buffer), px, py);
}
inline int64_t zen_read_pixel_fast(int64_t x, int64_t y, int64_t buffer = 0)
{
    return (int64_t)(int)z_read_pixel(z_buffer_arg(buffer), (int)x, (int)y);
}
inline void zen_get_color(int64_t x, int64_t y)
{
    /* GetColor reads the current buffer and makes that the drawing colour,
       with the alpha forced opaque. */
    const int px = (int)x + z_origin_x, py = (int)y + z_origin_y;
    z_draw_color = z_read_pixel(z_canvas_buffer, px, py) | 0xff000000u;
    z_platform.batch().setColor((unsigned char)((z_draw_color >> 16) & 0xff),
                                (unsigned char)((z_draw_color >> 8) & 0xff),
                                (unsigned char)(z_draw_color & 0xff));
}
inline int64_t zen_color_red() { return (int64_t)((z_draw_color >> 16) & 0xff); }inline int64_t zen_color_green() { return (int64_t)((z_draw_color >> 8) & 0xff); }
inline int64_t zen_color_blue() { return (int64_t)(z_draw_color & 0xff); }
inline void zen_buffer_dirty(int64_t buffer)
{
    /* BufferDirty told Blitz3D a locked surface had changed behind its back;
       the frame's own flag is what schedules the upload here. */
    engine::ImageFrame *f = z_buffer_frame(z_buffer_arg(buffer));
    if (f) f->dirty = true;
}
inline void zen_copy_pixel(int64_t sx, int64_t sy, int64_t src_buffer, int64_t dx, int64_t dy,
                           int64_t dst_buffer = 0)
{
    const unsigned argb = z_read_pixel(z_buffer_arg(src_buffer), (int)sx, (int)sy);
    if (!z_write_pixel(z_buffer_arg(dst_buffer), (int)dx, (int)dy, argb)) z_warn_screen_write("CopyPixel");
}
inline void zen_copy_pixel_fast(int64_t sx, int64_t sy, int64_t src_buffer, int64_t dx, int64_t dy,
                                int64_t dst_buffer = 0)
{
    /* CopyPixelFast is the same call in runtime3d too. */
    zen_copy_pixel(sx, sy, src_buffer, dx, dy, dst_buffer);
}
inline void zen_wireframe(int64_t) {}

inline int64_t zen_mouse_x() { return z_platform.mouseX(); }
inline int64_t zen_mouse_y() { return z_platform.mouseY(); }
inline int64_t zen_mouse_down(int64_t b) { return z_platform.mouseDown((int)b) ? 1 : 0; }
inline int64_t zen_mouse_hit(int64_t b) { return z_platform.mouseHit((int)b); }
inline int64_t zen_mouse_z() { return z_platform.mouseZ(); }

/* Mouse speed, GetMouse, FlushMouse and MouseWait, mirroring runtime3d's own;
   the speed is the difference against z_mouse_last_*, updated here. */
inline int64_t zen_mouse_x_speed()
{
    const int x = z_platform.mouseX();
    const int delta = x - z_mouse_last_x;
    z_mouse_last_x = x;
    return delta;
}
inline int64_t zen_mouse_y_speed()
{
    const int y = z_platform.mouseY();
    const int delta = y - z_mouse_last_y;
    z_mouse_last_y = y;
    return delta;
}
inline int64_t zen_mouse_z_speed()
{
    const int z = z_platform.mouseZ();
    const int delta = z - z_mouse_last_z;
    z_mouse_last_z = z;
    return delta;
}
/* GetMouse pops one entry off the device queue, as bbGetMouse did. */
inline int64_t zen_get_mouse() { return z_platform.popMouseButton(); }
inline void zen_flush_mouse() { z_platform.flushMouseHits(); }
inline int64_t zen_mouse_wait()
{
    /* bbWaitMouse returned which button was pressed, pumping events until one
       arrives - but only while the window is open, so a headless run returns
       straight away instead of spinning. */
    bool wasDown[4] = {false, false, false, false};
    for (int b = 1; b <= 3; ++b) wasDown[b] = z_platform.mouseDown(b);
    int pressed = 0;
    for (;;)
    {
        z_platform.pumpEvents();
        if (!z_platform.isOpen()) break;
        for (int b = 1; b <= 3; ++b)
        {
            const bool down = z_platform.mouseDown(b);
            if (down && !wasDown[b]) pressed = b;
            wasDown[b] = down;
        }
        if (pressed) break;
        SDL_Delay(1);   /* poll at ~1 kHz, not a busy spin */
    }
    return pressed;
}
/* No joystick is ever opened, so every query answers the way Blitz3D answered
   for a port with nothing plugged into it - the constants runtime3d uses.
   Defaults cover the commands that take a port, a button or neither. */
inline int64_t zen_joy_zero(int64_t = 0, int64_t = 0) { return 0; }
inline double zen_joy_zero_float(int64_t = 0) { return 0.0; }
inline void zen_flush_joy() {}
inline int64_t zen_joy_wait(int64_t = 0) { return 0; }

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
    return z_store_image(image);
}

inline int64_t zen_image_buffer(int64_t image, int64_t frame = 0)
{
    engine::Image *img = z_image_of(image);
    return img && frame >= 0 && frame < img->frameCount() ? z_buffer_handle(image, (int)frame, 0) : 0;
}

inline void zen_free_image(int64_t image)
{
    auto it = z_image_list.find(image);
    if (it == z_image_list.end()) return;
    delete it->second;
    z_image_list.erase(it);
}

/* Mesh extras */
inline void zen_fit_mesh(int64_t mesh, double x, double y, double z, double w, double h, double d,
                         int64_t uniform = 0)
{
    engine::MeshModel *m = z_mesh_of(mesh);
    if (m) engine::MeshUtil::fitMesh(m, engine::Vector((float)x, (float)y, (float)z),
                                     engine::Vector((float)w, (float)h, (float)d), uniform != 0);
}
inline void zen_paint_mesh(int64_t mesh, int64_t brush)
{
    /* PaintMesh and PaintEntity differ in where the brush lands: the mesh's
       own surfaces, or every model in the entity's tree. */
    engine::MeshModel *m = z_mesh_of(mesh);
    engine::Brush *b = z_brush_of(brush);
    if (m && b) m->paint(*b);
}
inline int64_t zen_create_surface(int64_t mesh, int64_t brush = 0)
{
    /* The handle is the engine::Surface pointer, as in c_CreateSurface. */
    engine::Entity *e = z_entity(mesh);
    engine::MeshModel *m = (e && e->getModel()) ? e->getModel()->getMeshModel() : nullptr;
    if (!m) return 0;
    engine::Brush *b = z_brush_of(brush);
    return (int64_t)(std::intptr_t)m->createSurface(b ? *b : engine::Brush());
}

/* Collisions */
inline void zen_collisions(int64_t src_type, int64_t dst_type, int64_t method, int64_t response)
{
    z_world.addCollision((int)src_type, (int)dst_type, (int)method, (int)response);
}

/* The collision list an entity carries after UpdateWorld, 1-based like
   runtime3d's collisionAt, with the same 0/-1 fallbacks when there is no
   such collision. */
static inline const engine::ObjCollision *z_collision_at(int64_t entity, int64_t index)
{
    engine::Entity *e = z_entity(entity);
    engine::Object *o = e ? e->getObject() : nullptr;
    if (!o || index < 1 || (size_t)index > o->getCollisions().size()) return nullptr;
    return o->getCollisions()[(size_t)index - 1];
}
inline int64_t zen_count_collisions(int64_t entity)
{
    engine::Entity *e = z_entity(entity);
    engine::Object *o = e ? e->getObject() : nullptr;
    return o ? (int64_t)o->getCollisions().size() : 0;
}
inline int64_t zen_collision_entity(int64_t entity, int64_t index)
{
    const engine::ObjCollision *c = z_collision_at(entity, index);
    return c ? z_handle_of(c->with) : 0;
}
inline double zen_collision_x(int64_t entity, int64_t index)
{
    const engine::ObjCollision *c = z_collision_at(entity, index);
    return c ? c->coords.x : 0.0;
}
inline double zen_collision_y(int64_t entity, int64_t index)
{
    const engine::ObjCollision *c = z_collision_at(entity, index);
    return c ? c->coords.y : 0.0;
}
inline double zen_collision_z(int64_t entity, int64_t index)
{
    const engine::ObjCollision *c = z_collision_at(entity, index);
    return c ? c->coords.z : 0.0;
}
inline double zen_collision_nx(int64_t entity, int64_t index)
{
    const engine::ObjCollision *c = z_collision_at(entity, index);
    return c ? c->collision.normal.x : 0.0;
}
inline double zen_collision_ny(int64_t entity, int64_t index)
{
    const engine::ObjCollision *c = z_collision_at(entity, index);
    return c ? c->collision.normal.y : 0.0;
}
inline double zen_collision_nz(int64_t entity, int64_t index)
{
    const engine::ObjCollision *c = z_collision_at(entity, index);
    return c ? c->collision.normal.z : 0.0;
}
inline int64_t zen_entity_collided(int64_t entity, int64_t type)
{
    /* The first collision with an object of the given type, as c_EntityCollided
       reports it. */
    engine::Entity *e = z_entity(entity);
    engine::Object *o = e ? e->getObject() : nullptr;
    if (!o) return 0;
    for (const engine::ObjCollision *c : o->getCollisions())
        if (c && c->with && c->with->getCollisionType() == (int)type) return z_handle_of(c->with);
    return 0;
}

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

/* ================= Transform queries ================= */

/* runtime3d keeps one global g_tformed that TFormedX/Y/Z read back. A point
   goes through both transforms, a vector ignores translation, and a normal
   uses the cofactor (inverse-transpose) matrix so it stays perpendicular to
   the surface under non-uniform scaling - then it is renormalised. */
inline void zen_tform_point(double x, double y, double z, int64_t src = 0, int64_t dest = 0)
{
    engine::Entity *from = z_entity(src);
    engine::Entity *to = z_entity(dest);
    z_tformed_state = engine::Vector((float)x, (float)y, (float)z);
    if (from) z_tformed_state = from->getWorldTform() * z_tformed_state;
    if (to) z_tformed_state = -to->getWorldTform() * z_tformed_state;
}
inline void zen_tform_vector(double x, double y, double z, int64_t src = 0, int64_t dest = 0)
{
    engine::Entity *from = z_entity(src);
    engine::Entity *to = z_entity(dest);
    z_tformed_state = engine::Vector((float)x, (float)y, (float)z);
    if (from) z_tformed_state = from->getWorldTform().m * z_tformed_state;
    if (to) z_tformed_state = (-to->getWorldTform()).m * z_tformed_state;
}
inline void zen_tform_normal(double x, double y, double z, int64_t src = 0, int64_t dest = 0)
{
    engine::Entity *from = z_entity(src);
    engine::Entity *to = z_entity(dest);
    z_tformed_state = engine::Vector((float)x, (float)y, (float)z);
    if (from) z_tformed_state = from->getWorldTform().m.cofactor() * z_tformed_state;
    if (to) z_tformed_state = (-to->getWorldTform()).m.cofactor() * z_tformed_state;
    const float length = z_tformed_state.length();
    if (length > blitz::EPSILON) z_tformed_state = z_tformed_state / length;
}
inline double zen_tformed_x() { return z_tformed_state.x; }
inline double zen_tformed_y() { return z_tformed_state.y; }
inline double zen_tformed_z() { return z_tformed_state.z; }

inline void zen_translate_entity(int64_t entity, double x, double y, double z, int64_t global = 0)
{
    engine::Entity *e = z_entity(entity);
    if (!e) return;
    engine::Vector v((float)x, (float)y, (float)z);
    global ? e->setWorldPosition(e->getWorldPosition() + v)
           : e->setLocalPosition(e->getLocalPosition() + v);
}
inline double zen_entity_distance(int64_t entity, int64_t other)
{
    engine::Entity *a = z_entity(entity);
    engine::Entity *b = z_entity(other);
    return a && b ? a->getWorldPosition().distance(b->getWorldPosition()) : 0.0f;
}
inline void zen_reset_entity(int64_t entity)
{
    engine::Entity *e = z_entity(entity);
    if (e && e->getObject()) e->getObject()->reset();
}

/* ================= Animator queries ================= */

static inline engine::Animator *z_animator_of(int64_t handle)
{
    engine::Entity *e = z_entity(handle);
    engine::Object *o = e ? e->getObject() : nullptr;
    return o ? o->getAnimator() : nullptr;
}
inline int64_t zen_anim_length(int64_t entity)
{
    engine::Animator *a = z_animator_of(entity);
    return a ? a->animLen() : -1;
}
inline double zen_anim_time(int64_t entity)
{
    engine::Animator *a = z_animator_of(entity);
    return a ? a->animTime() : -1.0;
}

/* ================= Picking ================= */

/* Clears the pick state the Picked* queries read and traces the line - the
   whole of runtime3d's doPick. */
static inline int64_t z_do_pick(const engine::Line &line, float radius)
{
    z_pick_state.with = nullptr;
    z_pick_state.coords.clear();
    z_pick_state.collision.time = 1.0f;
    z_pick_state.collision.normal.clear();
    z_pick_state.collision.surface = nullptr;
    z_pick_state.collision.index = (unsigned short)~0;
    engine::Object *hit = z_world.traceRay(line, radius, &z_pick_state);
    z_pick_state.with = hit;
    return hit ? z_handle_of(hit) : 0;
}

inline int64_t zen_camera_pick(int64_t camera, double x, double y)
{
    engine::Entity *e = z_entity(camera);
    engine::Camera *cam = e ? e->getCamera() : nullptr;
    if (!cam) return 0;
    int vpX = 0, vpY = 0, vpW = 0, vpH = 0;
    cam->getViewport(&vpX, &vpY, &vpW, &vpH);
    if (vpW <= 0 || vpH <= 0) return 0;
    const float nearRange = cam->getFrustumNear();
    const float farRange = cam->getFrustumFar();
    const float px = (((float)x / (float)vpW) - 0.5f) * cam->getFrustumWidth();
    const float py = (0.5f - ((float)y / (float)vpH)) * cam->getFrustumHeight();
    engine::Line line;
    if (cam->getProjMode() == engine::Camera::ProjOrtho)
        line = cam->getWorldTform() * engine::Line(engine::Vector(px, py, 0),
                                                  engine::Vector(0, 0, farRange));
    else
        line = cam->getWorldTform() * engine::Line(engine::Vector(),
                                                  engine::Vector(px * farRange / nearRange,
                                                                 py * farRange / nearRange, farRange));
    return z_do_pick(line, 0.0f);
}

inline int64_t zen_line_pick(double x, double y, double z, double dx, double dy, double dz,
                             double radius = 0.0)
{
    return z_do_pick(engine::Line(engine::Vector((float)x, (float)y, (float)z),
                                  engine::Vector((float)dx, (float)dy, (float)dz)),
                     (float)radius);
}

inline int64_t zen_entity_pick(int64_t entity, double range)
{
    engine::Entity *e = z_entity(entity);
    engine::Object *o = e ? e->getObject() : nullptr;
    if (!o) return 0;
    return z_do_pick(engine::Line(o->getWorldPosition(), o->getWorldTform().m.k * (float)range), 0.0f);
}

/* The Picked* queries: whatever the last pick landed on, or the cleared
   state (0 / -1 components, no entity) when it hit nothing. */
inline double zen_picked_x() { return z_pick_state.coords.x; }
inline double zen_picked_y() { return z_pick_state.coords.y; }
inline double zen_picked_z() { return z_pick_state.coords.z; }
inline double zen_picked_nx() { return z_pick_state.collision.normal.x; }
inline double zen_picked_ny() { return z_pick_state.collision.normal.y; }
inline double zen_picked_nz() { return z_pick_state.collision.normal.z; }
inline double zen_picked_time() { return z_pick_state.collision.time; }
inline int64_t zen_picked_entity() { return z_handle_of(z_pick_state.with); }
inline int64_t zen_picked_surface()
{
    return (int64_t)(std::intptr_t)z_pick_state.collision.surface;
}
inline int64_t zen_picked_triangle() { return (int64_t)z_pick_state.collision.index; }

/* ================= Textures ================= */

static int64_t z_next_texture = 0;

inline int64_t zen_load_texture(const std::string &file, int64_t flags = 1)
{
    z_world_ensure();
    engine::Texture *t = engine::Texture::load(file.c_str(), (int)flags);
    if (!t) return 0;
    int64_t h = ++z_next_texture;
    z_texture_list[h] = t;
    return h;
}

inline int64_t zen_create_texture(int64_t w, int64_t h, int64_t flags = 1, int64_t frames = 1)
{
    /* Blank and paintable: this is what a script draws its own art into,
       through the buffer TextureBuffer() hands back. */
    engine::Texture *t = engine::Texture::create((int)w, (int)h, (int)flags, (int)frames);
    if (!t) return 0;
    int64_t handle = ++z_next_texture;
    z_texture_list[handle] = t;
    return handle;
}

inline void zen_scale_texture(int64_t texture, double u, double v)
{
    /* runtime3d stores the reciprocal, so ScaleTexture 10,10 tiles ten
       times rather than shrinking to a tenth. */
    engine::Texture *t = z_texture_of(texture);
    if (t && u != 0.0 && v != 0.0) t->setScale(1.0f / (float)u, 1.0f / (float)v);
}

/* Brushes and the rest of the texture controls, each mirroring the runtime3d
   command of the same name. */
inline void zen_brush_alpha(int64_t brush, double alpha)
{
    engine::Brush *b = z_brush_of(brush);
    if (b) b->setAlpha((float)alpha);
}
inline void zen_brush_shininess(int64_t brush, double shininess)
{
    engine::Brush *b = z_brush_of(brush);
    if (b) b->setShininess((float)shininess);
}
inline void zen_brush_texture(int64_t brush, int64_t texture, int64_t frame = 0, int64_t index = 0)
{
    engine::Brush *b = z_brush_of(brush);
    engine::Texture *t = z_texture_of(texture);
    if (b && t) b->setTexture((int)index, engine::BrushTexture::fromTexture(z_platform.device(), t, (int)frame));
}
inline void zen_free_brush(int64_t brush)
{
    /* Brushes travel as their own pointer, so freeing one is the delete. */
    delete z_brush_of(brush);
}
inline int64_t zen_load_brush(const std::string &file, int64_t flags = 1, double u_scale = 1.0,
                              double v_scale = 1.0)
{
    /* Blitz3D's LoadBrush: load the texture, optionally rescale its UVs, and
       hand back a plain brush carrying it on slot 0. */
    engine::Texture *t = engine::Texture::load(file.c_str(), (int)flags);
    if (!t) return 0;
    if (u_scale != 1.0 || v_scale != 1.0) t->setScale(1.0f / (float)u_scale, 1.0f / (float)v_scale);
    engine::Brush *b = new engine::Brush();
    b->setTexture(0, engine::BrushTexture::fromTexture(z_platform.device(), t, 0));
    return (int64_t)(std::intptr_t)b;
}
inline void zen_texture_blend(int64_t texture, int64_t blend)
{
    engine::Texture *t = z_texture_of(texture);
    if (t) t->setBlend((int)blend);
}
inline void zen_texture_coords(int64_t texture, int64_t coords)
{
    engine::Texture *t = z_texture_of(texture);
    if (t) t->setFlags((int)coords);
}
inline void zen_position_texture(int64_t texture, double u, double v)
{
    engine::Texture *t = z_texture_of(texture);
    if (t) t->setPosition(-(float)u, -(float)v);
}
inline void zen_rotate_texture(int64_t texture, double angle)
{
    engine::Texture *t = z_texture_of(texture);
    if (t) t->setRotation(-(float)angle * kDegToRad);
}
inline void zen_clear_texture_filters() { engine::Texture::clearFilters(); }

/* Texture filters and animated textures, mirroring runtime3d's TextureFilter
   and LoadAnimTexture. */
inline void zen_texture_filter(const std::string &filter, int64_t flags)
{
    engine::Texture::addFilter(filter, (int)flags);
}
inline int64_t zen_load_anim_texture(const std::string &file, int64_t flags = 1, int64_t frameWidth = 0,
                                     int64_t frameHeight = 0, int64_t firstFrame = 0,
                                     int64_t frameCount = 1)
{
    z_world_ensure();
    engine::Texture *t = engine::Texture::loadAnim(file.c_str(), (int)flags, (int)frameWidth,
                                                   (int)frameHeight, (int)firstFrame, (int)frameCount);
    if (!t) return 0;
    int64_t handle = ++z_next_texture;
    z_texture_list[handle] = t;
    return handle;
}

/* Voxel materials: LoadMaterial fills the registry declared with the state,
   and a VoxelSprite is handed the whole atlas description, not a texture. */

inline int64_t zen_load_material(const std::string &file, int64_t flags = 1, int64_t frameWidth = 0,
                                 int64_t frameHeight = 0, int64_t firstFrame = 0, int64_t frameCount = 1)
{
    engine::Texture *texture = engine::Texture::load(file.c_str(), (int)flags);
    if (!texture) return 0;

    if (frameWidth < 1) frameWidth = texture->width();
    if (frameHeight < 1) frameHeight = texture->height();
    const int columns = frameWidth > 0 ? texture->width() / (int)frameWidth : 0;
    const int rows = frameHeight > 0 ? texture->height() / (int)frameHeight : 0;
    if (columns < 1 || rows < 1)
    {
        engine::Texture::release(texture);
        return 0;
    }
    const int total = columns * rows;
    int first = (int)firstFrame;
    if (first < 0) first = 0;
    if (first >= total) first = total - 1;
    int count = (int)frameCount;
    if (count < 1 || count > total - first) count = total - first;

    z_voxel_material *material = new z_voxel_material;
    material->texture = texture;
    material->columns = columns;
    material->rows = rows;
    material->firstFrame = first;
    material->frameCount = count;
    const int64_t handle = ++z_next_voxel_material;
    z_voxel_materials[handle] = material;
    return handle;
}
inline void zen_free_material(int64_t material)
{
    auto it = z_voxel_materials.find(material);
    if (it == z_voxel_materials.end()) return;
    engine::Texture::release(it->second->texture);
    delete it->second;
    z_voxel_materials.erase(it);
}
/* The voxel model behind an entity handle, the dynamic_cast runtime3d does
   before either of the two VoxelSprite commands touches anything. */
static inline engine::VoxelSprite *z_voxel_of(int64_t entity)
{
    engine::Entity *e = z_entity(entity);
    auto *model = e ? e->getModel() : nullptr;
    return model ? dynamic_cast<engine::VoxelSprite *>(model) : nullptr;
}
inline void zen_voxel_sprite_material(int64_t entity, int64_t material)
{
    engine::VoxelSprite *voxel = z_voxel_of(entity);
    auto it = z_voxel_materials.find(material);
    if (!voxel || it == z_voxel_materials.end()) return;
    z_voxel_material *m = it->second;
    voxel->setTexture(0, engine::BrushTexture::fromTexture(z_platform.device(), m->texture, 0));
    voxel->setAtlas(m->columns, m->rows, m->firstFrame, m->frameCount);
}
inline void zen_voxel_sprite_texture(int64_t entity, int64_t texture, int64_t frameWidth = 0,
                                     int64_t frameHeight = 0, int64_t firstFrame = 0,
                                     int64_t frameCount = 1)
{
    engine::VoxelSprite *voxel = z_voxel_of(entity);
    engine::Texture *t = z_texture_of(texture);
    if (!voxel || !t) return;

    if (frameWidth < 1) frameWidth = t->width();
    if (frameHeight < 1) frameHeight = t->height();
    int columns = frameWidth > 0 ? t->width() / (int)frameWidth : 1;
    int rows = frameHeight > 0 ? t->height() / (int)frameHeight : 1;
    if (columns < 1) columns = 1;
    if (rows < 1) rows = 1;
    voxel->setTexture(0, engine::BrushTexture::fromTexture(z_platform.device(), t, 0));
    voxel->setAtlas(columns, rows, (int)firstFrame, (int)frameCount);
}
inline void zen_light_mesh(int64_t mesh, double r, double g, double b, double range = 0, double x = 0,
                           double y = 0, double z = 0)
{
    engine::MeshModel *m = z_mesh_of(mesh);
    if (m) engine::MeshUtil::lightMesh(m, engine::Vector((float)x, (float)y, (float)z),
                                      engine::Vector((float)r / 255.0f, (float)g / 255.0f,
                                                     (float)b / 255.0f), (float)range);
}
inline void zen_light_cone_angles(int64_t light, double inner, double outer)
{
    engine::Entity *e = z_entity(light);
    engine::Light *l = e ? e->getLight() : nullptr;
    if (l) l->setConeAngles((float)inner, (float)outer);
}

inline void zen_free_texture(int64_t h)
{
    auto it = z_texture_list.find(h);
    if (it == z_texture_list.end()) return;
    engine::Texture::release(it->second);
    z_texture_list.erase(it);
}

inline void zen_entity_texture(int64_t entity, int64_t texture, int64_t frame = 0, int64_t index = 0)
{
    engine::Entity *e = z_entity(entity);
    auto it = z_texture_list.find(texture);
    if (!e || !e->getModel() || it == z_texture_list.end()) return;
    e->getModel()->setTexture((int)index,
        engine::BrushTexture::fromTexture(z_platform.device(), it->second, (int)frame));
}

/* ================= Surface and vertex queries ================= */

/* Every command here mirrors runtime3d's own of the same name. A surface
   travels as its own pointer (z_surface_of is a straight cast), so GetSurface
   hands back the pointer it found and the vertex queries answer 0 when the
   surface is gone - VertexW included, which Blitz3D defines as a constant 1. */
inline int64_t zen_image_width(int64_t image)
{
    engine::Image *img = z_image_of(image);
    return img ? img->width() : 0;
}
inline int64_t zen_image_height(int64_t image)
{
    engine::Image *img = z_image_of(image);
    return img ? img->height() : 0;
}
inline int64_t zen_count_surfaces(int64_t mesh)
{
    engine::MeshModel *m = z_mesh_of(mesh);
    return m ? (int64_t)m->getSurfaces().size() : 0;
}
inline int64_t zen_count_vertices(int64_t surface)
{
    engine::Surface *s = z_surface_of(surface);
    return s ? s->numVertices() : 0;
}
inline int64_t zen_count_triangles(int64_t surface)
{
    engine::Surface *s = z_surface_of(surface);
    return s ? s->numTriangles() : 0;
}
inline int64_t zen_get_surface(int64_t mesh, int64_t index)
{
    /* Surface indices are 1-based, as in Blitz3D. */
    engine::MeshModel *m = z_mesh_of(mesh);
    engine::Surface *s = nullptr;
    if (m && index >= 1 && (size_t)index <= m->getSurfaces().size())
        s = m->getSurfaces()[(size_t)index - 1];
    return (int64_t)(std::intptr_t)s;
}
inline int64_t zen_triangle_vertex(int64_t surface, int64_t index, int64_t vertex)
{
    engine::Surface *s = z_surface_of(surface);
    return s ? s->getTriangle((int)index).verts[(int)vertex] : 0;
}
inline double zen_vertex_x(int64_t surface, int64_t index)
{
    engine::Surface *s = z_surface_of(surface);
    return s ? s->getVertex((int)index).coords.x : 0.0;
}
inline double zen_vertex_y(int64_t surface, int64_t index)
{
    engine::Surface *s = z_surface_of(surface);
    return s ? s->getVertex((int)index).coords.y : 0.0;
}
inline double zen_vertex_z(int64_t surface, int64_t index)
{
    engine::Surface *s = z_surface_of(surface);
    return s ? s->getVertex((int)index).coords.z : 0.0;
}
inline double zen_vertex_w(int64_t = 0, int64_t = 0, int64_t = 0) { return 1.0; }
inline double zen_vertex_u(int64_t surface, int64_t index, int64_t coordSet = 0)
{
    engine::Surface *s = z_surface_of(surface);
    return s ? s->getVertex((int)index).texCoords[(int)coordSet][0] : 0.0;
}
inline double zen_vertex_v(int64_t surface, int64_t index, int64_t coordSet = 0)
{
    engine::Surface *s = z_surface_of(surface);
    return s ? s->getVertex((int)index).texCoords[(int)coordSet][1] : 0.0;
}
inline double zen_vertex_nx(int64_t surface, int64_t index)
{
    engine::Surface *s = z_surface_of(surface);
    return s ? s->getVertex((int)index).normal.x : 0.0;
}
inline double zen_vertex_ny(int64_t surface, int64_t index)
{
    engine::Surface *s = z_surface_of(surface);
    return s ? s->getVertex((int)index).normal.y : 0.0;
}
inline double zen_vertex_nz(int64_t surface, int64_t index)
{
    engine::Surface *s = z_surface_of(surface);
    return s ? s->getVertex((int)index).normal.z : 0.0;
}
inline double zen_vertex_red(int64_t surface, int64_t index)
{
    engine::Surface *s = z_surface_of(surface);
    const unsigned c = s ? s->getVertex((int)index).color : 0u;
    return (double)((c & 0xff0000u) >> 16);
}
inline double zen_vertex_green(int64_t surface, int64_t index)
{
    engine::Surface *s = z_surface_of(surface);
    const unsigned c = s ? s->getVertex((int)index).color : 0u;
    return (double)((c & 0xff00u) >> 8);
}
inline double zen_vertex_blue(int64_t surface, int64_t index)
{
    engine::Surface *s = z_surface_of(surface);
    const unsigned c = s ? s->getVertex((int)index).color : 0u;
    return (double)(c & 0xffu);
}
inline void zen_vertex_color(int64_t surface, int64_t index, double red, double green, double blue,
                             double alpha = 1.0)
{
    engine::Surface *s = z_surface_of(surface);
    if (!s) return;
    /* The colour arrives 0..255 and the alpha 0..1, both clamped, exactly as
       runtime3d's c_VertexColor packs them into one ARGB value. */
    double r = red, g = green, b = blue, a = alpha * 255.0;
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    if (a < 0) a = 0; else if (a > 255) a = 255;
    const unsigned argb =
        ((unsigned)a << 24) | ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
    s->setColor((int)index, argb);
}
inline void zen_vertex_normal(int64_t surface, int64_t index, double nx, double ny, double nz)
{
    if (engine::Surface *s = z_surface_of(surface))
        s->setNormal((int)index, engine::Vector((float)nx, (float)ny, (float)nz));
}
inline void zen_vertex_tex_coords(int64_t surface, int64_t index, double u, double v, double = 1.0,
                                  int64_t coordSet = 0)
{
    if (engine::Surface *s = z_surface_of(surface))
        s->setTexCoords((int)index, (float)u, (float)v, (int)coordSet);
}
inline void zen_clear_surface(int64_t surface, int64_t clearVertices = 1, int64_t clearTriangles = 1)
{
    if (engine::Surface *s = z_surface_of(surface))
        s->clear(clearVertices != 0, clearTriangles != 0);
}

/* ================= Animation and image collision queries ================= */

/* MD2 and animator state, each mirroring runtime3d's command of the same name;
   an animator is reached the way animator_of reaches it, through the entity's
   object. LoadAnimSeq loads a file as animation only, copies its sequences onto
   the entity's animator and throws the mesh away. */
inline double zen_md2_anim_time(int64_t md2)
{
    engine::MD2Model *m = z_md2_of(md2);
    return m ? m->getMD2AnimTime() : 0.0;
}
inline int64_t zen_md2_anim_length(int64_t md2)
{
    engine::MD2Model *m = z_md2_of(md2);
    return m ? m->getMD2AnimLength() : 0;
}
inline int64_t zen_md2_animating(int64_t md2)
{
    engine::MD2Model *m = z_md2_of(md2);
    return (m && m->getMD2Animating()) ? 1 : 0;
}
inline int64_t zen_anim_seq(int64_t entity)
{
    engine::Animator *a = z_animator_of(entity);
    return a ? a->animSeq() : -1;
}
inline int64_t zen_animating(int64_t entity)
{
    engine::Animator *a = z_animator_of(entity);
    return (a && a->animating()) ? 1 : 0;
}
inline void zen_set_anim_time(int64_t entity, double time, int64_t seq = 0)
{
    engine::Animator *a = z_animator_of(entity);
    if (a) a->setAnimTime((float)time, (int)seq);
}
inline int64_t zen_extract_anim_seq(int64_t entity, int64_t firstFrame, int64_t lastFrame,
                                    int64_t seq = 0)
{
    engine::Animator *a = z_animator_of(entity);
    if (!a) return -1;
    a->extractSeq((int)firstFrame, (int)lastFrame, (int)seq);
    return a->numSeqs() - 1;
}
inline int64_t zen_load_anim_seq(int64_t entity, const std::string &file)
{
    engine::Animator *a = z_animator_of(entity);
    if (!a) return -1;
    engine::LoaderB3D loader;
    engine::MeshModel *loaded = loader.load(file, engine::Transform(), engine::MeshLoader::HintAnimOnly,
                                            &z_platform.device());
    if (loaded)
    {
        if (engine::Animator *source = loaded->getAnimator()) a->addSeqs(source);
        delete loaded;
    }
    return a->numSeqs() - 1;
}
/* Image collision. The two "overlap" flavours test rectangles only, the two
   "collide" ones test the pixels of one frame - the same split runtime3d makes
   by passing the last argument of Image::collide. */
inline int64_t zen_images_overlap(int64_t image1, int64_t x1, int64_t y1, int64_t image2, int64_t x2,
                                  int64_t y2)
{
    engine::Image *a = z_image_of(image1);
    engine::Image *b = z_image_of(image2);
    const bool hit = a && b && a->collide((int)x1, (int)y1, 0, *b, (int)x2, (int)y2, 0, true);
    return hit ? 1 : 0;
}
inline int64_t zen_images_collide(int64_t image1, int64_t x1, int64_t y1, int64_t frame1,
                                  int64_t image2, int64_t x2, int64_t y2, int64_t frame2)
{
    engine::Image *a = z_image_of(image1);
    engine::Image *b = z_image_of(image2);
    const bool hit =
        a && b && a->collide((int)x1, (int)y1, (int)frame1, *b, (int)x2, (int)y2, (int)frame2, false);
    return hit ? 1 : 0;
}
inline int64_t zen_rects_overlap(int64_t x1, int64_t y1, int64_t w1, int64_t h1, int64_t x2,
                                 int64_t y2, int64_t w2, int64_t h2)
{
    const bool hit = !(x1 + w1 <= x2 || x1 >= x2 + w2 || y1 + h1 <= y2 || y1 >= y2 + h2);
    return hit ? 1 : 0;
}
inline int64_t zen_image_rect_overlap(int64_t image, int64_t x, int64_t y, int64_t rectX,
                                      int64_t rectY, int64_t rectW, int64_t rectH)
{
    engine::Image *img = z_image_of(image);
    const bool hit =
        img && img->rectCollide((int)x, (int)y, 0, (int)rectX, (int)rectY, (int)rectW, (int)rectH, true);
    return hit ? 1 : 0;
}
inline int64_t zen_image_rect_collide(int64_t image, int64_t x, int64_t y, int64_t frame,
                                      int64_t rectX, int64_t rectY, int64_t rectW, int64_t rectH)
{
    engine::Image *img = z_image_of(image);
    const bool hit = img && img->rectCollide((int)x, (int)y, (int)frame, (int)rectX, (int)rectY,
                                             (int)rectW, (int)rectH, false);
    return hit ? 1 : 0;
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

}  // namespace zen_sdk
using namespace zen_sdk;

#endif
