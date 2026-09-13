/*
** bb_cmds_image.cpp — LoadImage, LoadAnimImage, CreateImage, CopyImage,
** DrawImage/DrawBlock (+Rect variants), TileImage/TileBlock, MaskImage,
** FreeImage, ImageWidth/Height, HandleImage/MidHandle/AutoMidHandle,
** ScaleImage/ResizeImage/RotateImage/TFormImage, the collision tests.
*/
#include "runtime.h"
#include "vm.h"
#include "object.h"
#include "ct/hashmap.hpp"
#include "engine/Platform.h"
#include "engine/Image.h"
#include "engine/Texture.h"
#include <cmath>
#include <utility>

namespace bb3d
{
    extern engine::Platform *platform_for(zen::VM *vm);
    // bb_cmds_texture.cpp's texture store - image_buffer() below resolves
    // TextureBuffer's encoded handles through it, same as it resolves
    // ImageBuffer's through this file's own image_of().
    extern engine::Texture *texture_of(long long h);
}

namespace
{
    inline long long arg_int(zen::Value v)
    {
        return zen::is_int(v) ? v.as.integer : zen::is_float(v) ? (long long)v.as.number : 0;
    }
    inline const char *arg_cstr(zen::Value v)
    {
        return zen::is_string(v) ? zen::as_cstring(v) : "";
    }
    inline float arg_float(zen::Value v)
    {
        return zen::is_float(v) ? (float)v.as.number : zen::is_int(v) ? (float)v.as.integer : 0.0f;
    }

    /* degrees to radians, as bbgraphics.cpp's own dtor constant */
    constexpr float kDtoR = 0.0174532925199432957692369076848861f;
}

using namespace zen;

namespace bb3d
{
    /* Rebuilds a frame's alpha from its own RGB: every pixel whose colour
       is the mask colour becomes transparent, every other pixel opaque.
       Run over the full frame each time, so a second MaskImage with a
       different colour restores the pixels the first one hid - which is
       what Blitz3D's blit-time colour key did for free. */
    /* A loaded .bmp/.jpg has 3 components; masking needs somewhere to put
       the alpha, so widen the frame in place first. */
    static void ensure_rgba(engine::Pixmap &p)
    {
        if (!p.is_valid() || p.components == 4) return;
        engine::Pixmap *rgba = p.convert_to_rgba();
        if (!rgba) return;
        p = std::move(*rgba);
        delete rgba;
    }

    static void apply_mask_alpha(engine::Pixmap &p, unsigned char r, unsigned char g,
                                 unsigned char b)
    {
        ensure_rgba(p);
        if (!p.is_valid() || p.components != 4) return;
        for (int y = 0; y < p.height; ++y)
            for (int x = 0; x < p.width; ++x)
            {
                const engine::Color c = p.get_pixel_color((engine::u32)x, (engine::u32)y);
                const bool masked = c.r() == r && c.g() == g && c.b() == b;
                p.set_pixel((engine::u32)x, (engine::u32)y, c.r(), c.g(), c.b(),
                            masked ? 0 : 255);
            }
    }

    static ct::HashMap<long long, engine::Image *> g_images;
    static long long g_next_image = 0;

    engine::Image *image_of(long long h)
    {
        engine::Image **found = g_images.find(h);
        return found ? *found : nullptr;
    }

    /* AutoMidHandle: bbLoadImage/bbLoadAnimImage/bbCreateImage/bbCopyImage
       all centred the handle at creation when it was on. */
    static bool g_autoMidHandle = false;
    /* TFormFilter: bilinear sampling in tformCanvas. On in Blitz3D. */
    static bool g_tformFilter = true;

    static long long store_image(engine::Image *img)
    {
        if (g_autoMidHandle) img->setMidHandle();
        long long h = ++g_next_image;
        g_images.put(h, img);
        return h;
    }

