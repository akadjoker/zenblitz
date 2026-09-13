/*
** bb_cmds_canvas.cpp — Color, Plot, Line, Rect, Oval, Text, fonts.
*/
#include "runtime.h"
#include "vm.h"
#include "object.h"
#include "ct/hashmap.hpp"
#include "ct/vector.hpp"
#include "engine/Platform.h"
#include "engine/Image.h"

namespace bb3d { extern engine::Platform *platform_for(zen::VM *vm); extern engine::ImageFrame *image_buffer(long long handle); extern void reset_canvas_region(zen::VM *vm); }

namespace
{
    // Blitz3D's LoadFont/SetFont pick a system TrueType font by family
    // name and point size. This runtime only ships the one embedded
    // bitmap font BatchRenderer draws (see Batch.h/drawText), so a "font"
    // here is just the point size to pass through to drawText - the
    // family name is accepted (so real Blitz3D programs still load) and
    // otherwise ignored rather than pretending to support arbitrary
    // TrueType families we don't actually have.
    struct Bb3dFont { float size; };
}

namespace { inline long long arg_int(zen::Value v) { return zen::is_int(v) ? v.as.integer
    : zen::is_float(v) ? (long long)v.as.number : 0; }
    inline float arg_float(zen::Value v) { return zen::is_float(v) ? (float)v.as.number
        : zen::is_int(v) ? (float)v.as.integer : 0.0f; }
    inline const char *arg_cstr(zen::Value v) { return zen::is_string(v) ? zen::as_cstring(v) : ""; } }

using namespace zen;

namespace bb3d
{
    static ct::HashMap<VM *, long long> g_canvasBuffers;
    static ct::HashMap<VM *, unsigned> g_canvasColors;

    static engine::ImageFrame *target_for(VM *vm)
    {
        long long *handle = g_canvasBuffers.find(vm);
        return handle ? image_buffer(*handle) : nullptr;
    }
    static engine::Color color_for(VM *vm)
    {
        unsigned *color = g_canvasColors.find(vm);
        return engine::Color(color ? *color : 0xffffffffu);
    }
    static void dirty(engine::ImageFrame *frame) { if (frame) frame->dirty = true; }

    /* Accessors for bb_cmds_buffer.cpp, which owns the pixel-level
       commands but must share this file's current buffer and colour
       rather than keep a second copy of either. */
    long long current_buffer(VM *vm)
    {
        long long *handle = g_canvasBuffers.find(vm);
        return handle ? *handle : 1 /* kScreenBuffer */;
    }
    unsigned canvas_color(VM *vm) { return color_for(vm).value(); }
    void set_canvas_color(VM *vm, unsigned argb)
    {
        g_canvasColors.put(vm, argb);
        const engine::Color c(argb);
        platform_for(vm)->batch().setColor(c.r(), c.g(), c.b());
    }

    static int c_Color(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        unsigned char r = (unsigned char)arg_int(args[0]);
        unsigned char g = (unsigned char)arg_int(args[1]);
        unsigned char b = (unsigned char)arg_int(args[2]);
        g_canvasColors.put(vm, engine::Color(r, g, b).value());
        platform_for(vm)->batch().setColor(r, g, b);
        return 0;
    }

    static int c_Plot(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        const int x = (int)arg_int(args[0]), y = (int)arg_int(args[1]);
        if (engine::ImageFrame *target = target_for(vm)) { target->pixels.set_pixel(x, y, color_for(vm).value()); dirty(target); }
        else platform_for(vm)->batch().drawRect((float)x, (float)y, 1.0f, 1.0f, true);
        return 0;
    }

    static int c_Line(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        const int x1 = (int)arg_int(args[0]), y1 = (int)arg_int(args[1]), x2 = (int)arg_int(args[2]), y2 = (int)arg_int(args[3]);
        if (engine::ImageFrame *target = target_for(vm)) { target->pixels.draw_line(x1, y1, x2, y2, color_for(vm)); dirty(target); }
        else platform_for(vm)->batch().drawLine((float)x1, (float)y1, (float)x2, (float)y2);
        return 0;
    }

    static int c_Rect(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        int x = (int)arg_int(args[0]), y = (int)arg_int(args[1]), w = (int)arg_int(args[2]), h = (int)arg_int(args[3]);
        bool solid = arg_int(args[4]) != 0;
        if (engine::ImageFrame *target = target_for(vm)) { target->pixels.draw_rect(x, y, w, h, color_for(vm), solid); dirty(target); }
        else platform_for(vm)->batch().drawRect((float)x, (float)y, (float)w, (float)h, solid);
        return 0;
    }

    static int c_Oval(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        int x = (int)arg_int(args[0]), y = (int)arg_int(args[1]), w = (int)arg_int(args[2]), h = (int)arg_int(args[3]);
        bool solid = arg_int(args[4]) != 0;
        if (engine::ImageFrame *target = target_for(vm)) { target->pixels.draw_circle(x + w / 2, y + h / 2, (w < h ? w : h) / 2, color_for(vm), solid); dirty(target); }
        else platform_for(vm)->batch().drawEllipse(x + w * 0.5f, y + h * 0.5f, w * 0.5f, h * 0.5f, solid);
        return 0;
    }

