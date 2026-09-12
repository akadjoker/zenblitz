/*
** bb_cmds_canvas.cpp — Color, Plot, Line, Rect, Oval, Text, fonts.
*/
#include "runtime.h"
#include "vm.h"
#include "object.h"
#include "ct/hashmap.hpp"
#include "engine/Platform.h"

namespace bb3d { extern engine::Platform *platform_for(zen::VM *vm); }

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
    static int c_Color(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        unsigned char r = (unsigned char)arg_int(args[0]);
        unsigned char g = (unsigned char)arg_int(args[1]);
        unsigned char b = (unsigned char)arg_int(args[2]);
        platform_for(vm)->batch().setColor(r, g, b);
        return 0;
    }

    static int c_Plot(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        float x = (float)arg_int(args[0]), y = (float)arg_int(args[1]);
        platform_for(vm)->batch().drawRect(x, y, 1.0f, 1.0f, true);
        return 0;
    }

    static int c_Line(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        float x1 = (float)arg_int(args[0]), y1 = (float)arg_int(args[1]);
        float x2 = (float)arg_int(args[2]), y2 = (float)arg_int(args[3]);
        platform_for(vm)->batch().drawLine(x1, y1, x2, y2);
        return 0;
    }

    static int c_Rect(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        float x = (float)arg_int(args[0]), y = (float)arg_int(args[1]);
        float w = (float)arg_int(args[2]), h = (float)arg_int(args[3]);
        bool solid = arg_int(args[4]) != 0;
        platform_for(vm)->batch().drawRect(x, y, w, h, solid);
        return 0;
    }

    static int c_Oval(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        float x = (float)arg_int(args[0]), y = (float)arg_int(args[1]);
        float w = (float)arg_int(args[2]), h = (float)arg_int(args[3]);
        bool solid = arg_int(args[4]) != 0;
        platform_for(vm)->batch().drawEllipse(x + w * 0.5f, y + h * 0.5f, w * 0.5f, h * 0.5f, solid);
        return 0;
    }

    static ct::HashMap<VM *, float> g_fontSize;

    static int c_Text(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        float x = (float)arg_int(args[0]), y = (float)arg_int(args[1]);
        float *size = g_fontSize.find(vm);
        platform_for(vm)->batch().drawText(x, y, size ? *size : 16.0f, arg_cstr(args[2]));
        return 0;
    }

    static int c_LoadFont(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        float size = arg_float(args[1]);
        if (size <= 0.0f) size = 16.0f;
        Bb3dFont *f = new Bb3dFont{size};
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
        if (arg_int(args[0]) != kScreenBuffer)
        {
            vm->runtime_error("SetBuffer: only BackBuffer()/FrontBuffer() are supported (no ImageBuffer target yet)");
            return -1;
        }
        return 0;
    }

    extern const zen::CommandDecl bb3d_cmds_canvas[] = {
        {"Color%red%green%blue", c_Color},
        {"Plot%x%y", c_Plot},
        {"Line%x1%y1%x2%y2", c_Line},
        {"Rect%x%y%width%height%solid=1", c_Rect},
        {"Oval%x%y%width%height%solid=1", c_Oval},
        {"Text%x%y$text%centerx=0%centery=0", c_Text},

        {"%LoadFont$name%height=12%bold=0%italic=0%underline=0", c_LoadFont},
        {"SetFont%font", c_SetFont},
        {"FreeFont%font", c_FreeFont},

        {"%BackBuffer", c_BackBuffer},
        {"%FrontBuffer", c_FrontBuffer},
        {"SetBuffer%buffer", c_SetBuffer},
    };
    extern const int bb3d_cmds_canvas_count = (int)(sizeof(bb3d_cmds_canvas) / sizeof(bb3d_cmds_canvas[0]));
}