    /* ImageBuffer/TextureBuffer both hand SetBuffer a negative handle
       encoding (resource handle, frame, and which store the resource
       handle indexes) - image_buffer() below is the one place that
       decodes it back, shared by every 2D drawing command regardless of
       whether it ends up painting an Image or a create()d Texture's
       canvas. Bit 0 of the encoded magnitude is the store selector so
       the two resources' independent, both-starting-at-1 handle counters
       never collide with each other once negated. */
    static long long encode_buffer_handle(long long resource, int frame, int kind)
    {
        return -((resource * 256 + frame) * 2 + kind + 1);
    }

    engine::ImageFrame *image_buffer(long long handle)
    {
        if (handle >= 0) return nullptr;
        const long long encoded = -handle - 1;
        const int kind = (int)(encoded & 1);
        const long long rest = encoded >> 1;
        const long long resourceHandle = rest / 256;
        const int frame = (int)(rest % 256);

        if (kind == 0)
        {
            engine::Image *image = image_of(resourceHandle);
            return image && frame >= 0 && frame < image->frameCount() ? &image->frame(frame) : nullptr;
        }
        engine::Texture *texture = texture_of(resourceHandle);
        return texture ? texture->canvas(frame) : nullptr;
    }

    /* for bb_cmds_buffer.cpp's GrabImage, which takes an image handle
       rather than one of ImageBuffer's encoded buffer handles */
    engine::Image *image_of_handle(long long h) { return image_of(h); }

    static long long image_buffer_handle(long long image, int frame)
    {
        return encode_buffer_handle(image, frame, 0);
    }

    // extern: bb_cmds_texture.cpp's TextureBuffer() encodes through this
    // rather than duplicating encode_buffer_handle's bit layout.
    long long texture_buffer_handle(long long texture, int frame)
    {
        return encode_buffer_handle(texture, frame, 1);
    }

    // extern: called by shutdown_graphics() so images a script never
    // passed to FreeImage are still released.
    void free_all_images()
    {
        for (auto &e : g_images) delete e.value;
        g_images.clear();
        g_next_image = 0;
    }

    static int c_LoadImage(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        engine::Pixmap p;
        if (!p.load(arg_cstr(args[0]))) { args[0] = val_int(0); return 1; }
        engine::Image *img = new engine::Image();
        img->addFrame(std::move(p));
        args[0] = val_int(store_image(img));
        (void)vm;
        return 1;
    }

    static int c_LoadAnimImage(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        const char *path = arg_cstr(args[0]);
        int cellW = (int)arg_int(args[1]), cellH = (int)arg_int(args[2]);
        int first = (int)arg_int(args[3]), count = (int)arg_int(args[4]);

        if (count < 1 || first < 0 || cellW <= 0 || cellH <= 0) { args[0] = val_int(0); return 1; }

        engine::Pixmap sheet;
        if (!sheet.load(path)) { args[0] = val_int(0); return 1; }

        int fpr = sheet.width / cellW;
        int fpp = fpr > 0 ? (sheet.height / cellH) * fpr : 0;
        if (fpr <= 0 || first + count > fpp) { args[0] = val_int(0); return 1; }

        engine::Image *img = new engine::Image();
        for (int k = 0; k < count; ++k)
        {
            int idx = first + k;
            int srcX = (idx % fpr) * cellW, srcY = (idx / fpr) * cellH;
            engine::IntRect rect{srcX, srcY, cellW, cellH};
            engine::Pixmap frame(sheet, rect);
            img->addFrame(std::move(frame));
        }
        args[0] = val_int(store_image(img));
        (void)vm;
        return 1;
    }

    static int c_FreeImage(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        long long h = arg_int(args[0]);
        engine::Image *img = image_of(h);
        if (img) { delete img; g_images.erase(h); }
        (void)vm;
        return 0;
    }

    static int c_CreateImage(VM *, Value *args, int)
    {
        const int width = (int)arg_int(args[0]), height = (int)arg_int(args[1]), frames = (int)arg_int(args[2]);
        if (width <= 0 || height <= 0 || frames <= 0) { args[0] = val_int(0); return 1; }
        engine::Image *image = new engine::Image();
        for (int i = 0; i < frames; ++i)
        {
            engine::Pixmap pixels(width, height, 4);
            pixels.clear();
            image->addFrame(std::move(pixels));
        }
        args[0] = val_int(store_image(image));
        return 1;
    }

