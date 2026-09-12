/* =========================================================
** builtin_fs.cpp — "fs" module for Zen
**
** Provides: read, write, append, remove, mkdir, rmdir, list, stat, exists
** Pure C, no STL. Cross-platform (Linux/macOS/Windows).
** ========================================================= */

#include "module.h"
#include "vm.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define zen_mkdir(p) _mkdir(p)
#define zen_rmdir(p) _rmdir(p)
#else
#include <unistd.h>
#include <dirent.h>
#define zen_mkdir(p) mkdir(p, 0755)
#define zen_rmdir(p) rmdir(p)
#endif

namespace zen
{

    /* =========================================================
    ** Native functions
    ** ========================================================= */

    /* read(path) → string or nil */
    static int nat_read(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_obj(args[0]))
        {
            args[0] = val_nil();
            return 1;
        }
        ObjString *path = as_string(args[0]);
        FILE *f = fopen(path->chars, "rb");
        if (!f)
        {
            args[0] = val_nil();
            return 1;
        }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (size <= 0)
        {
            fclose(f);
            args[0] = val_obj((Obj *)vm->make_string("", 0));
            return 1;
        }
        char *buf = (char *)malloc((size_t)size);
        if (!buf)
        {
            fclose(f);
            args[0] = val_nil();
            return 1;
        }
        size_t read = fread(buf, 1, (size_t)size, f);
        fclose(f);
        args[0] = val_obj((Obj *)vm->make_string(buf, (int)read));
        free(buf);
        return 1;
    }

    /* write(path, data) → bool */
    static int nat_write(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_obj(args[0]) || !is_obj(args[1]))
        {
            args[0] = val_bool(false);
            return 1;
        }
        ObjString *path = as_string(args[0]);
        ObjString *data = as_string(args[1]);
        FILE *f = fopen(path->chars, "wb");
        if (!f)
        {
            args[0] = val_bool(false);
            return 1;
        }
        size_t written = fwrite(data->chars, 1, (size_t)data->length, f);
        fclose(f);
        args[0] = val_bool(written == (size_t)data->length);
        return 1;
    }

    /* append(path, data) → bool */
    static int nat_append(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_obj(args[0]) || !is_obj(args[1]))
        {
            args[0] = val_bool(false);
            return 1;
        }
        ObjString *path = as_string(args[0]);
        ObjString *data = as_string(args[1]);
        FILE *f = fopen(path->chars, "ab");
        if (!f)
        {
            args[0] = val_bool(false);
            return 1;
        }
        size_t written = fwrite(data->chars, 1, (size_t)data->length, f);
        fclose(f);
        args[0] = val_bool(written == (size_t)data->length);
        return 1;
    }

    /* exists(path) → bool */
    static int nat_exists(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_obj(args[0]))
        {
            args[0] = val_bool(false);
            return 1;
        }
        ObjString *path = as_string(args[0]);
        struct stat st;
        args[0] = val_bool(stat(path->chars, &st) == 0);
        return 1;
    }

    /* remove(path) → bool */
    static int nat_remove(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_obj(args[0]))
        {
            args[0] = val_bool(false);
            return 1;
        }
        ObjString *path = as_string(args[0]);
        args[0] = val_bool(::remove(path->chars) == 0);
        return 1;
    }

    /* mkdir(path) → bool */
    static int nat_mkdir(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_obj(args[0]))
        {
            args[0] = val_bool(false);
            return 1;
        }
        ObjString *path = as_string(args[0]);
        args[0] = val_bool(zen_mkdir(path->chars) == 0);
        return 1;
    }

    /* rmdir(path) → bool */
    static int nat_rmdir(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_obj(args[0]))
        {
            args[0] = val_bool(false);
            return 1;
        }
        ObjString *path = as_string(args[0]);
        args[0] = val_bool(zen_rmdir(path->chars) == 0);
        return 1;
    }

    /* size(path) → int or -1 */
    static int nat_size(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_obj(args[0]))
        {
            args[0] = val_int(-1);
            return 1;
        }
        ObjString *path = as_string(args[0]);
        struct stat st;
        if (stat(path->chars, &st) == 0)
            args[0] = val_int((int64_t)st.st_size);
        else
            args[0] = val_int(-1);
        return 1;
    }

    /* isdir(path) → bool */
    static int nat_isdir(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_obj(args[0]))
        {
            args[0] = val_bool(false);
            return 1;
        }
        ObjString *path = as_string(args[0]);
        struct stat st;
        args[0] = val_bool(stat(path->chars, &st) == 0 && S_ISDIR(st.st_mode));
        return 1;
    }

    /* isfile(path) → bool */
    static int nat_isfile(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_obj(args[0]))
        {
            args[0] = val_bool(false);
            return 1;
        }
        ObjString *path = as_string(args[0]);
        struct stat st;
        args[0] = val_bool(stat(path->chars, &st) == 0 && S_ISREG(st.st_mode));
        return 1;
    }

    /* =========================================================
    ** Module definition
    ** ========================================================= */

    static const NativeReg fs_functions[] = {
        {"read", nat_read, 1},
        {"write", nat_write, 2},
        {"append", nat_append, 2},
        {"exists", nat_exists, 1},
        {"remove", nat_remove, 1},
        {"mkdir", nat_mkdir, 1},
        {"rmdir", nat_rmdir, 1},
        {"size", nat_size, 1},
        {"isdir", nat_isdir, 1},
        {"isfile", nat_isfile, 1},
        {nullptr, nullptr, 0}};

    const NativeLib zen_lib_fs = {
        "fs",
        fs_functions,
        10,
        nullptr,
        0};

} /* namespace zen */
