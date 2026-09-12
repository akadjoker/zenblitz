/* =========================================================
** builtin_path.cpp — "path" module for Zen
**
** Provides: join, dirname, basename, filename, ext, normalize
** ========================================================= */

#include "module.h"
#include "vm.h"
#include <cstring>
#include <cstdio>

namespace zen
{

    static inline bool is_sep(char c)
    {
#ifdef _WIN32
        return c == '/' || c == '\\';
#else
        return c == '/';
#endif
    }

    /* join(a, b, ...) → path string (variadic) */
    static int nat_join(VM *vm, Value *args, int nargs)
    {
        char buf[4096];
        int pos = 0;

        for (int i = 0; i < nargs; i++)
        {
            if (!is_obj(args[i]))
                continue;
            ObjString *s = as_string(args[i]);
            if (s->length == 0)
                continue;

            if (pos > 0 && !is_sep(buf[pos - 1]))
                buf[pos++] = '/';

            int copy = s->length;
            if (pos + copy >= 4095)
                copy = 4095 - pos;
            memcpy(buf + pos, s->chars, copy);
            pos += copy;
        }
        buf[pos] = '\0';
        args[0] = val_obj((Obj *)vm->make_string(buf, pos));
        return 1;
    }

    /* dirname(path) → directory part */
    static int nat_dirname(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_obj(args[0]))
        {
            args[0] = val_obj((Obj *)vm->make_string(".", 1));
            return 1;
        }
        ObjString *path = as_string(args[0]);
        int last = -1;
        for (int i = path->length - 1; i >= 0; i--)
        {
            if (is_sep(path->chars[i]))
            {
                last = i;
                break;
            }
        }
        if (last < 0)
            args[0] = val_obj((Obj *)vm->make_string(".", 1));
        else if (last == 0)
            args[0] = val_obj((Obj *)vm->make_string("/", 1));
        else
            args[0] = val_obj((Obj *)vm->make_string(path->chars, last));
        return 1;
    }

    /* basename(path) → filename with extension */
    static int nat_basename(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_obj(args[0]))
        {
            args[0] = val_obj((Obj *)vm->make_string("", 0));
            return 1;
        }
        ObjString *path = as_string(args[0]);
        int start = 0;
        for (int i = path->length - 1; i >= 0; i--)
        {
            if (is_sep(path->chars[i]))
            {
                start = i + 1;
                break;
            }
        }
        args[0] = val_obj((Obj *)vm->make_string(path->chars + start, path->length - start));
        return 1;
    }

    /* filename(path) → filename without extension (stem) */
    static int nat_filename(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_obj(args[0]))
        {
            args[0] = val_obj((Obj *)vm->make_string("", 0));
            return 1;
        }
        ObjString *path = as_string(args[0]);
        /* Find last separator */
        int start = 0;
        for (int i = path->length - 1; i >= 0; i--)
        {
            if (is_sep(path->chars[i]))
            {
                start = i + 1;
                break;
            }
        }
        /* Find last dot after separator */
        int dot = -1;
        for (int i = path->length - 1; i >= start; i--)
        {
            if (path->chars[i] == '.')
            {
                dot = i;
                break;
            }
        }
        int end = (dot > start) ? dot : path->length;
        args[0] = val_obj((Obj *)vm->make_string(path->chars + start, end - start));
        return 1;
    }

    /* ext(path) → extension including dot, or "" */
    static int nat_ext(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_obj(args[0]))
        {
            args[0] = val_obj((Obj *)vm->make_string("", 0));
            return 1;
        }
        ObjString *path = as_string(args[0]);
        /* Find last separator */
        int sep = -1;
        for (int i = path->length - 1; i >= 0; i--)
        {
            if (is_sep(path->chars[i]))
            {
                sep = i;
                break;
            }
        }
        /* Find last dot after separator */
        int dot = -1;
        for (int i = path->length - 1; i > sep; i--)
        {
            if (path->chars[i] == '.')
            {
                dot = i;
                break;
            }
        }
        if (dot < 0)
            args[0] = val_obj((Obj *)vm->make_string("", 0));
        else
            args[0] = val_obj((Obj *)vm->make_string(path->chars + dot, path->length - dot));
        return 1;
    }

    /* normalize(path) → resolved . and .. */
    static int nat_normalize(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_obj(args[0]))
        {
            args[0] = val_obj((Obj *)vm->make_string(".", 1));
            return 1;
        }
        ObjString *path = as_string(args[0]);

        /* Simple stack-based normalize */
        const char *parts[128];
        int lens[128];
        int nparts = 0;
        bool absolute = (path->length > 0 && is_sep(path->chars[0]));

        int i = 0;
        while (i < path->length)
        {
            /* Skip separators */
            while (i < path->length && is_sep(path->chars[i]))
                i++;
            if (i >= path->length)
                break;
            int start = i;
            while (i < path->length && !is_sep(path->chars[i]))
                i++;
            int len = i - start;

            if (len == 1 && path->chars[start] == '.')
                continue; /* skip . */
            if (len == 2 && path->chars[start] == '.' && path->chars[start + 1] == '.')
            {
                if (nparts > 0)
                    nparts--;
                continue;
            }
            parts[nparts] = path->chars + start;
            lens[nparts] = len;
            nparts++;
            if (nparts >= 128)
                break;
        }

        /* Rebuild */
        char buf[4096];
        int pos = 0;
        if (absolute)
            buf[pos++] = '/';
        for (int p = 0; p < nparts; p++)
        {
            if (p > 0)
                buf[pos++] = '/';
            memcpy(buf + pos, parts[p], lens[p]);
            pos += lens[p];
        }
        if (pos == 0)
        {
            buf[0] = '.';
            pos = 1;
        }
        buf[pos] = '\0';
        args[0] = val_obj((Obj *)vm->make_string(buf, pos));
        return 1;
    }

    /* =========================================================
    ** Module definition
    ** ========================================================= */

    static const NativeReg path_functions[] = {
        {"join", nat_join, -1},
        {"dirname", nat_dirname, 1},
        {"basename", nat_basename, 1},
        {"filename", nat_filename, 1},
        {"ext", nat_ext, 1},
        {"normalize", nat_normalize, 1},
        {nullptr, nullptr, 0}};

    const NativeLib zen_lib_path = {
        "path",
        path_functions,
        6,
        nullptr,
        0,
        nullptr
    };

} /* namespace zen */
