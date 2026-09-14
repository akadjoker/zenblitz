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
static int64_t z_draw_buffer = 1;
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

static constexpr float kDegToRad = 0.0174532925199432957692369076848861f;

inline void zen_graphics_open(int width, int height)
{
    if (z_graphics_open) return;
    const char *hidden = getenv("ZENBLITZ_HIDDEN");
    bool visible = !(hidden && hidden[0] && hidden[0] != '0');
    if (z_platform.open(width, height, "zenblitz", false, visible))
    {
        z_graphics_open = true;
        z_platform.beginFrame();
    }
}

inline void zen_graphics3d_open(int width, int height)
{
    if (!z_graphics_open) zen_graphics_open(width, height);
    if (z_graphics_open) z_world_ensure();
}

inline void zen_flip()
{
    if (!z_graphics_open) return;
    z_platform.pumpEvents();
    if (!z_platform.isOpen()) { std::fflush(stdout); std::exit(0); }
    z_platform.endFrame();
    z_platform.beginFrame();
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
    /* Cls clears the back buffer; beginFrame already clears with the
       current ClsColor, so this just ensures a fresh frame start. */
    if (!z_graphics_open) return;
    z_platform.flushCanvasPass();
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

inline void zen_plot(int64_t x, int64_t y)
{
    if (!z_graphics_open) return;
    z_platform.batch().drawRect((float)x, (float)y, 1.0f, 1.0f, true);
}

inline void zen_line(int64_t x1, int64_t y1, int64_t x2, int64_t y2)
{
    if (!z_graphics_open) return;
    z_platform.batch().drawLine((float)x1, (float)y1, (float)x2, (float)y2);
}

inline void zen_rect(int64_t x, int64_t y, int64_t w, int64_t h, int64_t solid = 1)
{
    if (!z_graphics_open) return;
    z_platform.batch().drawRect((float)x, (float)y, (float)w, (float)h, solid != 0);
}

inline void zen_oval(int64_t x, int64_t y, int64_t w, int64_t h, int64_t solid = 1)
{
    if (!z_graphics_open) return;
    z_platform.batch().drawEllipse(x + w * 0.5f, y + h * 0.5f, w * 0.5f, h * 0.5f, solid != 0);
}

inline void zen_text(int64_t x, int64_t y, const std::string &text, int64_t centre_x = 0, int64_t centre_y = 0)
{
    if (!z_graphics_open) return;
    float fx = (float)x, fy = (float)y;
    if (centre_x) fx -= z_platform.batch().textWidth(16.0f, text.c_str()) * 0.5f;
    if (centre_y) fy -= 8.0f;
    z_platform.batch().drawText(fx, fy, 16.0f, text.c_str());
}

inline int64_t zen_key_down(int64_t key) { return z_platform.keyDown((int)key) ? 1 : 0; }
inline int64_t zen_key_hit(int64_t key) { return z_platform.keyHit((int)key) ? 1 : 0; }
inline int64_t zen_get_key() { return z_platform.popKey(); }
inline void zen_flush_keys() { z_platform.flushKeyHits(); }
inline void zen_hide_pointer() { z_platform.showPointer(false); }
inline void zen_show_pointer() { z_platform.showPointer(true); }

/* 2D canvas buffer (no-op: always back buffer) */
inline int64_t zen_back_buffer() { return 1; }
inline int64_t zen_front_buffer() { return 1; }
inline void zen_set_buffer(int64_t) {}
inline void zen_origin(int64_t x, int64_t y) { (void)x; (void)y; }

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
inline void zen_delay(int64_t ms) { if (z_graphics_open) z_platform.wait((float)ms); else SDL_Delay((Uint32)ms); }
inline void zen_vwait() { if (z_graphics_open) z_platform.wait(16.0f); }


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

/* AppTitle */
inline void zen_app_title(const std::string &) {}

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

/* Fonts (stubs — engine has one embedded font) */
inline int64_t zen_load_font(const std::string &, double size = 16) { (void)size; return 1; }
inline void zen_set_font(int64_t) {}
inline void zen_free_font(int64_t) {}
inline int64_t zen_font_width() { return 8; }
inline int64_t zen_font_height() { return 16; }
inline int64_t zen_string_width(const std::string &s) { return (int64_t)(s.size() * 8); }
inline int64_t zen_string_height(const std::string &) { return 16; }

/* Rand seed readback */
inline int64_t zen_rand_seed() { return (int64_t)z_rand_seed_value; }

/* Clear collisions */
inline void zen_clear_collisions() {}

/* Images 2D (stubs) */
inline int64_t zen_load_image(const std::string &) { return 0; }
inline void zen_draw_image(int64_t, int64_t, int64_t, int64_t = 0) {}
inline void zen_draw_image_rect(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t = 0) {}
inline void zen_grab_image(int64_t, int64_t, int64_t, int64_t = 0) {}
inline int64_t zen_load_anim_image(const std::string &) { return 0; }

/* Gfx drivers */
static inline void z_collect_gfx_modes()
{
    if (!z_gfx_modes.empty()) return;
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
inline int64_t zen_texture_buffer(int64_t, int64_t = 0) { return 1; }

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
inline int64_t zen_copy_entity(int64_t, int64_t = 0) { return 0; }

/* Mesh extras */
inline void zen_scale_mesh(int64_t mesh, double xs, double ys, double zs)
{
    engine::Entity *e = z_entity(mesh);
    if (e && e->getModel()) e->getModel()->setMeshScale(engine::Vector((float)xs, (float)ys, (float)zs));
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

/* Vertex */
inline int64_t zen_add_vertex(int64_t, double, double, double, double = 0, double = 0, double = 1) { return 0; }
inline int64_t zen_add_triangle(int64_t, int64_t, int64_t, int64_t) { return 0; }
inline void zen_update_normals(int64_t) {}

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
inline void zen_mid_handle(int64_t) {}
inline void zen_auto_mid_handle(int64_t) {}
inline int64_t zen_image_buffer(int64_t, int64_t = 0) { return 1; }
inline int64_t zen_copy_image(int64_t) { return 0; }
inline void zen_resize_image(int64_t, int64_t, int64_t) {}
inline void zen_free_image(int64_t) {}
inline void zen_save_image(int64_t, const std::string &, int64_t = 0) {}
inline void zen_handle_image(int64_t, int64_t, int64_t) {}
inline void zen_draw_block(int64_t, int64_t, int64_t, int64_t = 0) {}
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
inline int64_t zen_load_sprite(const std::string &) { return 0; }
inline int64_t zen_create_image(int64_t, int64_t = 0) { return 0; }

/* Mesh extras (stubs) */
inline void zen_fit_mesh(int64_t) {}
inline int64_t zen_create_surface(int64_t) { return 0; }

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
inline void zen_camera_proj_mode(int64_t cam, int64_t mode)
{
    engine::Entity *e = z_entity(cam);
    if (e && e->getCamera()) e->getCamera()->setProjMode((int)mode);
}
inline void zen_camera_viewport(int64_t cam, int64_t x, int64_t y, int64_t w, int64_t h)
{
    engine::Entity *e = z_entity(cam);
    if (e && e->getCamera()) e->getCamera()->setViewport((int)x, (int)y, (int)w, (int)h);
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

static std::map<int64_t, engine::Texture *> z_textures;
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

inline int64_t zen_create_texture(int64_t w, int64_t h, int64_t = 1, int64_t = 1)
{
    (void)w; (void)h;
    return 0;
}

inline void zen_scale_texture(int64_t, double, double) {}

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
