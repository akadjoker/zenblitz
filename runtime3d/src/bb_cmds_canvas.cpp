/*
** bb_cmds_canvas.cpp — Color, Plot, Line, Rect, Oval, Text.
*/
#include "runtime.h"
#include "vm.h"
#include "object.h"
#include "ct/hashmap.hpp"
#include "engine/Platform.h"

namespace bb3d { extern engine::Platform *platform_for(zen::VM *vm); }

namespace { inline long long arg_int(zen::Value v) { return zen::is_int(v) ? v.as.integer
    : zen::is_float(v) ? (long long)v.as.number : 0; }
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

    static int c_Text(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        float x = (float)arg_int(args[0]), y = (float)arg_int(args[1]);
        platform_for(vm)->batch().drawText(x, y, 16.0f, arg_cstr(args[2]));
        return 0;
    }

    extern const zen::CommandDecl bb3d_cmds_canvas[] = {
        {"Color%red%green%blue", c_Color},
        {"Plot%x%y", c_Plot},
        {"Line%x1%y1%x2%y2", c_Line},
        {"Rect%x%y%width%height%solid=1", c_Rect},
        {"Oval%x%y%width%height%solid=1", c_Oval},
        {"Text%x%y$text%centerx=0%centery=0", c_Text},
    };
    extern const int bb3d_cmds_canvas_count = (int)(sizeof(bb3d_cmds_canvas) / sizeof(bb3d_cmds_canvas[0]));
}