    static int c_ImageBuffer(VM *, Value *args, int)
    {
        const long long image = arg_int(args[0]);
        const int frame = (int)arg_int(args[1]);
        engine::Image *img = image_of(image);
        args[0] = val_int(img && frame >= 0 && frame < img->frameCount() ? image_buffer_handle(image, frame) : 0);
        return 1;
    }

    static int c_DrawImage(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        engine::Image *img = image_of(arg_int(args[0]));
        if (!img) return 0;
        float x = (float)arg_int(args[1]), y = (float)arg_int(args[2]);
        int frame = (int)arg_int(args[3]);
        engine::Platform *p = platform_for(vm);
        if (!img->ensureUploaded(p->device(), frame)) return 0;
        const engine::ImageFrame &f = img->frame(frame);
        p->batch().drawTexture(f.texture, x - (float)img->handleX(), y - (float)img->handleY(),
                               (float)f.pixels.width, (float)f.pixels.height);
        return 0;
    }

    static int c_DrawBlock(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        engine::Image *img = image_of(arg_int(args[0]));
        if (!img) return 0;
        int frame = (int)arg_int(args[3]);
        engine::Platform *p = platform_for(vm);
        if (!img->ensureUploaded(p->device(), frame)) return 0;
        const engine::ImageFrame &f = img->frame(frame);
        p->batch().setBlend(false);
        p->batch().drawTexture(f.texture, (float)arg_int(args[1]) - img->handleX(), (float)arg_int(args[2]) - img->handleY(),
                               (float)f.pixels.width, (float)f.pixels.height);
        p->batch().setBlend(true);
        return 0;
    }

    static int c_DrawImageRect(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        engine::Image *img = image_of(arg_int(args[0]));
        if (!img) return 0;
        const int frame = (int)arg_int(args[7]);
        engine::Platform *p = platform_for(vm);
        if (!img->ensureUploaded(p->device(), frame)) return 0;
        const engine::ImageFrame &f = img->frame(frame);
        p->batch().drawTexture(f.texture, (float)arg_int(args[1]) - img->handleX(), (float)arg_int(args[2]) - img->handleY(),
                               (float)arg_int(args[5]), (float)arg_int(args[6]),
                               (float)arg_int(args[3]) / f.pixels.width, (float)arg_int(args[4]) / f.pixels.height,
                               (float)arg_int(args[5]) / f.pixels.width, (float)arg_int(args[6]) / f.pixels.height);
        return 0;
    }

    static int c_MaskImage(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        engine::Image *img = image_of(arg_int(args[0]));
        if (!img) return 0;
        const unsigned char r = (unsigned char)arg_int(args[1]);
        const unsigned char g = (unsigned char)arg_int(args[2]);
        const unsigned char b = (unsigned char)arg_int(args[3]);
        /* Blitz3D masked at blit time through a colour key, so MaskImage
           could be called again with a different colour and the original
           pixels were never lost. set_color_key() punches the alpha out
           destructively, so the mask colour is recorded on the Image and
           the alpha rebuilt from the untouched RGB each time. */
        img->setMask(((std::uint32_t)r << 16) | ((std::uint32_t)g << 8) | b);
        for (int i = 0; i < img->frameCount(); ++i)
        {
            engine::ImageFrame &f = img->frame(i);
            apply_mask_alpha(f.pixels, r, g, b);
            f.dirty = true;
        }
        (void)vm;
        return 0;
    }

    static int c_ImageWidth(VM *vm, Value *args, int nargs)
    {
        (void)nargs; (void)vm;
        engine::Image *img = image_of(arg_int(args[0]));
        args[0] = val_int(img ? img->width() : 0);
        return 1;
    }

    static int c_ImageHeight(VM *vm, Value *args, int nargs)
    {
        (void)nargs; (void)vm;
        engine::Image *img = image_of(arg_int(args[0]));
        args[0] = val_int(img ? img->height() : 0);
        return 1;
    }

