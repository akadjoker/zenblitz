/*
** bb_cmds_buffer.cpp — the buffer side of Blitz3D's 2D canvas:
** GraphicsBuffer, Origin, Viewport, the pixel reads and writes
** (ReadPixel/WritePixel and their Fast/Copy variants), LockBuffer,
** CopyRect, GrabImage, LoadBuffer/SaveBuffer/BufferDirty and the
** current-colour queries GetColor/ColorRed/ColorGreen/ColorBlue.
**
** A "buffer" is either the screen (kScreenBuffer) or one frame of an
** Image, which is where ImageBuffer's negative handles point. The
** screen's pixels can only be read back, never written: the presented
** surface is a GPU texture and Blitz3D's per-pixel writes to the front
** buffer have no equivalent that would not stall the pipeline every
** call, so writes to the screen are reported rather than silently lost.
*/
#include "runtime.h"
#include "vm.h"
#include "object.h"
#include "ct/hashmap.hpp"
#include "engine/Platform.h"
#include "engine/Image.h"
#include "ct/vector.hpp"
#include <cstdio>

namespace bb3d
{
    extern engine::Platform *platform_for(zen::VM *vm);
    extern engine::ImageFrame *image_buffer(long long handle);
    extern engine::Image *image_of_handle(long long handle);
    extern long long current_buffer(zen::VM *vm);
    extern unsigned canvas_color(zen::VM *vm);
    extern void set_canvas_color(zen::VM *vm, unsigned argb);
}

namespace
{
    inline long long arg_int(zen::Value v)
    {
        return zen::is_int(v) ? v.as.integer : zen::is_float(v) ? (long long)v.as.number : 0;
    }
    inline const char *arg_cstr(zen::Value v) { return zen::is_string(v) ? zen::as_cstring(v) : ""; }
}

using namespace zen;

namespace bb3d
{
    static const long long kScreenBuffer = 1;

    /* Origin and Viewport are per-VM drawing state, exactly as they were
       fields of the gxCanvas Blitz3D was currently drawing to. Viewport
       defaults to the whole buffer; a width of 0 means "not set yet", so
       it follows the window until a script sets one. */
    struct CanvasRegion
    {
        int originX = 0, originY = 0;
        int viewX = 0, viewY = 0, viewW = 0, viewH = 0;
    };
    static ct::HashMap<VM *, CanvasRegion> g_regions;

    static CanvasRegion &region_for(VM *vm)
    {
        CanvasRegion *found = g_regions.find(vm);
        if (found) return *found;
        g_regions.put(vm, CanvasRegion());
        return *g_regions.find(vm);
    }

    static void canvas_origin(VM *vm, int &x, int &y)
    {
        const CanvasRegion &r = region_for(vm);
        x = r.originX;
        y = r.originY;
    }

    static void buffer_size(VM *vm, long long buffer, int &w, int &h);

    /* Only the screen goes through the Batch, so an ImageBuffer's
       viewport must not leave a clip rect set on it. */
    static void apply_clip(VM *vm)
    {
        const CanvasRegion &r = region_for(vm);
        engine::BatchRenderer &batch = platform_for(vm)->batch();
        if (current_buffer(vm) != kScreenBuffer || r.viewW <= 0 || r.viewH <= 0)
        {
            batch.clearClipRect();
            return;
        }
        batch.setClipRect((float)r.viewX, (float)r.viewY, (float)r.viewW, (float)r.viewH);
    }

    /* SetBuffer reset the viewport to the whole buffer in bbSetBuffer;
       bb_cmds_canvas.cpp calls this once the new buffer is current. */
    void reset_canvas_region(VM *vm)
    {
        CanvasRegion &r = region_for(vm);
        r.originX = r.originY = 0;
        r.viewX = r.viewY = r.viewW = r.viewH = 0;
        apply_clip(vm);
    }

    static void canvas_viewport(VM *vm, int &x, int &y, int &w, int &h)
    {
        const CanvasRegion &r = region_for(vm);
        if (r.viewW > 0 && r.viewH > 0)
        {
            x = r.viewX; y = r.viewY; w = r.viewW; h = r.viewH;
            return;
        }
        x = 0; y = 0;
        buffer_size(vm, current_buffer(vm), w, h);
    }

    /* The buffer an argument names: 0 means "the current one", as every
       bbReadPixel-style command in bbgraphics.cpp treated a null canvas. */
    static long long buffer_arg(VM *vm, Value v)
    {
        const long long h = arg_int(v);
        return h ? h : current_buffer(vm);
    }

    /* Size of a buffer in pixels; the screen reports the window. */
    static void buffer_size(VM *vm, long long buffer, int &w, int &h)
    {
        if (engine::ImageFrame *f = image_buffer(buffer))
        {
            w = f->pixels.width;
            h = f->pixels.height;
            return;
        }
        w = platform_for(vm)->width();
        h = platform_for(vm)->height();
    }