    static ct::HashMap<VM *, float> g_fontSize;
    // every LoadFont allocation, so shutdown_graphics() can free the ones
    // a script never passed to FreeFont.
    static ct::Vector<Bb3dFont *> g_fonts;

    // extern: called by shutdown_graphics() when the program ends.
    void free_all_fonts()
    {
        for (size_t k = 0; k < g_fonts.size(); ++k) delete g_fonts[k];
        g_fonts.clear();
        g_fontSize.clear();
    }

    static int c_Text(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        float x = (float)arg_int(args[0]), y = (float)arg_int(args[1]);
        float *size = g_fontSize.find(vm);
        const float fontSize = size ? *size : 16.0f;
        const char *text = arg_cstr(args[2]);
        if (arg_int(args[3])) x -= platform_for(vm)->batch().textWidth(fontSize, text) * 0.5f;
        if (arg_int(args[4])) y -= fontSize * 0.5f;
        platform_for(vm)->batch().drawText(x, y, fontSize, text);
        return 0;
    }

    static int c_FontWidth(VM *vm, Value *args, int)
    {
        float *size = g_fontSize.find(vm);
        args[0] = val_int((long long)platform_for(vm)->batch().textWidth(size ? *size : 16.0f, "W"));
        return 1;
    }
    static int c_FontHeight(VM *vm, Value *args, int)
    {
        float *size = g_fontSize.find(vm);
        args[0] = val_int((long long)(size ? *size : 16.0f));
        return 1;
    }
    static int c_StringWidth(VM *vm, Value *args, int)
    {
        float *size = g_fontSize.find(vm);
        args[0] = val_int((long long)platform_for(vm)->batch().textWidth(size ? *size : 16.0f, arg_cstr(args[0])));
        return 1;
    }
    static int c_StringHeight(VM *vm, Value *args, int)
    {
        float *size = g_fontSize.find(vm);
        args[0] = val_int((long long)(size ? *size : 16.0f));
        return 1;
    }

    static int c_LoadFont(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        float size = arg_float(args[1]);
        if (size <= 0.0f) size = 16.0f;
        Bb3dFont *f = new Bb3dFont{size};
        g_fonts.push_back(f);
        args[0] = val_int((long long)(std::intptr_t)f);
        return 1;
    }
    static int c_SetFont(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        Bb3dFont *f = (Bb3dFont *)(std::intptr_t)arg_int(args[0]);
        if (f) g_fontSize.put(vm, f->size);
        return 0;
    }
    static int c_FreeFont(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        Bb3dFont *f = (Bb3dFont *)(std::intptr_t)arg_int(args[0]);
        for (size_t k = 0; k < g_fonts.size(); ++k)
            if (g_fonts[k] == f) { g_fonts.erase(g_fonts.begin() + k); break; }
        delete f;
        return 0;
    }

    // Blitz3D's SetBuffer/BackBuffer/FrontBuffer pick which surface 2D
    // drawing targets - a window's front and back buffer, or an
    // ImageBuffer's pixels. This runtime has no offscreen ImageBuffer
    // target yet and always draws to the one back buffer, so BackBuffer()/
    // FrontBuffer() just hand back a fixed handle for "the screen" and
    // SetBuffer accepts it as a no-op; passing anything else is a runtime
    // error rather than silently doing the wrong thing.
    static const long long kScreenBuffer = 1;

    static int c_BackBuffer(VM *vm, Value *args, int)
    {
        (void)vm;
        args[0] = val_int(kScreenBuffer);
        return 1;
    }
    static int c_FrontBuffer(VM *vm, Value *args, int)
    {
        (void)vm;
        args[0] = val_int(kScreenBuffer);
        return 1;
    }
    static int c_SetBuffer(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        const long long buffer = arg_int(args[0]);
        if (buffer != kScreenBuffer && !image_buffer(buffer))
        {
            vm->runtime_error("SetBuffer: buffer does not exist");
            return -1;
        }
        if (buffer == kScreenBuffer) g_canvasBuffers.erase(vm);
        else g_canvasBuffers.put(vm, buffer);
        /* bbSetBuffer reset the origin and the viewport to the new
           buffer, so a viewport set for one never clips another */
        reset_canvas_region(vm);
        return 0;
    }

    extern const zen::CommandDecl bb3d_cmds_canvas[] = {
        {"Color%red%green%blue", c_Color},
        {"Plot%x%y", c_Plot},
        {"Line%x1%y1%x2%y2", c_Line},
        {"Rect%x%y%width%height%solid=1", c_Rect},
        {"Oval%x%y%width%height%solid=1", c_Oval},
        {"Text%x%y$text%centre_x=0%centre_y=0", c_Text},

        {"%LoadFont$fontname%height=12%bold=0%italic=0%underline=0", c_LoadFont},
        {"SetFont%font", c_SetFont},
        {"FreeFont%font", c_FreeFont},
        {"%FontWidth", c_FontWidth},
        {"%FontHeight", c_FontHeight},
        {"%StringWidth$string", c_StringWidth},
        {"%StringHeight$string", c_StringHeight},

        {"%BackBuffer", c_BackBuffer},
        {"%FrontBuffer", c_FrontBuffer},
        {"SetBuffer%buffer", c_SetBuffer},
    };
    extern const int bb3d_cmds_canvas_count = (int)(sizeof(bb3d_cmds_canvas) / sizeof(bb3d_cmds_canvas[0]));
}