    static int c_HandleImage(VM *vm, Value *args, int nargs)
    {
        (void)nargs; (void)vm;
        engine::Image *img = image_of(arg_int(args[0]));
        if (img) img->setHandle((int)arg_int(args[1]), (int)arg_int(args[2]));
        return 0;
    }

    static int c_MidHandle(VM *vm, Value *args, int nargs)
    {
        (void)nargs; (void)vm;
        engine::Image *img = image_of(arg_int(args[0]));
        if (img) img->setMidHandle();
        return 0;
    }


    /* DrawImage/DrawBlock share one path: "solid" is Blitz3D's word for
       ignoring the mask, so it just turns alpha blending off for the
       quad. The mask itself already lives in the frame's alpha. */
    static void draw_frame(VM *vm, engine::Image *img, float x, float y, int frame,
                           float sx, float sy, float sw, float sh, bool solid)
    {
        engine::Platform *p = platform_for(vm);
        if (!img->ensureUploaded(p->device(), frame)) return;
        const engine::ImageFrame &f = img->frame(frame);
        const float fw = (float)f.pixels.width, fh = (float)f.pixels.height;
        if (fw <= 0.0f || fh <= 0.0f) return;

        if (sw <= 0.0f || sh <= 0.0f) { sx = 0.0f; sy = 0.0f; sw = fw; sh = fh; }

        engine::BatchRenderer &batch = p->batch();
        if (solid) batch.setBlend(false);
        batch.setColor((unsigned char)255, (unsigned char)255, (unsigned char)255);
        /* drawTexture takes normalised source coordinates */
        batch.drawTexture(f.texture, x - (float)img->handleX(), y - (float)img->handleY(),
                          sw, sh, sx / fw, sy / fh, sw / fw, sh / fh);
        if (solid) batch.setBlend(true);
    }

    static int c_DrawBlockRect(VM *vm, Value *args, int)
    {
        engine::Image *img = image_of(arg_int(args[0]));
        if (!img) return 0;
        draw_frame(vm, img, (float)arg_int(args[1]), (float)arg_int(args[2]),
                   (int)arg_int(args[7]), (float)arg_int(args[3]), (float)arg_int(args[4]),
                   (float)arg_int(args[5]), (float)arg_int(args[6]), true);
        return 0;
    }

    /* bbgraphics.cpp's static tile(): walk the viewport from one frame
       before the origin so a partially-scrolled tile still covers the
       top-left edge. The viewport here is the whole screen - this runtime
       has no Viewport-clipped canvas of its own yet. */
    static int tile_cmd(VM *vm, Value *args, bool solid)
    {
        engine::Image *img = image_of(arg_int(args[0]));
        if (!img) return 0;
        const int w = img->width(), h = img->height();
        if (w <= 0 || h <= 0) return 0;

        engine::Platform *p = platform_for(vm);
        const int vpW = p->width(), vpH = p->height();
        int x = (int)arg_int(args[1]), y = (int)arg_int(args[2]);
        const int frame = (int)arg_int(args[3]);

        int dx = img->handleX(), dy = img->handleY();
        x -= dx; y -= dy;
        dx += (x >= 0 ? x % w : w - (-x % w));
        dy += (y >= 0 ? y % h : h - (-y % h));

        for (int ty = -h; ty < vpH; ty += h)
            for (int tx = -w; tx < vpW; tx += w)
                draw_frame(vm, img, (float)(tx + dx + img->handleX()),
                           (float)(ty + dy + img->handleY()), frame,
                           0.0f, 0.0f, 0.0f, 0.0f, solid);
        return 0;
    }

    static int c_TileImage(VM *vm, Value *args, int) { return tile_cmd(vm, args, false); }
    static int c_TileBlock(VM *vm, Value *args, int) { return tile_cmd(vm, args, true); }