    /* ARGB of one pixel, in the same packing readScreenPixel returns. */
    static unsigned read_pixel(VM *vm, long long buffer, int x, int y)
    {
        if (engine::ImageFrame *f = image_buffer(buffer))
        {
            if (x < 0 || y < 0 || x >= f->pixels.width || y >= f->pixels.height) return 0;
            const engine::Color c = f->pixels.get_pixel_color((engine::u32)x, (engine::u32)y);
            return ((unsigned)c.a() << 24) | ((unsigned)c.r() << 16) | ((unsigned)c.g() << 8) | c.b();
        }
        return platform_for(vm)->readScreenPixel(x, y);
    }

    static bool write_pixel(VM *vm, long long buffer, int x, int y, unsigned argb)
    {
        engine::ImageFrame *f = image_buffer(buffer);
        if (!f) return false;
        if (x < 0 || y < 0 || x >= f->pixels.width || y >= f->pixels.height) return true;
        f->pixels.set_pixel((engine::u32)x, (engine::u32)y, (engine::u8)((argb >> 16) & 0xff),
                            (engine::u8)((argb >> 8) & 0xff), (engine::u8)(argb & 0xff),
                            (engine::u8)((argb >> 24) & 0xff));
        f->dirty = true;
        f->cmMask.clear();
        f->cmPitch = 0;
        (void)vm;
        return true;
    }

