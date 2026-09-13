/*
** bb_cmds_gfxmode.cpp — the rest of bbgraphics.cpp's display-mode,
** driver, gamma and movie commands: CountGfxDrivers, GfxDriverName,
** SetGfxDriver, GfxDriver3D, CountGfxModes, GfxModeExists, ScanLine,
** GraphicsLost, TotalVidMem/AvailVidMem, the gamma ramp, and the
** OpenMovie family.
**
** A "driver" here is an SDL video display, the same mapping BlitzNG's
** SDL context driver used (numGraphicsDrivers -> SDL_GetNumVideoDisplays,
** numGraphicsModes -> SDL_GetNumDisplayModes). Blitz3D numbered drivers
** and modes from 1, so every index is converted on the way in and an
** out-of-range one is a runtime error, exactly as debugDriver/debugMode
** reported it.
*/
#include "runtime.h"
#include "vm.h"
#include "object.h"
#include "ct/vector.hpp"
#include "engine/Platform.h"
#include <SDL2/SDL.h>

namespace bb3d { extern engine::Platform *platform_for(zen::VM *vm); }

namespace
{
    inline long long arg_int(zen::Value v)
    {
        return zen::is_int(v) ? v.as.integer : zen::is_float(v) ? (long long)v.as.number : 0;
    }
    inline float arg_float(zen::Value v)
    {
        return zen::is_float(v) ? (float)v.as.number : zen::is_int(v) ? (float)v.as.integer : 0.0f;
    }
}

using namespace zen;

namespace bb3d
{
    /* bbSetGfxDriver kept the chosen driver and cleared the cached mode
       list; every later mode query ran against it. */
    static int g_driver = 0;

    /* the mode list itself lives in bb_cmds_graphics.cpp, which already
       had one for the 3D queries - the original kept a single gfx_modes
       vector that both families filled and read */
    extern void collectGfxModes();
    extern int gfx_mode_count();
    extern bool gfx_mode_at(int index, int &w, int &h, int &d);

    int gfx_driver() { return g_driver; }

    static bool video_ready()
    {
        return SDL_WasInit(SDL_INIT_VIDEO) || SDL_InitSubSystem(SDL_INIT_VIDEO) == 0;
    }

    static int driver_count()
    {
        if (!video_ready()) return 0;
        const int n = SDL_GetNumVideoDisplays();
        return n > 0 ? n : 0;
    }


    static int c_CountGfxDrivers(VM *vm, Value *args, int)
    {
        (void)vm;
        args[0] = val_int(driver_count());
        return 1;
    }

    static int c_GfxDriverName(VM *vm, Value *args, int)
    {
        const int n = (int)arg_int(args[0]);
        if (n < 1 || n > driver_count())
        {
            vm->runtime_error("Illegal graphics driver index");
            return -1;
        }
        const char *name = SDL_GetDisplayName(n - 1);
        args[0] = val_obj((Obj *)new_string(&vm->get_gc(), name ? name : "", name ? (int)strlen(name) : 0));
        return 1;
    }

    static int c_SetGfxDriver(VM *vm, Value *args, int)
    {
        const int n = (int)arg_int(args[0]);
        if (n < 1 || n > driver_count())
        {
            vm->runtime_error("Illegal graphics driver index");
            return -1;
        }
        g_driver = n - 1;
        collectGfxModes();
        return 0;
    }

    /* Every display this runtime can open a GL window on is 3D-capable,
       which is what BlitzNG's SDL driver reported too. */
    static int c_GfxDriver3D(VM *vm, Value *args, int)
    {
        const int n = (int)arg_int(args[0]);
        if (n < 1 || n > driver_count())
        {
            vm->runtime_error("Illegal graphics driver index");
            return -1;
        }
        args[0] = val_int(1);
        return 1;
    }

    static int c_CountGfxModes(VM *vm, Value *args, int)
    {
        (void)vm;
        if (!video_ready()) { args[0] = val_int(0); return 1; }
        collectGfxModes();
        args[0] = val_int(gfx_mode_count());
        return 1;
    }

    static int c_GfxModeExists(VM *vm, Value *args, int)
    {
        (void)vm;
        const int w = (int)arg_int(args[0]), h = (int)arg_int(args[1]);
        const int d = (int)arg_int(args[2]);
        if (!video_ready()) { args[0] = val_int(0); return 1; }
        collectGfxModes();
        int found = 0;
        for (int i = 0; i < gfx_mode_count(); ++i)
        {
            int mw = 0, mh = 0, md = 0;
            if (gfx_mode_at(i, mw, mh, md) && mw == w && mh == h && md == d) { found = 1; break; }
        }
        args[0] = val_int(found);
        return 1;
    }

    /* No backend here exposes the raster position, and Blitz3D itself
       returned 0 when the driver could not report one. */
    static int c_ScanLine(VM *vm, Value *args, int)
    {
        (void)vm;
        args[0] = val_int(0);
        return 1;
    }

