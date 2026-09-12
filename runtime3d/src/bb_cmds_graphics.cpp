/*
** bb_cmds_graphics.cpp — Graphics, Flip, Cls, ClsColor, KeyDown, KeyHit,
** MouseX/Y/Down, GraphicsWidth/Height, EndGraphics.
*/
#include "runtime.h"
#include "vm.h"
#include "object.h"
#include "ct/hashmap.hpp"
#include "engine/Platform.h"

namespace { inline long long arg_int(zen::Value v) { return zen::is_int(v) ? v.as.integer
    : zen::is_float(v) ? (long long)v.as.number : 0; } }

using namespace zen;

namespace bb3d
{
    static ct::HashMap<VM *, engine::Platform *> g_platforms;

    engine::Platform *platform_for(VM *vm)
    {
        engine::Platform **found = g_platforms.find(vm);
        if (found) return *found;
        engine::Platform *p = new engine::Platform();
        g_platforms.put(vm, p);
        return p;
    }

    static int c_Graphics(VM *vm, Value *args, int nargs)
    {
        int w = (int)arg_int(args[0]);
        int h = (int)arg_int(args[1]);
        engine::Platform *p = platform_for(vm);
        bool ok = p->open(w, h, "zenblitz3d", false);
        if (ok) p->setTargetFPS(60);
        args[0] = val_int(ok ? 1 : 0);
        (void)nargs;
        return 1;
    }
    static int c_Graphics3D(VM *vm, Value *args, int nargs) { return c_Graphics(vm, args, nargs); }

    static int c_EndGraphics(VM *vm, Value *args, int nargs)
    {
        (void)args; (void)nargs;
        platform_for(vm)->close();
        return 0;
    }

    static int c_GraphicsWidth(VM *vm, Value *args, int)
    {
        args[0] = val_int(platform_for(vm)->width());
        return 1;
    }
    static int c_GraphicsHeight(VM *vm, Value *args, int)
    {
        args[0] = val_int(platform_for(vm)->height());
        return 1;
    }

    static int c_Cls(VM *vm, Value *args, int nargs)
    {
        (void)args; (void)nargs;
        platform_for(vm)->beginFrame();
        return 0;
    }

    static unsigned char g_clsR = 0, g_clsG = 0, g_clsB = 0;
    static int c_ClsColor(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        g_clsR = (unsigned char)arg_int(args[0]);
        g_clsG = (unsigned char)arg_int(args[1]);
        g_clsB = (unsigned char)arg_int(args[2]);
        platform_for(vm)->setClearColor(g_clsR / 255.0f, g_clsG / 255.0f, g_clsB / 255.0f);
        return 0;
    }

    static int c_Flip(VM *vm, Value *args, int nargs)
    {
        (void)args; (void)nargs;
        platform_for(vm)->endFrame();
        vm->request_suspend(0);
        return 0;
    }

    static int c_KeyDown(VM *vm, Value *args, int)
    {
        int dik = (int)arg_int(args[0]);
        args[0] = val_int(platform_for(vm)->keyDown(dik) ? 1 : 0);
        return 1;
    }
    static int c_KeyHit(VM *vm, Value *args, int)
    {
        int dik = (int)arg_int(args[0]);
        args[0] = val_int(platform_for(vm)->keyHit(dik) ? 1 : 0);
        return 1;
    }
    static int c_FlushKeys(VM *vm, Value *, int) { platform_for(vm)->flushKeyHits(); return 0; }

    static int c_MouseX(VM *vm, Value *args, int) { args[0] = val_int(platform_for(vm)->mouseX()); return 1; }
    static int c_MouseY(VM *vm, Value *args, int) { args[0] = val_int(platform_for(vm)->mouseY()); return 1; }
    static int c_MouseDown(VM *vm, Value *args, int)
    {
        int b = (int)arg_int(args[0]);
        args[0] = val_int(platform_for(vm)->mouseDown(b) ? 1 : 0);
        return 1;
    }

    /* extern: without it, a const array at namespace scope has internal
       linkage and runtime3d.cpp's extern declaration fails to link. */
    extern const zen::CommandDecl bb3d_cmds_graphics[] = {
        {"%Graphics%width%height%depth=0%mode=0", c_Graphics},
        {"%Graphics3D%width%height%depth=0%mode=0", c_Graphics3D},
        {"EndGraphics", c_EndGraphics},
        {"%GraphicsWidth", c_GraphicsWidth},
        {"%GraphicsHeight", c_GraphicsHeight},

        {"Cls", c_Cls},
        {"ClsColor%red%green%blue", c_ClsColor},
        {"Flip%vwait=1", c_Flip},

        {"%KeyDown%key", c_KeyDown},
        {"%KeyHit%key", c_KeyHit},
        {"FlushKeys", c_FlushKeys},
        {"%MouseX", c_MouseX},
        {"%MouseY", c_MouseY},
        {"%MouseDown%button", c_MouseDown},
    };
    extern const int bb3d_cmds_graphics_count = (int)(sizeof(bb3d_cmds_graphics) / sizeof(bb3d_cmds_graphics[0]));
}