    static int c_CopyImage(VM *vm, Value *args, int)
    {
        (void)vm;
        engine::Image *src = image_of(arg_int(args[0]));
        if (!src || src->frameCount() == 0) { args[0] = val_int(0); return 1; }

        engine::Image *img = new engine::Image();
        for (int k = 0; k < src->frameCount(); ++k)
        {
            const engine::Pixmap &sp = src->frame(k).pixels;
            engine::IntRect all{0, 0, sp.width, sp.height};
            engine::Pixmap copy(sp, all);
            img->addFrame(std::move(copy));
        }
        /* bbCopyImage carried the handle and the mask across, and
           store_image's AutoMidHandle must not override an explicit one */
        const bool saved = g_autoMidHandle;
        g_autoMidHandle = false;
        const long long h = store_image(img);
        g_autoMidHandle = saved;
        img->setHandle(src->handleX(), src->handleY());
        img->setMask(src->mask());
        args[0] = val_int(h);
        return 1;
    }

    static int c_ImageXHandle(VM *vm, Value *args, int)
    {
        (void)vm;
        engine::Image *img = image_of(arg_int(args[0]));
        args[0] = val_int(img ? img->handleX() : 0);
        return 1;
    }

    static int c_ImageYHandle(VM *vm, Value *args, int)
    {
        (void)vm;
        engine::Image *img = image_of(arg_int(args[0]));
        args[0] = val_int(img ? img->handleY() : 0);
        return 1;
    }

    static int c_AutoMidHandle(VM *vm, Value *args, int)
    {
        (void)vm;
        g_autoMidHandle = arg_int(args[0]) != 0;
        return 0;
    }

    static int c_TFormFilter(VM *vm, Value *args, int)
    {
        (void)vm;
        g_tformFilter = arg_int(args[0]) != 0;
        return 0;
    }

    /* bbgraphics.cpp's getPixel(): the bilinear sample TFormFilter turns
       on, on the half-pixel grid the original used. */
    static engine::Color sample_filtered(const engine::Pixmap &p, float x, float y)
    {
        x -= 0.5f; y -= 0.5f;
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
        return engine::Color(mix(tl.r(), tr.r(), bl.r(), br.r()),
                             mix(tl.g(), tr.g(), bl.g(), br.g()),
                             mix(tl.b(), tr.b(), bl.b(), br.b()),
                             mix(tl.a(), tr.a(), bl.a(), br.a()));
    }