    /* gxRuntime::graphicsLost tracked a lost DirectDraw surface. GL
       contexts are not lost the same way, and the window closing is
       reported through the run loop instead. */
    static int c_GraphicsLost(VM *vm, Value *args, int)
    {
        (void)vm;
        args[0] = val_int(0);
        return 1;
    }

    /* The GL backend this engine uses reports no video-memory budget, and
       Blitz3D returned 0 for drivers that could not answer either. */
    static int c_TotalVidMem(VM *vm, Value *args, int)
    {
        (void)vm;
        args[0] = val_int(0);
        return 1;
    }

    static int c_AvailVidMem(VM *vm, Value *args, int)
    {
        (void)vm;
        args[0] = val_int(0);
        return 1;
    }

    /* gxGraphics kept a 256-entry ramp per channel that SetGamma wrote
       one entry of at a time and UpdateGamma pushed to the display. */
    static Uint16 g_rampR[256], g_rampG[256], g_rampB[256];
    static bool g_rampReady = false;

    static void ensure_ramp()
    {
        if (g_rampReady) return;
        for (int i = 0; i < 256; ++i)
            g_rampR[i] = g_rampG[i] = g_rampB[i] = (Uint16)(i * 257);
        g_rampReady = true;
    }

    static int c_SetGamma(VM *vm, Value *args, int)
    {
        (void)vm;
        ensure_ramp();
        auto clamp255 = [](float v) { return v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v); };
        g_rampR[arg_int(args[0]) & 255] = (Uint16)(clamp255(arg_float(args[3])) * 257.0f);
        g_rampG[arg_int(args[1]) & 255] = (Uint16)(clamp255(arg_float(args[4])) * 257.0f);
        g_rampB[arg_int(args[2]) & 255] = (Uint16)(clamp255(arg_float(args[5])) * 257.0f);
        return 0;
    }

    static int c_UpdateGamma(VM *vm, Value *args, int)
    {
        (void)args;
        ensure_ramp();
        engine::Platform *p = platform_for(vm);
        if (!p->isOpen()) return 0;
        SDL_SetWindowGammaRamp(SDL_GL_GetCurrentWindow(), g_rampR, g_rampG, g_rampB);
        return 0;
    }

    static int c_GammaRed(VM *vm, Value *args, int)
    {
        (void)vm;
        ensure_ramp();
        args[0] = val_float(g_rampR[arg_int(args[0]) & 255] / 257.0);
        return 1;
    }
    static int c_GammaGreen(VM *vm, Value *args, int)
    {
        (void)vm;
        ensure_ramp();
        args[0] = val_float(g_rampG[arg_int(args[0]) & 255] / 257.0);
        return 1;
    }
    static int c_GammaBlue(VM *vm, Value *args, int)
    {
        (void)vm;
        ensure_ramp();
        args[0] = val_float(g_rampB[arg_int(args[0]) & 255] / 257.0);
        return 1;
    }

    /* Movies were DirectShow AVI playback. Nothing decodes video here, so
       OpenMovie fails the way Blitz3D's did for a file it could not open
       and every other command answers for a null movie - a script tests
       the handle before using it. */
    static int c_OpenMovie(VM *vm, Value *args, int)
    {
        zen::backend_log(vm->backend(), zen::LOG_WARN,
                         "OpenMovie: video playback is not supported");
        args[0] = val_int(0);
        return 1;
    }
    static int c_MovieZero(VM *vm, Value *args, int)
    {
        (void)vm;
        args[0] = val_int(0);
        return 1;
    }
    static int c_CloseMovie(VM *, Value *, int) { return 0; }

    extern const zen::CommandDecl bb3d_cmds_gfxmode[] = {
        {"%CountGfxDrivers", c_CountGfxDrivers},
        {"$GfxDriverName%driver", c_GfxDriverName},
        {"SetGfxDriver%driver", c_SetGfxDriver},
        {"%GfxDriver3D%driver", c_GfxDriver3D},
        {"%CountGfxModes", c_CountGfxModes},
        {"%GfxModeExists%width%height%depth", c_GfxModeExists},

        {"%ScanLine", c_ScanLine},
        {"%GraphicsLost", c_GraphicsLost},
        {"%TotalVidMem", c_TotalVidMem},
        {"%AvailVidMem", c_AvailVidMem},

        {"SetGamma%src_red%src_green%src_blue#dest_red#dest_green#dest_blue", c_SetGamma},
        {"UpdateGamma%calibrate=0", c_UpdateGamma},
        {"#GammaRed%red", c_GammaRed},
        {"#GammaGreen%green", c_GammaGreen},
        {"#GammaBlue%blue", c_GammaBlue},

        {"%OpenMovie$file", c_OpenMovie},
        {"%DrawMovie%movie%x=0%y=0%w=-1%h=-1", c_MovieZero},
        {"%MovieWidth%movie", c_MovieZero},
        {"%MovieHeight%movie", c_MovieZero},
        {"%MoviePlaying%movie", c_MovieZero},
        {"CloseMovie%movie", c_CloseMovie},
    };
    extern const int bb3d_cmds_gfxmode_count =
        (int)(sizeof(bb3d_cmds_gfxmode) / sizeof(bb3d_cmds_gfxmode[0]));
}
