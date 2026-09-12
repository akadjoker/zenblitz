/* =========================================================
** builtin_os.cpp — "os" module for Zen
**
** Provides: execute, getenv, setenv, getcwd, chdir, exit
** Platform constant: platform
** ========================================================= */

#include "module.h"
#include "vm.h"
#include "memory.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

#ifdef _WIN32
#include <direct.h>
#define zen_getcwd _getcwd
#define zen_chdir _chdir
#else
#include <unistd.h>
#include <sys/wait.h>
#define zen_getcwd getcwd
#define zen_chdir chdir
#endif

namespace zen
{

    /* =========================================================
    ** Native functions
    ** ========================================================= */

    /* execute(cmd) → exit code (int) */
    static int nat_execute(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_obj(args[0]))
        {
            args[0] = val_int(-1);
            return 1;
        }
        ObjString *cmd = as_string(args[0]);
        int result = system(cmd->chars);
        args[0] = val_int(result);
        return 1;
    }

    /* getenv(name) → string or nil */
    static int nat_getenv(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_obj(args[0]))
        {
            args[0] = val_nil();
            return 1;
        }
        ObjString *name = as_string(args[0]);
        const char *val = getenv(name->chars);
        if (val)
            args[0] = val_obj((Obj*)vm->make_string(val));
        else
            args[0] = val_nil();
        return 1;
    }

    /* setenv(name, value) → bool */
    static int nat_setenv(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_obj(args[0]) || !is_obj(args[1]))
        {
            args[0] = val_bool(false);
            return 1;
        }
        ObjString *name = as_string(args[0]);
        ObjString *val = as_string(args[1]);
#ifdef _WIN32
        char buf[512];
        snprintf(buf, sizeof(buf), "%s=%s", name->chars, val->chars);
        args[0] = val_bool(_putenv(buf) == 0);
#else
        args[0] = val_bool(setenv(name->chars, val->chars, 1) == 0);
#endif
        return 1;
    }

    /* getcwd() → string */
    static int nat_getcwd(VM *vm, Value *args, int nargs)
    {
        char buf[4096];
        if (zen_getcwd(buf, sizeof(buf)))
            args[0] = val_obj((Obj*)vm->make_string(buf));
        else
            args[0] = val_nil();
        return 1;
    }

    /* chdir(path) → bool */
    static int nat_chdir(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_obj(args[0]))
        {
            args[0] = val_bool(false);
            return 1;
        }
        ObjString *path = as_string(args[0]);
        args[0] = val_bool(zen_chdir(path->chars) == 0);
        return 1;
    }

    /* exit(code?) — terminates process */
    static int nat_exit(VM *vm, Value *args, int nargs)
    {
        int code = (nargs >= 1 && is_int(args[0])) ? (int)to_integer(args[0]) : 0;
        exit(code);
        return 0;
    }

    /* capture(cmd) → string (stdout of command) or nil on failure
    ** Like Python's subprocess.check_output() */
    static int nat_capture(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_obj(args[0]))
        {
            args[0] = val_nil();
            return 1;
        }
        ObjString *cmd = as_string(args[0]);
#ifdef _WIN32
        FILE *pipe = _popen(cmd->chars, "r");
#else
        FILE *pipe = popen(cmd->chars, "r");
#endif
        if (!pipe)
        {
            args[0] = val_nil();
            return 1;
        }

        /* Read all output into dynamic buffer */
        char *buf = nullptr;
        size_t cap = 0, len = 0;
        char chunk[1024];
        size_t n;
        while ((n = fread(chunk, 1, sizeof(chunk), pipe)) > 0)
        {
            if (len + n > cap)
            {
                cap = (len + n) * 2;
                if (cap < 4096) cap = 4096;
                buf = (char *)realloc(buf, cap);
            }
            memcpy(buf + len, chunk, n);
            len += n;
        }

#ifdef _WIN32
        int status = _pclose(pipe);
#else
        int status = pclose(pipe);