    static void warn_screen_write(VM *vm, const char *cmd)
    {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "%s: writing pixels to the screen is not supported - draw to an ImageBuffer", cmd);
        zen::backend_log(vm->backend(), zen::LOG_WARN, msg);
    }

    static int c_GraphicsBuffer(VM *vm, Value *args, int)
    {
        args[0] = val_int(kScreenBuffer);
        (void)vm;
        return 1;
    }

    static int c_Origin(VM *vm, Value *args, int)
    {
        CanvasRegion &r = region_for(vm);
        r.originX = (int)arg_int(args[0]);
        r.originY = (int)arg_int(args[1]);
        return 0;
    }

    /* gxCanvas::setViewport clipped the rectangle to the buffer and
       collapsed it to nothing when it fell outside entirely. */
    static int c_Viewport(VM *vm, Value *args, int)
    {
        int bw = 0, bh = 0;
        buffer_size(vm, current_buffer(vm), bw, bh);
        int x = (int)arg_int(args[0]), y = (int)arg_int(args[1]);
        int w = (int)arg_int(args[2]), h = (int)arg_int(args[3]);
        int right = x + w, bottom = y + h;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (right > bw) right = bw;
        if (bottom > bh) bottom = bh;
        if (right <= x || bottom <= y) { x = y = right = bottom = 0; }

        CanvasRegion &r = region_for(vm);
        r.viewX = x; r.viewY = y;
        r.viewW = right - x; r.viewH = bottom - y;
        apply_clip(vm);
        return 0;
    }

    /* Blitz3D locked a surface so the Fast reads/writes could skip the
       per-call lock. An Image frame is plain CPU memory that is already
       addressable, so these only have to exist and validate. */
    static int c_LockBuffer(VM *vm, Value *args, int)
    {
        const long long buffer = buffer_arg(vm, args[0]);
        if (buffer != kScreenBuffer && !image_buffer(buffer))
        {
            vm->runtime_error("LockBuffer: buffer does not exist");
            return -1;
        }
        return 0;
    }

    static int c_UnlockBuffer(VM *vm, Value *args, int)
    {
        const long long buffer = buffer_arg(vm, args[0]);
        if (buffer != kScreenBuffer && !image_buffer(buffer))
        {
            vm->runtime_error("UnlockBuffer: buffer does not exist");
            return -1;
        }
        return 0;
    }

    /* ReadPixel/WritePixel apply the origin and the viewport; the Fast
       variants are raw, exactly as gxCanvas separated them. */
    static int c_ReadPixel(VM *vm, Value *args, int)
    {
        const long long buffer = buffer_arg(vm, args[2]);
        int ox = 0, oy = 0, vx = 0, vy = 0, vw = 0, vh = 0;
        canvas_origin(vm, ox, oy);
        canvas_viewport(vm, vx, vy, vw, vh);
        const int x = (int)arg_int(args[0]) + ox, y = (int)arg_int(args[1]) + oy;
        if (x < vx || x >= vx + vw || y < vy || y >= vy + vh) { args[0] = val_int(0); return 1; }
        args[0] = val_int((long long)(int)read_pixel(vm, buffer, x, y));
        return 1;
    }

    static int c_ReadPixelFast(VM *vm, Value *args, int)
    {
        const long long buffer = buffer_arg(vm, args[2]);
        args[0] = val_int((long long)(int)read_pixel(vm, buffer, (int)arg_int(args[0]),
                                                    (int)arg_int(args[1])));
        return 1;
    }

    static int c_WritePixel(VM *vm, Value *args, int)
    {
        const long long buffer = buffer_arg(vm, args[3]);
        int ox = 0, oy = 0, vx = 0, vy = 0, vw = 0, vh = 0;
        canvas_origin(vm, ox, oy);
        canvas_viewport(vm, vx, vy, vw, vh);
        const int x = (int)arg_int(args[0]) + ox, y = (int)arg_int(args[1]) + oy;
        if (x < vx || x >= vx + vw || y < vy || y >= vy + vh) return 0;
        if (!write_pixel(vm, buffer, x, y, (unsigned)arg_int(args[2]))) warn_screen_write(vm, "WritePixel");
        return 0;
    }

    static int c_WritePixelFast(VM *vm, Value *args, int)
    {
        const long long buffer = buffer_arg(vm, args[3]);
        if (!write_pixel(vm, buffer, (int)arg_int(args[0]), (int)arg_int(args[1]),
                         (unsigned)arg_int(args[2])))
            warn_screen_write(vm, "WritePixelFast");
        return 0;
    }

    static int copy_pixel(VM *vm, Value *args, const char *cmd)
    {
        const long long src = buffer_arg(vm, args[2]);
        const long long dst = buffer_arg(vm, args[5]);
        const unsigned argb = read_pixel(vm, src, (int)arg_int(args[0]), (int)arg_int(args[1]));
        if (!write_pixel(vm, dst, (int)arg_int(args[3]), (int)arg_int(args[4]), argb))
            warn_screen_write(vm, cmd);
        return 0;
    }

    static int c_CopyPixel(VM *vm, Value *args, int) { return copy_pixel(vm, args, "CopyPixel"); }
    static int c_CopyPixelFast(VM *vm, Value *args, int) { return copy_pixel(vm, args, "CopyPixelFast"); }

    /* bbCopyRect: a straight rectangle copy between two buffers, source
       clipped to what actually exists in both. */
    static int c_CopyRect(VM *vm, Value *args, int)
    {
        const int sx = (int)arg_int(args[0]), sy = (int)arg_int(args[1]);
        int w = (int)arg_int(args[2]), h = (int)arg_int(args[3]);
        const int dx = (int)arg_int(args[4]), dy = (int)arg_int(args[5]);
        const long long src = buffer_arg(vm, args[6]);
        const long long dst = buffer_arg(vm, args[7]);
        if (w <= 0 || h <= 0) return 0;

        engine::ImageFrame *dstFrame = image_buffer(dst);
        if (!dstFrame) { warn_screen_write(vm, "CopyRect"); return 0; }

        int sw = 0, sh = 0;
        buffer_size(vm, src, sw, sh);
        if (sx + w > sw) w = sw - sx;
        if (sy + h > sh) h = sh - sy;
        if (w <= 0 || h <= 0) return 0;

        /* copy through a temporary so a rectangle overlapping itself
           inside one buffer still reads the original pixels */
        ct::Vector<unsigned> tmp((size_t)w * (size_t)h, 0u);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                tmp[(size_t)y * (size_t)w + (size_t)x] = read_pixel(vm, src, sx + x, sy + y);

        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                write_pixel(vm, dst, dx + x, dy + y, tmp[(size_t)y * (size_t)w + (size_t)x]);
        return 0;
    }

    /* bbGrabImage: copy a rectangle of the current buffer into one frame
       of an image, offset by the source origin and the frame's handle. */
    static int c_GrabImage(VM *vm, Value *args, int)
    {
        const long long imageHandle = arg_int(args[0]);
        engine::Image *img = image_of_handle(imageHandle);
        engine::ImageFrame *frame = nullptr;
        const int n = (int)arg_int(args[3]);
        if (!img || n < 0 || n >= img->frameCount()) return 0;
        frame = &img->frame(n);

        int ox = 0, oy = 0;
        canvas_origin(vm, ox, oy);
        const int x = (int)arg_int(args[1]) + ox - img->handleX();
        const int y = (int)arg_int(args[2]) + oy - img->handleY();

        const long long src = current_buffer(vm);
        const int w = frame->pixels.width, h = frame->pixels.height;
        for (int py = 0; py < h; ++py)
            for (int px = 0; px < w; ++px)
            {
                const unsigned argb = read_pixel(vm, src, x + px, y + py);
                frame->pixels.set_pixel((engine::u32)px, (engine::u32)py,
                                        (engine::u8)((argb >> 16) & 0xff),
                                        (engine::u8)((argb >> 8) & 0xff),
                                        (engine::u8)(argb & 0xff), 255);
            }
        frame->dirty = true;
        frame->cmMask.clear();
        frame->cmPitch = 0;
        return 0;
    }

    /* bbLoadBuffer stretched the loaded picture over the whole buffer. */
    static int c_LoadBuffer(VM *vm, Value *args, int)
    {
        const long long buffer = buffer_arg(vm, args[0]);
        engine::ImageFrame *f = image_buffer(buffer);
        if (!f) { warn_screen_write(vm, "LoadBuffer"); args[0] = val_int(0); return 1; }

        engine::Pixmap loaded;
        if (!loaded.load(arg_cstr(args[1]))) { args[0] = val_int(0); return 1; }

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
        args[0] = val_int(1);
        return 1;
    }

    static int c_SaveBuffer(VM *vm, Value *args, int)
    {
        const long long buffer = buffer_arg(vm, args[0]);
        const char *path = arg_cstr(args[1]);
        if (engine::ImageFrame *f = image_buffer(buffer))
        {
            args[0] = val_int(f->pixels.save(path) ? 1 : 0);
            return 1;
        }
        /* the screen: read it back a row at a time into a pixmap */
        engine::Platform *p = platform_for(vm);
        const int w = p->width(), h = p->height();
        if (w <= 0 || h <= 0) { args[0] = val_int(0); return 1; }
        engine::Pixmap shot(w, h, 4);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const unsigned argb = p->readScreenPixel(x, y);
                shot.set_pixel((engine::u32)x, (engine::u32)y, (engine::u8)((argb >> 16) & 0xff),
                               (engine::u8)((argb >> 8) & 0xff), (engine::u8)(argb & 0xff), 255);
            }
        args[0] = val_int(shot.save(path) ? 1 : 0);
        return 1;
    }

    /* BufferDirty told Blitz3D a locked surface had changed behind its
       back; here the frame's own dirty flag is what schedules the upload. */
    static int c_BufferDirty(VM *vm, Value *args, int)
    {
        if (engine::ImageFrame *f = image_buffer(buffer_arg(vm, args[0]))) f->dirty = true;
        return 0;
    }

    static int c_GetColor(VM *vm, Value *args, int)
    {
        int ox = 0, oy = 0;
        canvas_origin(vm, ox, oy);
        const unsigned argb = read_pixel(vm, current_buffer(vm), (int)arg_int(args[0]) + ox,
                                         (int)arg_int(args[1]) + oy);
        set_canvas_color(vm, argb | 0xff000000u);
        return 0;
    }

    static int c_ColorRed(VM *vm, Value *args, int)
    {
        args[0] = val_int((canvas_color(vm) >> 16) & 0xff);
        return 1;
    }
    static int c_ColorGreen(VM *vm, Value *args, int)
    {
        args[0] = val_int((canvas_color(vm) >> 8) & 0xff);
        return 1;
    }
    static int c_ColorBlue(VM *vm, Value *args, int)
    {
        args[0] = val_int(canvas_color(vm) & 0xff);
        return 1;
    }

    extern const zen::CommandDecl bb3d_cmds_buffer[] = {
        {"%GraphicsBuffer", c_GraphicsBuffer},
        {"Origin%x%y", c_Origin},
        {"Viewport%x%y%width%height", c_Viewport},

        {"LockBuffer%buffer=0", c_LockBuffer},
        {"UnlockBuffer%buffer=0", c_UnlockBuffer},
        {"%ReadPixel%x%y%buffer=0", c_ReadPixel},
        {"WritePixel%x%y%argb%buffer=0", c_WritePixel},
        {"%ReadPixelFast%x%y%buffer=0", c_ReadPixelFast},
        {"WritePixelFast%x%y%argb%buffer=0", c_WritePixelFast},
        {"CopyPixel%src_x%src_y%src_buffer%dest_x%dest_y%dest_buffer=0", c_CopyPixel},
        {"CopyPixelFast%src_x%src_y%src_buffer%dest_x%dest_y%dest_buffer=0", c_CopyPixelFast},
        {"CopyRect%source_x%source_y%width%height%dest_x%dest_y%src_buffer=0%dest_buffer=0", c_CopyRect},
        {"GrabImage%image%x%y%frame=0", c_GrabImage},

        {"%LoadBuffer%buffer$bmpfile", c_LoadBuffer},
        {"%SaveBuffer%buffer$bmpfile", c_SaveBuffer},
        {"BufferDirty%buffer", c_BufferDirty},

        {"GetColor%x%y", c_GetColor},
        {"%ColorRed", c_ColorRed},
        {"%ColorGreen", c_ColorGreen},
        {"%ColorBlue", c_ColorBlue},
    };
    extern const int bb3d_cmds_buffer_count =
        (int)(sizeof(bb3d_cmds_buffer) / sizeof(bb3d_cmds_buffer[0]));
}
