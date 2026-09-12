/* =========================================================
** builtin_base.cpp — Global builtin functions for Zen
**
** Always available (no import needed):
**   str(val)         → convert to string
**   int(val)         → convert to integer
**   float(val)       → convert to float
**   char(code)       → codepoint → 1-char string
**   ord(str)         → first char → codepoint int
**   typeof(val)      → type name as string
**   input(prompt?)   → read line from stdin
**   assert(cond,msg?)→ runtime error if !cond
**   error(msg)       → raise runtime error
**   range(a,b?,step?)→ array [a..b) with step
** ========================================================= */

#include "module.h"
#include "vm.h"
#include "memory.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

namespace zen
{

    /* =========================================================
    ** str(val) → string
    ** ========================================================= */
    static int nat_str(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1)
        {
            args[0] = val_obj((Obj *)vm->make_string(""));
            return 1;
        }
        Value v = args[0];
        if (is_string(v))
        {
            args[0] = v;
            return 1;
        }
        /* Instance with a __str__ overload — use it (like print / concat / interp). */
        if (is_instance(v))
        {
            ObjClass *k = ((ObjInstance *)v.as.obj)->klass;
            if ((int)VM::SLOT_STR < kOperatorSlotCount &&
                !is_nil(k->operator_slots[VM::SLOT_STR]))
            {
                Value r = vm->invoke_operator(v, VM::SLOT_STR, nullptr, 0);
                if (is_string(r))
                {
                    args[0] = r;
                    return 1;
                }
            }
        }
        char buf[64];
        int len = 0;
        if (is_nil(v))
            len = snprintf(buf, sizeof(buf), "nil");
        else if (is_bool(v))
            len = snprintf(buf, sizeof(buf), "%s", v.as.boolean ? "true" : "false");
        else if (is_int(v))
            len = snprintf(buf, sizeof(buf), "%lld", (long long)v.as.integer);
        else if (is_float(v))
            len = snprintf(buf, sizeof(buf), "%g", v.as.number);
        else if (is_array(v))
            len = snprintf(buf, sizeof(buf), "<array>");
        else if (is_map(v))
            len = snprintf(buf, sizeof(buf), "<map>");
        else
            len = snprintf(buf, sizeof(buf), "<object>");