#endif
        /* Strip trailing newline if present */
        if (len > 0 && buf[len - 1] == '\n')
            len--;

        if (buf)
        {
            args[0] = val_obj((Obj *)vm->make_string(buf, (int)len));
            free(buf);
        }
        else
            args[0] = val_obj((Obj *)vm->make_string("", 0));

        (void)status;
        return 1;
    }

    /* run(cmd) → map {stdout: string, code: int}
    ** Like Python's subprocess.run(cmd, capture_output=True) */
    static int nat_run(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_obj(args[0]))
        {
            args[0] = val_nil();
            return 1;
        }
        ObjString *cmd = as_string(args[0]);
#ifdef _WIN32
        FILE *pipe = _popen(cmd->chars, "r");
#else
        FILE *pipe = popen(cmd->chars, "r");
#endif
        if (!pipe)
        {
            args[0] = val_nil();
            return 1;
        }

        char *buf = nullptr;
        size_t cap = 0, len = 0;
        char chunk[1024];
        size_t n;
        while ((n = fread(chunk, 1, sizeof(chunk), pipe)) > 0)
        {
            if (len + n > cap)
            {
                cap = (len + n) * 2;
                if (cap < 4096) cap = 4096;
                buf = (char *)realloc(buf, cap);
            }
            memcpy(buf + len, chunk, n);
            len += n;
        }

#ifdef _WIN32
        int status = _pclose(pipe);
#else
        int status = pclose(pipe);
        /* POSIX: extract exit code */
        if (WIFEXITED(status))
            status = WEXITSTATUS(status);
#endif

        /* Build result map {stdout: ..., code: ...} */
        ObjMap *map = new_map(&vm->get_gc());
        ObjString *key_out = vm->make_string("stdout");
        ObjString *key_code = vm->make_string("code");

        Value out_val;
        if (buf)
        {
            /* Strip trailing newline */
            if (len > 0 && buf[len - 1] == '\n')
                len--;
            out_val = val_obj((Obj *)vm->make_string(buf, (int)len));
            free(buf);
        }
        else
            out_val = val_obj((Obj *)vm->make_string("", 0));

        map_set(&vm->get_gc(), map, val_obj((Obj *)key_out), out_val);
        map_set(&vm->get_gc(), map, val_obj((Obj *)key_code), val_int(status));
        args[0] = val_obj((Obj *)map);
        return 1;
    }

    /* =========================================================
    ** Module definition
    ** ========================================================= */

    /* (os_functions_full below supersedes this) */

    /* Platform constant */
#if defined(__EMSCRIPTEN__)
#define ZEN_PLATFORM "emscripten"
#elif defined(_WIN32)
#define ZEN_PLATFORM "windows"
#elif defined(__APPLE__)
#define ZEN_PLATFORM "macos"
#elif defined(__linux__)
#define ZEN_PLATFORM "linux"
#else
#define ZEN_PLATFORM "unknown"
#endif

    /* We can't easily put a string constant in static init without GC.
       Use a sentinel — the VM will handle "platform" specially,
       or we just don't include string constants for now.
       For now: no constants (platform will be a function). */

    /* platform() → string */
    static int nat_platform(VM *vm, Value *args, int nargs)
    {
        const char *p = ZEN_PLATFORM;
        args[0] = val_obj((Obj*)vm->make_string(p));
        return 1;
    }

    /* Re-define functions to include platform */
    static const NativeReg os_functions_full[] = {
        {"execute", nat_execute, 1},
        {"capture", nat_capture, 1},
        {"run", nat_run, 1},
        {"getenv", nat_getenv, 1},
        {"setenv", nat_setenv, 2},
        {"getcwd", nat_getcwd, 0},
        {"chdir", nat_chdir, 1},
        {"exit", nat_exit, -1},
        {"platform", nat_platform, 0},
        {nullptr, nullptr, 0}};

    const NativeLib zen_lib_os = {
        "os",
        os_functions_full,
        9,
        nullptr,
        0,
        nullptr
    };

} /* namespace zen */
