#ifdef ZEN_ENABLE_UTF8

/* =========================================================
** builtin_utf8.cpp — "utf8" module for Zen
**
** UTF-8 string operations:
**   utf8.len(str)           → number of codepoints
**   utf8.codepoints(str)    → array of codepoint ints
**   utf8.encode(cp)         → single-char string from codepoint
**   utf8.encode(arr)        → string from array of codepoints
**   utf8.decode(str, i?)    → codepoint at byte index i (0-based)
**   utf8.offset(str, n, i?) → byte offset of n-th codepoint from i
**   utf8.valid(str)         → true if valid UTF-8
**
** Pure C, no external deps.
** ========================================================= */

#include "module.h"
#include "vm.h"
#include <cstring>
#include <cstdlib>

namespace zen
{

#define MAXUNICODE 0x10FFFF

    /* =========================================================
    ** Internal helpers
    ** ========================================================= */

    /* Returns pointer past decoded char, or NULL if invalid */
    static const char *utf8_decode_one(const char *s, const char *end, int *out_cp)
    {
        unsigned char c = (unsigned char)*s;
        int cp;
        int n; /* expected continuation bytes */

        if (c < 0x80)
        {
            *out_cp = c;
            return s + 1;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            cp = c & 0x1F;
            n = 1;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            cp = c & 0x0F;
            n = 2;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            cp = c & 0x07;
            n = 3;
        }
        else
        {
            return NULL; /* invalid lead byte */
        }

        if (s + 1 + n > end)
            return NULL;

        for (int i = 1; i <= n; i++)
        {
            unsigned char cc = (unsigned char)s[i];
            if ((cc & 0xC0) != 0x80)
                return NULL;
            cp = (cp << 6) | (cc & 0x3F);
        }

        /* Check overlong and surrogates */
        if (cp > MAXUNICODE)
            return NULL;
        if (cp >= 0xD800 && cp <= 0xDFFF)
            return NULL;
        if (n == 1 && cp < 0x80)
            return NULL;
        if (n == 2 && cp < 0x800)
            return NULL;
        if (n == 3 && cp < 0x10000)
            return NULL;

        *out_cp = cp;
        return s + 1 + n;
    }

    /* Encode a codepoint to buf, returns bytes written (1-4), or 0 on error */
    static int utf8_encode_one(int cp, char *buf)
    {
        if (cp < 0 || cp > MAXUNICODE || (cp >= 0xD800 && cp <= 0xDFFF))
            return 0;
        if (cp < 0x80)
        {
            buf[0] = (char)cp;
            return 1;
        }
        if (cp < 0x800)
        {
            buf[0] = (char)(0xC0 | (cp >> 6));
            buf[1] = (char)(0x80 | (cp & 0x3F));
            return 2;
        }
        if (cp < 0x10000)
        {
            buf[0] = (char)(0xE0 | (cp >> 12));
            buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
            buf[2] = (char)(0x80 | (cp & 0x3F));
            return 3;
        }
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }

    /* =========================================================
    ** utf8.len(str) → int (number of codepoints)
    ** ========================================================= */
    static int nat_utf8_len(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_string(args[0]))
        {
            vm->runtime_error("utf8.len() expects a string.");
            return -1;
        }
        ObjString *s = as_string(args[0]);
        const char *p = s->chars;
        const char *end = p + s->length;
        int count = 0;
        while (p < end)
        {
            int cp;
            const char *next = utf8_decode_one(p, end, &cp);
            if (!next)
            {
                args[0] = val_int(-1); /* invalid UTF-8 */
                return 1;
            }
            count++;
            p = next;
        }
        args[0] = val_int(count);
        return 1;
    }

    /* =========================================================
    ** utf8.codepoints(str) → array of ints
    ** ========================================================= */
    static int nat_utf8_codepoints(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_string(args[0]))
        {
            vm->runtime_error("utf8.codepoints() expects a string.");
            return -1;
        }
        ObjString *s = as_string(args[0]);
        const char *p = s->chars;
        const char *end = p + s->length;

        GC *gc = &vm->get_gc();
        ObjArray *arr = new_array(gc);

        while (p < end)
        {
            int cp;
            const char *next = utf8_decode_one(p, end, &cp);
            if (!next)
            {
                vm->runtime_error("utf8.codepoints(): invalid UTF-8 at byte %d.",
                                  (int)(p - s->chars));
                return -1;
            }
            array_push(gc, arr, val_int(cp));
            p = next;
        }

        args[0] = val_obj((Obj *)arr);
        return 1;
    }

    /* =========================================================
    ** utf8.encode(codepoint_or_array) → string
    ** ========================================================= */
    static int nat_utf8_encode(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1)
        {
            vm->runtime_error("utf8.encode() expects int or array.");
            return -1;
        }

        if (is_int(args[0]))
        {
            int cp = (int)args[0].as.integer;
            char buf[4];
            int n = utf8_encode_one(cp, buf);
            if (n == 0)
            {
                vm->runtime_error("utf8.encode(): invalid codepoint %d.", cp);
                return -1;
            }
            args[0] = val_obj((Obj *)vm->make_string(buf, n));
            return 1;
        }

        if (is_array(args[0]))
        {
            ObjArray *arr = as_array(args[0]);
            int count = arr_count(arr);

            /* Worst case: 4 bytes per codepoint */
            int cap = count * 4;
            char *buf = (char *)malloc((size_t)cap);
            int len = 0;

            for (int i = 0; i < count; i++)
            {
                if (!is_int(arr->data[i]))
                {
                    free(buf);
                    vm->runtime_error("utf8.encode(): array must contain ints.");
                    return -1;
                }
                int cp = (int)arr->data[i].as.integer;
                int n = utf8_encode_one(cp, buf + len);
                if (n == 0)
                {
                    free(buf);
                    vm->runtime_error("utf8.encode(): invalid codepoint %d at index %d.", cp, i);
                    return -1;
                }
                len += n;
            }

            args[0] = val_obj((Obj *)vm->make_string(buf, len));
            free(buf);
            return 1;
        }

        vm->runtime_error("utf8.encode() expects int or array of ints.");
        return -1;
    }

    /* =========================================================
    ** utf8.decode(str, byte_index?) → codepoint int
    **   byte_index is 0-based, default 0
    ** ========================================================= */
    static int nat_utf8_decode(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_string(args[0]))
        {
            vm->runtime_error("utf8.decode() expects (string, [byte_index]).");
            return -1;
        }
        ObjString *s = as_string(args[0]);
        int idx = (nargs >= 2 && is_int(args[1])) ? (int)args[1].as.integer : 0;

        if (idx < 0 || idx >= s->length)
        {
            args[0] = val_int(-1);
            return 1;
        }

        int cp;
        const char *next = utf8_decode_one(s->chars + idx, s->chars + s->length, &cp);
        if (!next)
        {
            args[0] = val_int(-1);
            return 1;
        }

        args[0] = val_int(cp);
        return 1;
    }

    /* =========================================================
    ** utf8.offset(str, n, start_byte?) → byte offset
    **   n > 0: offset of n-th codepoint from start_byte (0-based)
    **   n = 0: start of codepoint at start_byte
    ** ========================================================= */
    static int nat_utf8_offset(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_string(args[0]) || !is_int(args[1]))
        {
            vm->runtime_error("utf8.offset() expects (string, n, [start]).");
            return -1;
        }
        ObjString *s = as_string(args[0]);
        int n = (int)args[1].as.integer;
        int start = (nargs >= 3 && is_int(args[2])) ? (int)args[2].as.integer : 0;

        if (start < 0 || start > s->length)
        {
            args[0] = val_int(-1);
            return 1;
        }

        const char *p = s->chars + start;
        const char *end = s->chars + s->length;

        if (n == 0)
        {
            /* Find beginning of current codepoint */
            while (p > s->chars && ((unsigned char)*p & 0xC0) == 0x80)
                p--;
            args[0] = val_int((int)(p - s->chars));
            return 1;
        }

        if (n > 0)
        {
            for (int i = 0; i < n; i++)
            {
                if (p >= end)
                {
                    args[0] = val_int(-1);
                    return 1;
                }
                int cp;
                const char *next = utf8_decode_one(p, end, &cp);
                if (!next)
                {
                    args[0] = val_int(-1);
                    return 1;
                }
                p = next;
            }
        }
        else
        {
            /* n < 0: go backwards */
            for (int i = 0; i > n; i--)
            {
                if (p <= s->chars)
                {
                    args[0] = val_int(-1);
                    return 1;
                }
                p--;
                while (p > s->chars && ((unsigned char)*p & 0xC0) == 0x80)
                    p--;
            }
        }

        args[0] = val_int((int)(p - s->chars));
        return 1;
    }

    /* =========================================================
    ** utf8.valid(str) → bool
    ** ========================================================= */
    static int nat_utf8_valid(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_string(args[0]))
        {
            args[0] = val_bool(false);
            return 1;
        }
        ObjString *s = as_string(args[0]);
        const char *p = s->chars;
        const char *end = p + s->length;
        while (p < end)
        {
            int cp;
            const char *next = utf8_decode_one(p, end, &cp);
            if (!next)
            {
                args[0] = val_bool(false);
                return 1;
            }
            p = next;
        }
        args[0] = val_bool(true);
        return 1;
    }

    /* =========================================================
    ** Registration
    ** ========================================================= */

    static const NativeReg utf8_functions[] = {
        {"len", nat_utf8_len, 1},
        {"codepoints", nat_utf8_codepoints, 1},
        {"encode", nat_utf8_encode, -1},
        {"decode", nat_utf8_decode, -1},
        {"offset", nat_utf8_offset, -1},
        {"valid", nat_utf8_valid, 1},
    };

    const NativeLib zen_lib_utf8 = {
        "utf8",
        utf8_functions,
        6,
        nullptr,
        0,
    };

} /* namespace zen */

#endif /* ZEN_ENABLE_UTF8 */
