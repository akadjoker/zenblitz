/*
** bb_cmds_graphics.cpp — Graphics, Flip, Cls, ClsColor, KeyDown, KeyHit,
** MouseX/Y/Down, GraphicsWidth/Height, EndGraphics.
*/
#include "runtime.h"
#include "vm.h"
#include "object.h"
#include "ct/hashmap.hpp"
#include "engine/Platform.h"
#include "engine/World.h"

namespace { inline long long arg_int(zen::Value v) { return zen::is_int(v) ? v.as.integer
    : zen::is_float(v) ? (long long)v.as.number : 0; } }

using namespace zen;

namespace bb3d
{
    static ct::HashMap<VM *, engine::Platform *> g_platforms;
    static ct::HashMap<VM *, engine::World *> g_worlds;

    engine::Platform *platform_for(VM *vm)
    {
        engine::Platform **found = g_platforms.find(vm);
        if (found) return *found;
        engine::Platform *p = new engine::Platform();
        g_platforms.put(vm, p);
        return p;
    }

    engine::World *world_for(VM *vm)
    {
        engine::World **found = g_worlds.find(vm);
        if (found) return *found;
        engine::World *w = new engine::World();
        g_worlds.put(vm, w);
        return w;
    }

    extern void free_all_entities(gpu::Device *dev);
    extern void free_all_textures();
    extern void free_all_fonts();

    // Releases everything the runtime allocated for this VM. A Blitz
    // program normally runs until the user closes the window, so nothing
    // here is ever freed by the script itself - without this the process
    // exits with the whole scene, both renderers and the GL device still
    // live (which AddressSanitizer rightly reports as leaks).
    // Order matters: entities and textures hold GPU handles owned by the
    // World/Platform, so they go first.
    void shutdown_graphics(VM *vm)
    {
        engine::World **w = g_worlds.find(vm);
        engine::Platform **p = g_platforms.find(vm);

        // the device has to outlive the GPU buffers the scene owns
        free_all_entities(p && (*p)->isOpen() ? &(*p)->device() : nullptr);
        free_all_textures();
        free_all_fonts();

        if (w) (*w)->shutdown();
        if (p) (*p)->close();
        if (w) { delete *w; g_worlds.erase(vm); }
        if (p) { delete *p; g_platforms.erase(vm); }
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
    static int c_Graphics3D(VM *vm, Value *args, int nargs)
    {
        int rv = c_Graphics(vm, args, nargs);
        if (is_int(args[0]) && args[0].as.integer)
            world_for(vm)->init(platform_for(vm)->device(), platform_for(vm)->shaderDialect());
        return rv;
    }

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

    // VWait/WaitKey/MouseWait block the calling native until the next
    // frame / a key / a mouse click, pumping events (and pacing to
    // setTargetFPS, same as Flip) each iteration ourselves rather than
    // suspending the fiber - main3d.cpp's resume() loop does nothing else
    // meanwhile, so there's no host-side work being starved by blocking
    // here the way there would be in an embedding that shares the thread.
    static int c_VWait(VM *vm, Value *args, int nargs)
    {
        (void)args; (void)nargs;
        platform_for(vm)->pumpEvents();
        return 0;
    }
    static int c_WaitKey(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        engine::Platform *p = platform_for(vm);
        p->flushKeyHits();
        int dik = 0;
        while (p->isOpen() && !dik)
        {
            p->pumpEvents();
            for (int k = 1; k < 256; ++k)
                if (p->keyHit(k)) { dik = k; break; }
        }
        args[0] = val_int(dik);
        return 1;
    }
    static int c_MouseWait(VM *vm, Value *args, int nargs)
    {
        (void)args; (void)nargs;
        engine::Platform *p = platform_for(vm);
        bool wasDown[4] = {false, false, false, false};
        for (int b = 1; b <= 3; ++b) wasDown[b] = p->mouseDown(b);
        for (;;)
        {
            p->pumpEvents();
            if (!p->isOpen()) break;
            bool clicked = false;
            for (int b = 1; b <= 3; ++b)
            {
                bool down = p->mouseDown(b);
                if (down && !wasDown[b]) clicked = true;
                wasDown[b] = down;
            }
            if (clicked) break;
        }
        return 0;
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

        {"VWait", c_VWait},
        {"%WaitKey", c_WaitKey},
        {"MouseWait", c_MouseWait},
    };
    extern const int bb3d_cmds_graphics_count = (int)(sizeof(bb3d_cmds_graphics) / sizeof(bb3d_cmds_graphics[0]));
}