    /* bbgraphics.cpp's tformCanvas(): map the frame through the 2x2
       matrix, sizing the result to the transformed corners and moving the
       handle to keep the same pixel under it. */
    static bool tform_frame(engine::Image *img, int k, const float m[2][2])
    {
        engine::ImageFrame &f = img->frame(k);
        engine::Pixmap &src = f.pixels;
        if (!src.is_valid()) return false;

        const float det = m[0][0] * m[1][1] - m[1][0] * m[0][1];
        if (std::fabs(det) < 1e-8f) return false;
        const float dt = 1.0f / det;
        float inv[2][2];
        inv[0][0] = dt * m[1][1];  inv[1][0] = -dt * m[1][0];
        inv[0][1] = -dt * m[0][1]; inv[1][1] = dt * m[0][0];

        const float ox = (float)img->handleX(), oy = (float)img->handleY();
        const float cx[4] = {-ox, (float)src.width - ox, (float)src.width - ox, -ox};
        const float cy[4] = {-oy, -oy, (float)src.height - oy, (float)src.height - oy};
        float minX = 0, minY = 0, maxX = 0, maxY = 0;
        for (int i = 0; i < 4; ++i)
        {
            const float tx = m[0][0] * cx[i] + m[0][1] * cy[i];
            const float ty = m[1][0] * cx[i] + m[1][1] * cy[i];
            if (i == 0) { minX = maxX = tx; minY = maxY = ty; }
            else
            {
                if (tx < minX) minX = tx; if (tx > maxX) maxX = tx;
                if (ty < minY) minY = ty; if (ty > maxY) maxY = ty;
            }
        }
        minX = std::floor(minX); minY = std::floor(minY);
        maxX = std::ceil(maxX);  maxY = std::ceil(maxY);
        const int iw = (int)(maxX - minX), ih = (int)(maxY - minY);
        if (iw <= 0 || ih <= 0) return false;

        ensure_rgba(src);
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
                if (g_tformFilter) c = sample_filtered(src, qx + ox, qy + oy);
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

    static int c_TFormImage(VM *vm, Value *args, int)
    {
        (void)vm;
        engine::Image *img = image_of(arg_int(args[0]));
        if (!img) return 0;
        float m[2][2];
        m[0][0] = arg_float(args[1]); m[1][0] = arg_float(args[2]);
        m[0][1] = arg_float(args[3]); m[1][1] = arg_float(args[4]);
        /* the handle moves with the first frame; every frame is the same
           size, so they all land on the same new handle */
        const int hx = img->handleX(), hy = img->handleY();
        for (int k = 0; k < img->frameCount(); ++k)
        {
            img->setHandle(hx, hy);
            tform_frame(img, k, m);
        }
        return 0;
    }

    static int c_ScaleImage(VM *vm, Value *args, int)
    {
        Value t[5] = {args[0], val_float(arg_float(args[1])), val_float(0.0),
                      val_float(0.0), val_float(arg_float(args[2]))};
        const int rv = c_TFormImage(vm, t, 5);
        return rv;
    }

    static int c_ResizeImage(VM *vm, Value *args, int)
    {
        engine::Image *img = image_of(arg_int(args[0]));
        if (!img || img->width() <= 0 || img->height() <= 0) return 0;
        const float sx = arg_float(args[1]) / (float)img->width();
        const float sy = arg_float(args[2]) / (float)img->height();
        Value t[5] = {args[0], val_float(sx), val_float(0.0), val_float(0.0), val_float(sy)};
        return c_TFormImage(vm, t, 5);
    }

    static int c_RotateImage(VM *vm, Value *args, int)
    {
        const float d = -arg_float(args[1]) * kDtoR;
        Value t[5] = {args[0], val_float(std::cos(d)), val_float(-std::sin(d)),
                      val_float(std::sin(d)), val_float(std::cos(d))};
        return c_TFormImage(vm, t, 5);
    }

    static int c_ImagesOverlap(VM *vm, Value *args, int)
    {
        (void)vm;
        engine::Image *a = image_of(arg_int(args[0]));
        engine::Image *b = image_of(arg_int(args[3]));
        const bool hit = a && b && a->collide((int)arg_int(args[1]), (int)arg_int(args[2]), 0,
                                              *b, (int)arg_int(args[4]), (int)arg_int(args[5]),
                                              0, true);
        args[0] = val_int(hit ? 1 : 0);
        return 1;
    }

    static int c_ImagesCollide(VM *vm, Value *args, int)
    {
        (void)vm;
        engine::Image *a = image_of(arg_int(args[0]));
        engine::Image *b = image_of(arg_int(args[4]));
        const bool hit = a && b && a->collide((int)arg_int(args[1]), (int)arg_int(args[2]),
                                              (int)arg_int(args[3]), *b, (int)arg_int(args[5]),
                                              (int)arg_int(args[6]), (int)arg_int(args[7]), false);
        args[0] = val_int(hit ? 1 : 0);
        return 1;
    }

    static int c_RectsOverlap(VM *vm, Value *args, int)
    {
        (void)vm;
        const long long x1 = arg_int(args[0]), y1 = arg_int(args[1]);
        const long long w1 = arg_int(args[2]), h1 = arg_int(args[3]);
        const long long x2 = arg_int(args[4]), y2 = arg_int(args[5]);
        const long long w2 = arg_int(args[6]), h2 = arg_int(args[7]);
        const bool hit = !(x1 + w1 <= x2 || x1 >= x2 + w2 || y1 + h1 <= y2 || y1 >= y2 + h2);
        args[0] = val_int(hit ? 1 : 0);
        return 1;
    }

    static int c_ImageRectOverlap(VM *vm, Value *args, int)
    {
        (void)vm;
        engine::Image *img = image_of(arg_int(args[0]));
        const bool hit = img && img->rectCollide((int)arg_int(args[1]), (int)arg_int(args[2]), 0,
                                                 (int)arg_int(args[3]), (int)arg_int(args[4]),
                                                 (int)arg_int(args[5]), (int)arg_int(args[6]), true);
        args[0] = val_int(hit ? 1 : 0);
        return 1;
    }

    static int c_ImageRectCollide(VM *vm, Value *args, int)
    {
        (void)vm;
        engine::Image *img = image_of(arg_int(args[0]));
        const bool hit = img && img->rectCollide((int)arg_int(args[1]), (int)arg_int(args[2]),
                                                 (int)arg_int(args[3]), (int)arg_int(args[4]),
                                                 (int)arg_int(args[5]), (int)arg_int(args[6]),
                                                 (int)arg_int(args[7]), false);
        args[0] = val_int(hit ? 1 : 0);
        return 1;
    }

    static int c_SaveImage(VM *vm, Value *args, int)
    {
        (void)vm;
        engine::Image *img = image_of(arg_int(args[0]));
        const int frame = (int)arg_int(args[2]);
        bool ok = false;
        if (img && img->frameCount() > 0)
            ok = img->frame(frame).pixels.save(arg_cstr(args[1]));
        args[0] = val_int(ok ? 1 : 0);
        return 1;
    }

    extern const zen::CommandDecl bb3d_cmds_image[] = {
        {"%LoadImage$bmpfile", c_LoadImage},
        {"%LoadAnimImage$bmpfile%cellwidth%cellheight%first%count", c_LoadAnimImage},
        {"%CreateImage%width%height%frames=1", c_CreateImage},
        {"FreeImage%image", c_FreeImage},
        {"DrawImage%image%x%y%frame=0", c_DrawImage},
        {"DrawBlock%image%x%y%frame=0", c_DrawBlock},
        {"DrawImageRect%image%x%y%rect_x%rect_y%rect_width%rect_height%frame=0", c_DrawImageRect},
        {"DrawBlockRect%image%x%y%rect_x%rect_y%rect_width%rect_height%frame=0", c_DrawBlockRect},
        {"TileImage%image%x=0%y=0%frame=0", c_TileImage},
        {"TileBlock%image%x=0%y=0%frame=0", c_TileBlock},
        {"%CopyImage%image", c_CopyImage},
        {"%SaveImage%image$bmpfile%frame=0", c_SaveImage},
        {"MaskImage%image%red%green%blue", c_MaskImage},
        {"%ImageWidth%image", c_ImageWidth},
        {"%ImageHeight%image", c_ImageHeight},
        {"HandleImage%image%x%y", c_HandleImage},
        {"MidHandle%image", c_MidHandle},
        {"%ImageBuffer%image%frame=0", c_ImageBuffer},
        {"%ImageXHandle%image", c_ImageXHandle},
        {"%ImageYHandle%image", c_ImageYHandle},
        {"AutoMidHandle%enable", c_AutoMidHandle},

        {"ScaleImage%image#xscale#yscale", c_ScaleImage},
        {"ResizeImage%image#width#height", c_ResizeImage},
        {"RotateImage%image#angle", c_RotateImage},
        {"TFormImage%image#a#b#c#d", c_TFormImage},
        {"TFormFilter%enable", c_TFormFilter},

        {"%ImagesOverlap%image1%x1%y1%image2%x2%y2", c_ImagesOverlap},
        {"%ImagesCollide%image1%x1%y1%frame1%image2%x2%y2%frame2", c_ImagesCollide},
        {"%RectsOverlap%x1%y1%width1%height1%x2%y2%width2%height2", c_RectsOverlap},
        {"%ImageRectOverlap%image%x%y%rect_x%rect_y%rect_width%rect_height", c_ImageRectOverlap},
        {"%ImageRectCollide%image%x%y%frame%rect_x%rect_y%rect_width%rect_height", c_ImageRectCollide},
    };
    extern const int bb3d_cmds_image_count = (int)(sizeof(bb3d_cmds_image) / sizeof(bb3d_cmds_image[0]));
}
