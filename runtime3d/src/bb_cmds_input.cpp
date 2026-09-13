/*
** bb_cmds_input.cpp — the input commands bb_cmds_graphics.cpp does not
** already cover: MouseHit, MouseZ, the three MouseXSpeed/YSpeed/ZSpeed
** deltas, FlushMouse, MoveMouse, ShowPointer/HidePointer, and the
** joystick family.
**
** Ported from bbinput.cpp. The joystick commands are registered so real
** Blitz3D programs compile and run, and report "no joystick" until this
** runtime opens SDL's game controller subsystem - bbJoyType returning 0
** is exactly what Blitz3D reported for an empty port, and every program
** already has to handle it.
*/
#include "runtime.h"
#include "vm.h"
#include "object.h"
#include "engine/Platform.h"

namespace bb3d { extern engine::Platform *platform_for(zen::VM *vm); }

namespace
{
    inline long long arg_int(zen::Value v)
    {
        return zen::is_int(v) ? v.as.integer : zen::is_float(v) ? (long long)v.as.number : 0;
    }
}

using namespace zen;

namespace bb3d
{
    static int c_MouseHit(VM *vm, Value *args, int)
    {
        args[0] = val_int(platform_for(vm)->mouseHit((int)arg_int(args[0])));
        return 1;
    }

    static int c_MouseZ(VM *vm, Value *args, int)
    {
        args[0] = val_int(platform_for(vm)->mouseZ());
        return 1;
    }

    /* bbinput.cpp kept one previous value per axis and returned the
       difference, so the first call after a jump reports the whole move. */
    static int g_lastX = 0, g_lastY = 0, g_lastZ = 0;

    static int c_MouseXSpeed(VM *vm, Value *args, int)
    {
        const int x = platform_for(vm)->mouseX();
        args[0] = val_int(x - g_lastX);
        g_lastX = x;
        return 1;
    }

    static int c_MouseYSpeed(VM *vm, Value *args, int)
    {
        const int y = platform_for(vm)->mouseY();
        args[0] = val_int(y - g_lastY);
        g_lastY = y;
        return 1;
    }

    static int c_MouseZSpeed(VM *vm, Value *args, int)
    {
        const int z = platform_for(vm)->mouseZ();
        args[0] = val_int(z - g_lastZ);
        g_lastZ = z;
        return 1;
    }

    /* bbGetKey/bbGetMouse popped one entry off the device queue and
       returned 0 when it was empty. */
    static int c_GetKey(VM *vm, Value *args, int)
    {
        args[0] = val_int(platform_for(vm)->popKey());
        return 1;
    }

    static int c_GetMouse(VM *vm, Value *args, int)
    {
        args[0] = val_int(platform_for(vm)->popMouseButton());
        return 1;
    }

    /* DirectInput was a Windows input path Blitz3D could switch off; SDL
       is the only one here, so the switch is accepted and reports off. */
    static int c_EnableDirectInput(VM *, Value *, int) { return 0; }

    static int c_DirectInputEnabled(VM *vm, Value *args, int)
    {
        (void)vm;
        args[0] = val_int(0);
        return 1;
    }

    /* No joystick is opened, so waiting for one would hang forever;
       bbWaitJoy returned 0 immediately for an absent port. */
    static int c_WaitJoy(VM *vm, Value *args, int)
    {
        (void)vm;
        args[0] = val_int(0);
        return 1;
    }

    static int c_FlushMouse(VM *vm, Value *, int)
    {
        platform_for(vm)->flushMouseHits();
        return 0;
    }

    static int c_MoveMouse(VM *vm, Value *args, int)
    {
        const int x = (int)arg_int(args[0]), y = (int)arg_int(args[1]);
        /* bbMoveMouse updated the speed reference too, so warping the
           pointer does not read back as a huge MouseXSpeed next call -
           but only when the pointer actually moved */
        if (platform_for(vm)->moveMouse(x, y))
        {
            g_lastX = x;
            g_lastY = y;
        }
        return 0;
    }

    static int c_ShowPointer(VM *vm, Value *, int)
    {
        platform_for(vm)->showPointer(true);
        return 0;
    }

    static int c_HidePointer(VM *vm, Value *, int)
    {
        platform_for(vm)->showPointer(false);
        return 0;
    }

    /* No joystick is opened yet: every query answers the way Blitz3D
       answered for a port with nothing plugged into it. */
    static int c_JoyZero(VM *vm, Value *args, int)
    {
        (void)vm;
        args[0] = val_int(0);
        return 1;
    }

    static int c_JoyZeroFloat(VM *vm, Value *args, int)
    {
        (void)vm;
        args[0] = val_float(0.0);
        return 1;
    }

    static int c_FlushJoy(VM *, Value *, int) { return 0; }

    extern const zen::CommandDecl bb3d_cmds_input[] = {
        {"%MouseHit%button", c_MouseHit},
        {"%MouseZ", c_MouseZ},
        {"%MouseXSpeed", c_MouseXSpeed},
        {"%MouseYSpeed", c_MouseYSpeed},
        {"%MouseZSpeed", c_MouseZSpeed},
        {"FlushMouse", c_FlushMouse},
        {"MoveMouse%x%y", c_MoveMouse},
        {"ShowPointer", c_ShowPointer},
        {"HidePointer", c_HidePointer},

        {"%JoyType%port=0", c_JoyZero},
        {"%JoyDown%button%port=0", c_JoyZero},
        {"%JoyHit%button%port=0", c_JoyZero},
        {"%GetJoy%port=0", c_JoyZero},
        {"%JoyHat%port=0", c_JoyZero},
        {"%JoyXDir%port=0", c_JoyZero},
        {"%JoyYDir%port=0", c_JoyZero},
        {"%JoyZDir%port=0", c_JoyZero},
        {"%JoyUDir%port=0", c_JoyZero},
        {"%JoyVDir%port=0", c_JoyZero},
        {"#JoyX%port=0", c_JoyZeroFloat},
        {"#JoyY%port=0", c_JoyZeroFloat},
        {"#JoyZ%port=0", c_JoyZeroFloat},
        {"#JoyU%port=0", c_JoyZeroFloat},
        {"#JoyV%port=0", c_JoyZeroFloat},
        {"#JoyPitch%port=0", c_JoyZeroFloat},
        {"#JoyYaw%port=0", c_JoyZeroFloat},
        {"#JoyRoll%port=0", c_JoyZeroFloat},
        {"FlushJoy", c_FlushJoy},
        {"%WaitJoy%port=0", c_WaitJoy},
        {"%JoyWait%port=0", c_WaitJoy},

        {"%GetKey", c_GetKey},
        {"%GetMouse", c_GetMouse},
        {"EnableDirectInput%enable", c_EnableDirectInput},
        {"%DirectInputEnabled", c_DirectInputEnabled},
    };
    extern const int bb3d_cmds_input_count =
        (int)(sizeof(bb3d_cmds_input) / sizeof(bb3d_cmds_input[0]));
}