        args[0] = val_obj((Obj *)vm->make_string(buf, len));
        return 1;
    }

    /* =========================================================
    ** int(val) → integer
    ** ========================================================= */
    static int nat_int(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1)
        {
            args[0] = val_int(0);
            return 1;
        }
        Value v = args[0];
        if (is_int(v))
        {
            args[0] = v;
            return 1;
        }
        if (is_float(v))
        {
            args[0] = val_int((int64_t)v.as.number);
            return 1;
        }
        if (is_bool(v))
        {
            args[0] = val_int(v.as.boolean ? 1 : 0);
            return 1;
        }
        if (is_string(v))
        {
            ObjString *s = as_string(v);
            char *end;
            int64_t n = strtoll(s->chars, &end, 10);
            if (end == s->chars)
            {
                vm->runtime_error("int(): cannot convert '%s' to int.", s->chars);
                return -1;
            }
            args[0] = val_int(n);
            return 1;
        }
        if (is_nil(v))
        {
            args[0] = val_int(0);
            return 1;
        }
        vm->runtime_error("int(): unsupported type.");
        return -1;
    }

    /* =========================================================
    ** float(val) → float
    ** ========================================================= */
    static int nat_float(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1)
        {
            args[0] = val_float(0.0);
            return 1;
        }
        Value v = args[0];
        if (is_float(v))
        {
            args[0] = v;
            return 1;
        }
        if (is_int(v))
        {
            args[0] = val_float((double)v.as.integer);
            return 1;
        }
        if (is_bool(v))
        {
            args[0] = val_float(v.as.boolean ? 1.0 : 0.0);
            return 1;
        }
        if (is_string(v))
        {
            ObjString *s = as_string(v);
            char *end;
            double d = strtod(s->chars, &end);
            if (end == s->chars)
            {
                vm->runtime_error("float(): cannot convert '%s' to float.", s->chars);
                return -1;
            }
            args[0] = val_float(d);
            return 1;
        }
        if (is_nil(v))
        {
            args[0] = val_float(0.0);
            return 1;
        }
        vm->runtime_error("float(): unsupported type.");
        return -1;
    }

    /* =========================================================
    ** char(code) → string (1 UTF-8 char from codepoint)
    ** ========================================================= */
    static int nat_char(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1)
        {
            vm->runtime_error("char() expects 1 argument.");
            return -1;
        }
        Value v = args[0];
        int64_t cp;
        if (is_int(v))
            cp = v.as.integer;
        else if (is_float(v))
            cp = (int64_t)v.as.number;
        else
        {
            vm->runtime_error("char() expects an integer codepoint.");
            return -1;
        }

        /* Encode UTF-8 */
        char buf[5];
        int len = 0;
        if (cp < 0x80)
        {
            buf[0] = (char)cp;
            len = 1;
        }
        else if (cp < 0x800)
        {
            buf[0] = (char)(0xC0 | (cp >> 6));
            buf[1] = (char)(0x80 | (cp & 0x3F));
            len = 2;
        }
        else if (cp < 0x10000)
        {
            buf[0] = (char)(0xE0 | (cp >> 12));
            buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
            buf[2] = (char)(0x80 | (cp & 0x3F));
            len = 3;
        }
        else if (cp < 0x110000)
        {
            buf[0] = (char)(0xF0 | (cp >> 18));
            buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
            buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
            buf[3] = (char)(0x80 | (cp & 0x3F));
            len = 4;
        }
        else
        {
            vm->runtime_error("char(): invalid codepoint %lld.", (long long)cp);
            return -1;
        }
        buf[len] = '\0';
        args[0] = val_obj((Obj *)vm->make_string(buf, len));
        return 1;
    }

    /* =========================================================
    ** ord(str) → int (codepoint of first char)
    ** ========================================================= */
    static int nat_ord(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_string(args[0]))
        {
            vm->runtime_error("ord() expects a string argument.");
            return -1;
        }
        ObjString *s = as_string(args[0]);
        if (s->length == 0)
        {
            vm->runtime_error("ord(): empty string.");
            return -1;
        }
        /* Decode first UTF-8 char */
        const uint8_t *p = (const uint8_t *)s->chars;
        int64_t cp;
        if (p[0] < 0x80)
            cp = p[0];
        else if ((p[0] & 0xE0) == 0xC0)
            cp = ((int64_t)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        else if ((p[0] & 0xF0) == 0xE0)
            cp = ((int64_t)(p[0] & 0x0F) << 12) | ((int64_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        else if ((p[0] & 0xF8) == 0xF0)
            cp = ((int64_t)(p[0] & 0x07) << 18) | ((int64_t)(p[1] & 0x3F) << 12) |
                 ((int64_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        else
            cp = p[0]; /* fallback */

        args[0] = val_int(cp);
        return 1;
    }

    /* =========================================================
    ** Type check builtins — direct tag comparison, no string alloc
    ** ========================================================= */
    static int nat_isNil(VM *vm, Value *args, int nargs)
    { args[0] = val_bool(nargs >= 1 && is_nil(args[0])); return 1; }

    static int nat_isBool(VM *vm, Value *args, int nargs)
    { args[0] = val_bool(nargs >= 1 && is_bool(args[0])); return 1; }

    static int nat_isInt(VM *vm, Value *args, int nargs)
    { args[0] = val_bool(nargs >= 1 && is_int(args[0])); return 1; }

    static int nat_isFloat(VM *vm, Value *args, int nargs)
    { args[0] = val_bool(nargs >= 1 && is_float(args[0])); return 1; }

    static int nat_isNumber(VM *vm, Value *args, int nargs)
    { args[0] = val_bool(nargs >= 1 && (is_int(args[0]) || is_float(args[0]))); return 1; }

    static int nat_isString(VM *vm, Value *args, int nargs)
    { args[0] = val_bool(nargs >= 1 && is_string(args[0])); return 1; }

    static int nat_isArray(VM *vm, Value *args, int nargs)
    { args[0] = val_bool(nargs >= 1 && is_array(args[0])); return 1; }

    static int nat_isMap(VM *vm, Value *args, int nargs)
    { args[0] = val_bool(nargs >= 1 && is_map(args[0])); return 1; }

    static int nat_isFunction(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1) { args[0] = val_bool(false); return 1; }
        Value v = args[0];
        bool r = is_obj(v) && (v.as.obj->type == OBJ_FUNC ||
                               v.as.obj->type == OBJ_CLOSURE ||
                               v.as.obj->type == OBJ_NATIVE);
        args[0] = val_bool(r);
        return 1;
    }

    /* =========================================================
    ** typeof(val) → string ("nil","bool","int","float","string",
    **                        "array","map","function","class","instance")
    ** ========================================================= */
    static int nat_typeof(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1)
        {
            args[0] = val_obj((Obj *)vm->make_string("nil"));
            return 1;
        }
        Value v = args[0];
        const char *name;
        switch (v.type)
        {
        case VAL_NIL:
            name = "nil";
            break;
        case VAL_BOOL:
            name = "bool";
            break;
        case VAL_INT:
            name = "int";
            break;
        case VAL_FLOAT:
            name = "float";
            break;
        case VAL_OBJ:
        {
            switch (v.as.obj->type)
            {
            case OBJ_STRING:
                name = "string";
                break;
            case OBJ_ARRAY:
                name = "array";
                break;
            case OBJ_MAP:
                name = "map";
                break;
            case OBJ_FUNC:
            case OBJ_CLOSURE:
            case OBJ_NATIVE:
                name = "function";
                break;
            case OBJ_CLASS:
                name = "class";
                break;
            case OBJ_INSTANCE:
                name = "instance";
                break;
            default:
                name = "object";
                break;
            }
            break;
        }
        default:
            name = "unknown";
            break;
        }
        args[0] = val_obj((Obj *)vm->make_string(name));
        return 1;
    }

    /* =========================================================
    ** input(prompt?) → string (reads line from stdin)
    ** ========================================================= */
    static int nat_input(VM *vm, Value *args, int nargs)
    {
        /* Print prompt if given */
        if (nargs >= 1 && is_string(args[0]))
        {
            ObjString *prompt = as_string(args[0]);
            fwrite(prompt->chars, 1, prompt->length, stdout);
            fflush(stdout);
        }

        /* Read line */
        char buf[4096];
        if (!fgets(buf, sizeof(buf), stdin))
        {
            args[0] = val_nil();
            return 1;
        }
        /* Strip trailing newline */
        int len = (int)strlen(buf);
        if (len > 0 && buf[len - 1] == '\n')
            len--;
        if (len > 0 && buf[len - 1] == '\r')
            len--;

        args[0] = val_obj((Obj *)vm->make_string(buf, len));
        return 1;
    }

    /* =========================================================
    ** assert(cond, msg?) → nil or runtime error
    ** ========================================================= */
    static int nat_assert(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1)
        {
            vm->runtime_error("assert() expects at least 1 argument.");
            return -1;
        }
        if (!is_truthy(args[0]))
        {
            if (nargs >= 2 && is_string(args[1]))
                vm->runtime_error("assertion failed: %s", as_string(args[1])->chars);
            else
                vm->runtime_error("assertion failed.");
            return -1;
        }
        args[0] = val_nil();
        return 1;
    }

    /* =========================================================
    ** error(msg) → raises runtime error (never returns normally)
    ** ========================================================= */
    static int nat_error(VM *vm, Value *args, int nargs)
    {
        if (nargs >= 1 && is_string(args[0]))
            vm->runtime_error("%s", as_string(args[0])->chars);
        else
            vm->runtime_error("error() called.");
        return -1;
    }

    /* =========================================================
    ** range(stop) or range(start, stop) or range(start, stop, step)
    ** → array of integers
    ** ========================================================= */
    static int nat_range(VM *vm, Value *args, int nargs)
    {
        int64_t start = 0, stop = 0, step = 1;

        if (nargs == 1)
        {
            stop = to_integer(args[0]);
        }
        else if (nargs == 2)
        {
            start = to_integer(args[0]);
            stop = to_integer(args[1]);
        }
        else if (nargs >= 3)
        {
            start = to_integer(args[0]);
            stop = to_integer(args[1]);
            step = to_integer(args[2]);
        }
        else
        {
            vm->runtime_error("range() expects 1-3 arguments.");
            return -1;
        }

        if (step == 0)
        {
            vm->runtime_error("range(): step cannot be 0.");
            return -1;
        }

        /* Sanity limit to prevent huge allocations */
        int64_t count;
        if (step > 0)
            count = (stop > start) ? (stop - start + step - 1) / step : 0;
        else
            count = (start > stop) ? (start - stop + (-step) - 1) / (-step) : 0;

        if (count > 1000000)
        {
            vm->runtime_error("range(): too many elements (%lld).", (long long)count);
            return -1;
        }

        ObjArray *arr = new_array(&vm->get_gc());
        if (step > 0)
        {
            for (int64_t i = start; i < stop; i += step)
                array_push(&vm->get_gc(), arr, val_int(i));
        }
        else
        {
            for (int64_t i = start; i > stop; i += step)
                array_push(&vm->get_gc(), arr, val_int(i));
        }

        args[0] = val_obj((Obj *)arr);
        return 1;
    }

    /* =========================================================
    ** format(fmt, ...) → string
    **
    ** C-style format: %d %i %u %x %X %o %f %e %g %s %c %%
    ** Width/precision: %10d %-8s %.2f %04x
    ** ========================================================= */
    static int nat_format(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_string(args[0]))
        {
            vm->runtime_error("format() expects (fmt_string, ...).");
            return -1;
        }

        ObjString *fmt = as_string(args[0]);
        const char *p = fmt->chars;
        const char *end = p + fmt->length;
        int arg_idx = 1; /* next arg to consume */

        /* Output buffer */
        int cap = 256;
        int len = 0;
        char *out = (char *)malloc((size_t)cap);

        auto grow = [&](int need) {
            if (len + need > cap)
            {
                while (cap < len + need)
                    cap *= 2;
                out = (char *)realloc(out, (size_t)cap);
            }
        };

        while (p < end)
        {
            if (*p != '%')
            {
                grow(1);
                out[len++] = *p++;
                continue;
            }
            p++; /* skip '%' */
            if (p >= end)
                break;

            /* %% → literal % */
            if (*p == '%')
            {
                grow(1);
                out[len++] = '%';
                p++;
                continue;
            }

            /* Parse flags: -, +, 0, space, # */
            char spec[32];
            int si = 0;
            spec[si++] = '%';

            while (p < end && (*p == '-' || *p == '+' || *p == '0' || *p == ' ' || *p == '#'))
            {
                if (si < 28)
                    spec[si++] = *p;
                p++;
            }
            /* Width */
            while (p < end && *p >= '0' && *p <= '9')
            {
                if (si < 28)
                    spec[si++] = *p;
                p++;
            }
            /* Precision */
            if (p < end && *p == '.')
            {
                if (si < 28)
                    spec[si++] = *p;
                p++;
                while (p < end && *p >= '0' && *p <= '9')
                {
                    if (si < 28)
                        spec[si++] = *p;
                    p++;
                }
            }

            if (p >= end)
                break;

            char conv = *p++;
            spec[si++] = conv;
            spec[si] = '\0';

            char tmp[128];
            int n = 0;

            switch (conv)
            {
            case 'd':
            case 'i':
            {
                if (arg_idx >= nargs)
                {
                    free(out);
                    vm->runtime_error("format(): not enough arguments for %%d.");
                    return -1;
                }
                /* Replace 'd'/'i' with lld for int64_t */
                spec[si - 1] = '\0';
                char full[36];
                snprintf(full, sizeof(full), "%slld", spec);
                int64_t val = is_int(args[arg_idx]) ? args[arg_idx].as.integer
                            : is_float(args[arg_idx]) ? (int64_t)args[arg_idx].as.number
                            : 0;
                n = snprintf(tmp, sizeof(tmp), full, (long long)val);
                arg_idx++;
                break;
            }
            case 'u':
            {
                if (arg_idx >= nargs)
                {
                    free(out);
                    vm->runtime_error("format(): not enough arguments for %%u.");
                    return -1;
                }
                spec[si - 1] = '\0';
                char full[36];
                snprintf(full, sizeof(full), "%sllu", spec);
                uint64_t val = is_int(args[arg_idx]) ? (uint64_t)args[arg_idx].as.integer
                             : is_float(args[arg_idx]) ? (uint64_t)args[arg_idx].as.number
                             : 0;
                n = snprintf(tmp, sizeof(tmp), full, (unsigned long long)val);
                arg_idx++;
                break;
            }
            case 'x':
            case 'X':
            case 'o':
            {
                if (arg_idx >= nargs)
                {
                    free(out);
                    vm->runtime_error("format(): not enough arguments for %%%c.", conv);
                    return -1;
                }
                spec[si - 1] = '\0';
                char full[36];
                snprintf(full, sizeof(full), "%sll%c", spec, conv);
                uint64_t val = is_int(args[arg_idx]) ? (uint64_t)args[arg_idx].as.integer
                             : is_float(args[arg_idx]) ? (uint64_t)args[arg_idx].as.number
                             : 0;
                n = snprintf(tmp, sizeof(tmp), full, (unsigned long long)val);
                arg_idx++;
                break;
            }
            case 'f':
            case 'e':
            case 'E':
            case 'g':
            case 'G':
            {
                if (arg_idx >= nargs)
                {
                    free(out);
                    vm->runtime_error("format(): not enough arguments for %%%c.", conv);
                    return -1;
                }
                double val = is_float(args[arg_idx]) ? args[arg_idx].as.number
                           : is_int(args[arg_idx]) ? (double)args[arg_idx].as.integer
                           : 0.0;
                n = snprintf(tmp, sizeof(tmp), spec, val);
                arg_idx++;
                break;
            }
            case 's':
            {
                if (arg_idx >= nargs)
                {
                    free(out);
                    vm->runtime_error("format(): not enough arguments for %%s.");
                    return -1;
                }
                const char *sv = "nil";
                int sl = 3;
                if (is_string(args[arg_idx]))
                {
                    sv = as_cstring(args[arg_idx]);
                    sl = as_string(args[arg_idx])->length;
                }
                else if (is_int(args[arg_idx]))
                {
                    sl = snprintf(tmp, sizeof(tmp), "%lld", (long long)args[arg_idx].as.integer);
                    sv = tmp;
                }
                else if (is_float(args[arg_idx]))
                {
                    sl = snprintf(tmp, sizeof(tmp), "%g", args[arg_idx].as.number);
                    sv = tmp;
                }
                else if (is_bool(args[arg_idx]))
                {
                    sv = args[arg_idx].as.boolean ? "true" : "false";
                    sl = args[arg_idx].as.boolean ? 4 : 5;
                }
                else if (is_nil(args[arg_idx]))
                {
                    sv = "nil";
                    sl = 3;
                }

                /* If spec is just "%s", skip snprintf overhead */
                if (si == 2)
                {
                    grow(sl);
                    memcpy(out + len, sv, (size_t)sl);
                    len += sl;
                    arg_idx++;
                    continue;
                }
                n = snprintf(tmp, sizeof(tmp), spec, sv);
                arg_idx++;
                break;
            }
            case 'c':
            {
                if (arg_idx >= nargs)
                {
                    free(out);
                    vm->runtime_error("format(): not enough arguments for %%c.");
                    return -1;
                }
                int ch = is_int(args[arg_idx]) ? (int)args[arg_idx].as.integer : '?';
                n = snprintf(tmp, sizeof(tmp), spec, ch);
                arg_idx++;
                break;
            }
            default:
                /* Unknown specifier — output as-is */
                grow(si);
                memcpy(out + len, spec, (size_t)si);
                len += si;
                continue;
            }

            if (n > 0)
            {
                grow(n);
                memcpy(out + len, tmp, (size_t)n);
                len += n;
            }
        }

        args[0] = val_obj((Obj *)vm->make_string(out, len));
        free(out);
        return 1;
    }

    /* =========================================================
    ** collect() → int (freed bytes)
    ** Force a garbage collection cycle.
    ** ========================================================= */
    static int nat_collect(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        GC *gc = &vm->get_gc();
        size_t before = gc->bytes_allocated;
        gc_collect(vm);
        int64_t freed = (int64_t)(before - gc->bytes_allocated);
        args[0] = val_int(freed);
        return 1;
    }

    /* =========================================================
    ** mem_used() → int (bytes currently allocated by GC)
    ** ========================================================= */
    static int nat_mem_used(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        GC *gc = &vm->get_gc();
        args[0] = val_int((int64_t)gc->bytes_allocated);
        return 1;
    }

    /* =========================================================
    ** mem_info() → map { "used", "next_gc", "objects" }
    ** ========================================================= */
    static int nat_mem_info(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        GC *gc = &vm->get_gc();

        /* Count objects in linked list */
        int64_t obj_count = 0;
        for (Obj *o = gc->objects; o != nullptr; o = o->gc_next)
            obj_count++;

        ObjMap *map = new_map(gc);
        ObjString *k_used = new_string(gc, "used", 4);
        ObjString *k_next = new_string(gc, "next_gc", 7);
        ObjString *k_objs = new_string(gc, "objects", 7);

        map_set(gc, map, val_obj((Obj *)k_used), val_int((int64_t)gc->bytes_allocated));
        map_set(gc, map, val_obj((Obj *)k_next), val_int((int64_t)gc->next_gc));
        map_set(gc, map, val_obj((Obj *)k_objs), val_int(obj_count));

        args[0] = val_obj((Obj *)map);
        return 1;
    }

    /* =========================================================
    ** Process control natives
    ** ========================================================= */

    /* signal(target, sig) — target can be int (process ID) or closure (type) */
    static int nat_signal(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2) return 0;
        int sig = (int)to_integer(args[1]);
        if (is_int(args[0]))
        {
            vm->signal_process((int)to_integer(args[0]), sig);
        }
        else if (is_closure(args[0]))
        {
            ObjClosure *cl = as_closure(args[0]);
            vm->signal_type(cl->func, sig);
        }
        return 0;
    }

    /* let_me_alone() — kill all processes except the caller */
    static int nat_let_me_alone(VM *vm, Value *args, int nargs)
    {
        (void)args; (void)nargs;
        vm->let_me_alone();
        return 0;
    }

    /* get_id(type) — returns ID of first process of that type, or 0 */
    static int nat_get_id(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_closure(args[0]))
        {
            args[0] = val_int(0);
            return 1;
        }
        ObjClosure *cl = as_closure(args[0]);
        int id = vm->get_id_by_type(cl->func);
        args[0] = val_int(id);
        return 1;
    }

    /* advance(dist) — move current process along its angle by dist (or speed if no arg) */
    static int nat_advance(VM *vm, Value *args, int nargs)
    {
        VM::ProcessSlot *slot = vm->current_slot();
        if (!slot) { vm->runtime_error("advance() outside process"); return 0; }
        double angle = to_number(slot->privates[VM::PRIV_ANGLE]) * 3.14159265358979323846 / 180.0;
        double dist = (nargs > 0) ? to_number(args[0]) : to_number(slot->privates[VM::PRIV_SPEED]);
        double x = to_number(slot->privates[VM::PRIV_X]);
        double y = to_number(slot->privates[VM::PRIV_Y]);
        slot->privates[VM::PRIV_X] = val_float(x + dist * cos(angle));
        slot->privates[VM::PRIV_Y] = val_float(y + dist * sin(angle));
        return 0;
    }

    static int nat_advance_process(VM *vm, Value *args, int nargs)
    {
        float dt = 0.016f;
        if (nargs >= 1)
            dt = (float)to_number(args[0]);

        int alive = vm->tick_processes(dt);
        args[0] = val_int(alive);
        return 1;
    }



    /* =========================================================
    ** classname(x) — class name for an instance, else the basic type name.
    ** ========================================================= */
    static int nat_classname(VM *vm, Value *args, int nargs)
    {
        if (nargs >= 1 && is_instance(args[0]))
        {
            ObjClass *k = ((ObjInstance *)args[0].as.obj)->klass;
            args[0] = val_obj((Obj *)vm->make_string(k->name ? k->name->chars : "instance"));
            return 1;
        }
        return nat_typeof(vm, args, nargs);
    }

    /* =========================================================
    ** pcall(fn, args...) — protected call.
    ** Returns (true, result) if fn runs, or (false, message) on a runtime error.
    ** ========================================================= */
    static int nat_pcall(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1)
        {
            args[0] = val_bool(false);
            args[1] = val_obj((Obj *)vm->make_string("pcall: missing function argument"));
            return 2;
        }
        Value fn = args[0];
        bool ok = false;
        Value res = vm->call_protected(fn, args + 1, nargs - 1, &ok);
        args[0] = val_bool(ok);
        args[1] = res;
        return 2;
    }

    /* =========================================================
    ** Registration table
    ** ========================================================= */

    static const NativeReg base_functions[] = {
        {"pcall", nat_pcall, -1},
        {"classname", nat_classname, 1},
        {"str", nat_str, 1},
        {"int", nat_int, 1},
        {"float", nat_float, 1},
        {"char", nat_char, 1},
        {"chr", nat_char, 1},
        {"ord", nat_ord, 1},
        {"typeof", nat_typeof, 1},
        {"isNil", nat_isNil, 1},
        {"isBool", nat_isBool, 1},
        {"isInt", nat_isInt, 1},
        {"isFloat", nat_isFloat, 1},
        {"isNumber", nat_isNumber, 1},
        {"isString", nat_isString, 1},
        {"isArray", nat_isArray, 1},
        {"isMap", nat_isMap, 1},
        {"isFunction", nat_isFunction, 1},
        {"input", nat_input, -1},
        {"assert", nat_assert, -1},
        {"error", nat_error, 1},
        {"range", nat_range, -1},
        {"format", nat_format, -1},
        {"collect", nat_collect, 0},
        {"mem_used", nat_mem_used, 0},
        {"mem_info", nat_mem_info, 0},
        {"signal", nat_signal, -1},
        {"let_me_alone", nat_let_me_alone, 0},
        {"get_id", nat_get_id, 1},
        {"advance", nat_advance, -1},
        {"advance_process", nat_advance_process, -1},
    };

    static const NativeConst base_constants[] = {
        {"S_KILL",   val_int(VM::SIG_KILL)},
        {"S_FREEZE", val_int(VM::SIG_FREEZE)},
        {"S_SLEEP",  val_int(VM::SIG_SLEEP)},
        {"S_WAKEUP", val_int(VM::SIG_WAKEUP)},
    };

    const NativeLib zen_lib_base = {
        "base",
        base_functions,
        31,   /* num_functions */
        base_constants,
        4,    /* num_constants */
    };

} /* namespace zen */
