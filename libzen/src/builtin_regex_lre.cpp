#ifdef ZEN_ENABLE_REGEX

/* =========================================================
** builtin_regex.cpp — "re" module for Zen (libregexp from QuickJS)
**
** Zero external dependencies. Uses Bellard's libregexp (MIT).
**
** Usage:
**   import re;
**   re.match("(\\d+)-(\\w+)", "123-abc")  → "123-abc"
**   re.search("(\\d+)-(\\w+)", "123-abc") → ["123-abc", "123", "abc"]
**   re.find_all("\\d+", "a1b22c333")      → ["1", "22", "333"]
**   re.replace("\\d+", "X", "a1b2c3")     → "aXbXcX"
**   re.split(",\\s*", "a, b, c")           → ["a", "b", "c"]
**   re.test("\\d+", "abc123")             → true
** ========================================================= */

#include "module.h"
#include "vm.h"
#include <cstring>
#include <cstdlib>

extern "C" {
#include "libregexp.h"
}

namespace zen
{

    /* =========================================================
    ** libregexp callbacks (required by the library)
    ** ========================================================= */

    extern "C" {

    void *lre_realloc(void *opaque, void *ptr, size_t size)
    {
        (void)opaque;
        if (size == 0)
        {
            free(ptr);
            return nullptr;
        }
        return realloc(ptr, size);
    }

    bool lre_check_stack_overflow(void *opaque, size_t alloca_size)
    {
        (void)opaque;
        (void)alloca_size;
        return false; /* no stack overflow */
    }

    int lre_check_timeout(void *opaque)
    {
        (void)opaque;
        return 0; /* no timeout */
    }

    } /* extern "C" */

    /* =========================================================
    ** Internal helpers
    ** ========================================================= */

    /* Compile pattern. Returns bytecode (caller must free), or nullptr on error. */
    static uint8_t *compile_pattern(VM *vm, const char *pattern, int len, int flags)
    {
        char error_msg[128];
        int bytecode_len;
        uint8_t *bc = lre_compile(&bytecode_len, error_msg, sizeof(error_msg),
                                  pattern, (size_t)len,
                                  flags | LRE_FLAG_UNICODE, nullptr);
        if (!bc)
        {
            vm->runtime_error("re: compile error: %s", error_msg);
            return nullptr;
        }
        return bc;
    }

    /* Execute regex. Returns number of captures (0 = no match), or < 0 on error.
    ** capture must be array of uint8_t* with at least (capture_count*2) entries. */
    __attribute__((unused))
    static int exec_regex(uint8_t *bc, const char *subject, int subject_len,
                          int start_offset, uint8_t **capture)
    {
        int capture_count = lre_get_capture_count(bc);
        int ret = lre_exec(capture, bc,
                           (const uint8_t *)subject + start_offset,
                           0, subject_len - start_offset,
                           0, nullptr);
        if (ret == 1)
            return capture_count;
        return ret; /* 0 = no match, <0 = error */
    }

    /* Get match start/end offsets from capture pointers relative to subject */
    static inline void get_offsets(uint8_t **capture, int group,
                                   const char *subject, int *out_start, int *out_end)
    {
        if (capture[2 * group] && capture[2 * group + 1])
        {
            *out_start = (int)((const char *)capture[2 * group] - subject);
            *out_end = (int)((const char *)capture[2 * group + 1] - subject);
        }
        else
        {
            *out_start = -1;
            *out_end = -1;
        }
    }

    /* =========================================================
    ** match(pattern, subject) → string or nil
    ** First match (full match string).
    ** ========================================================= */
    static int nat_re_match(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_string(args[0]) || !is_string(args[1]))
        {
            vm->runtime_error("re.match() expects (pattern, subject).");
            return -1;
        }

        ObjString *pat = as_string(args[0]);
        ObjString *subj = as_string(args[1]);

        uint8_t *bc = compile_pattern(vm, pat->chars, pat->length, 0);
        if (!bc)
            return -1;

        int capture_count = lre_get_capture_count(bc);
        uint8_t **capture = (uint8_t **)calloc((size_t)(capture_count * 2), sizeof(uint8_t *));

        int ret = lre_exec(capture, bc,
                           (const uint8_t *)subj->chars, 0, subj->length,
                           0, nullptr);

        if (ret != 1)
        {
            free(capture);
            free(bc);
            args[0] = val_nil();
            return 1;
        }

        int s, e;
        get_offsets(capture, 0, subj->chars, &s, &e);
        if (s >= 0 && e >= s)
            args[0] = val_obj((Obj *)vm->make_string(subj->chars + s, e - s));
        else
            args[0] = val_nil();

        free(capture);
        free(bc);
        return 1;
    }

    /* =========================================================
    ** search(pattern, subject) → array [full, group1, ...] or nil
    ** First match with all captures.
    ** ========================================================= */
    static int nat_re_search(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_string(args[0]) || !is_string(args[1]))
        {
            vm->runtime_error("re.search() expects (pattern, subject).");
            return -1;
        }

        ObjString *pat = as_string(args[0]);
        ObjString *subj = as_string(args[1]);

        uint8_t *bc = compile_pattern(vm, pat->chars, pat->length, 0);
        if (!bc)
            return -1;

        int capture_count = lre_get_capture_count(bc);
        uint8_t **capture = (uint8_t **)calloc((size_t)(capture_count * 2), sizeof(uint8_t *));

        int ret = lre_exec(capture, bc,
                           (const uint8_t *)subj->chars, 0, subj->length,
                           0, nullptr);

        if (ret != 1)
        {
            free(capture);
            free(bc);
            args[0] = val_nil();
            return 1;
        }

        ObjArray *arr = new_array(&vm->get_gc());
        for (int i = 0; i < capture_count; i++)
        {
            int s, e;
            get_offsets(capture, i, subj->chars, &s, &e);
            if (s >= 0 && e >= s)
            {
                Value str = val_obj((Obj *)vm->make_string(subj->chars + s, e - s));
                array_push(&vm->get_gc(), arr, str);
            }
            else
            {
                array_push(&vm->get_gc(), arr, val_nil());
            }
        }

        args[0] = val_obj((Obj *)arr);
        free(capture);
        free(bc);
        return 1;
    }

    /* =========================================================
    ** find_all(pattern, subject) → array of strings
    ** All non-overlapping matches (full match only).
    ** ========================================================= */
    static int nat_re_find_all(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_string(args[0]) || !is_string(args[1]))
        {
            vm->runtime_error("re.find_all() expects (pattern, subject).");
            return -1;
        }

        ObjString *pat = as_string(args[0]);
        ObjString *subj = as_string(args[1]);

        uint8_t *bc = compile_pattern(vm, pat->chars, pat->length, 0);
        if (!bc)
            return -1;

        int capture_count = lre_get_capture_count(bc);
        uint8_t **capture = (uint8_t **)calloc((size_t)(capture_count * 2), sizeof(uint8_t *));
        ObjArray *result = new_array(&vm->get_gc());

        int offset = 0;
        while (offset <= subj->length)
        {
            int ret = lre_exec(capture, bc,
                               (const uint8_t *)subj->chars, offset, subj->length,
                               0, nullptr);
            if (ret != 1)
                break;

            int s, e;
            get_offsets(capture, 0, subj->chars, &s, &e);
            if (s < 0)
                break;

            Value str = val_obj((Obj *)vm->make_string(subj->chars + s, e - s));
            array_push(&vm->get_gc(), result, str);

            offset = e;
            if (e == s)
                offset++;
        }

        args[0] = val_obj((Obj *)result);
        free(capture);
        free(bc);
        return 1;
    }

    /* =========================================================
    ** replace(pattern, replacement, subject, max?) → string
    ** Replace all (or up to max) occurrences.
    ** ========================================================= */
    static int nat_re_replace(VM *vm, Value *args, int nargs)
    {
        if (nargs < 3 || !is_string(args[0]) || !is_string(args[1]) || !is_string(args[2]))
        {
            vm->runtime_error("re.replace() expects (pattern, replacement, subject).");
            return -1;
        }

        ObjString *pat = as_string(args[0]);
        ObjString *repl = as_string(args[1]);
        ObjString *subj = as_string(args[2]);
        int max_replacements = (nargs >= 4 && is_int(args[3])) ? (int)args[3].as.integer : -1;

        uint8_t *bc = compile_pattern(vm, pat->chars, pat->length, 0);
        if (!bc)
            return -1;

        int capture_count = lre_get_capture_count(bc);
        uint8_t **capture = (uint8_t **)calloc((size_t)(capture_count * 2), sizeof(uint8_t *));

        int capacity = subj->length + 64;
        char *buf = (char *)malloc((size_t)capacity);
        int buf_len = 0;
        int offset = 0;
        int count = 0;

        while (offset <= subj->length)
        {
            if (max_replacements >= 0 && count >= max_replacements)
                break;

            int ret = lre_exec(capture, bc,
                               (const uint8_t *)subj->chars, offset, subj->length,
                               0, nullptr);
            if (ret != 1)
                break;

            int s, e;
            get_offsets(capture, 0, subj->chars, &s, &e);
            if (s < 0)
                break;

            /* Copy text before match */
            int pre_len = s - offset;
            int needed = buf_len + pre_len + repl->length + 1;
            if (needed > capacity)
            {
                capacity = needed * 2;
                buf = (char *)realloc(buf, (size_t)capacity);
            }
            memcpy(buf + buf_len, subj->chars + offset, (size_t)pre_len);
            buf_len += pre_len;

            /* Copy replacement */
            memcpy(buf + buf_len, repl->chars, (size_t)repl->length);
            buf_len += repl->length;

            offset = e;
            if (e == s)
                offset++;
            count++;
        }

        /* Copy remainder */
        int rem = subj->length - offset;
        if (rem > 0)
        {
            if (buf_len + rem + 1 > capacity)
            {
                capacity = buf_len + rem + 1;
                buf = (char *)realloc(buf, (size_t)capacity);
            }
            memcpy(buf + buf_len, subj->chars + offset, (size_t)rem);
            buf_len += rem;
        }

        args[0] = val_obj((Obj *)vm->make_string(buf, buf_len));
        free(buf);
        free(capture);
        free(bc);
        return 1;
    }

    /* =========================================================
    ** split(pattern, subject, max?) → array of strings
    ** Split subject by pattern matches.
    ** ========================================================= */
    static int nat_re_split(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_string(args[0]) || !is_string(args[1]))
        {
            vm->runtime_error("re.split() expects (pattern, subject).");
            return -1;
        }

        ObjString *pat = as_string(args[0]);
        ObjString *subj = as_string(args[1]);
        int max_splits = (nargs >= 3 && is_int(args[2])) ? (int)args[2].as.integer : -1;

        uint8_t *bc = compile_pattern(vm, pat->chars, pat->length, 0);
        if (!bc)
            return -1;

        int capture_count = lre_get_capture_count(bc);
        uint8_t **capture = (uint8_t **)calloc((size_t)(capture_count * 2), sizeof(uint8_t *));
        ObjArray *result = new_array(&vm->get_gc());

        int offset = 0;
        int count = 0;

        while (offset <= subj->length)
        {
            if (max_splits >= 0 && count >= max_splits)
                break;

            int ret = lre_exec(capture, bc,
                               (const uint8_t *)subj->chars, offset, subj->length,
                               0, nullptr);
            if (ret != 1)
                break;

            int s, e;
            get_offsets(capture, 0, subj->chars, &s, &e);
            if (s < 0)
                break;

            /* Add segment before match */
            int seg_len = s - offset;
            Value seg = val_obj((Obj *)vm->make_string(subj->chars + offset, seg_len));
            array_push(&vm->get_gc(), result, seg);

            offset = e;
            if (e == s)
                offset++;
            count++;
        }

        /* Add final segment */
        int rem = subj->length - offset;
        Value last = val_obj((Obj *)vm->make_string(subj->chars + offset, rem >= 0 ? rem : 0));
        array_push(&vm->get_gc(), result, last);

        args[0] = val_obj((Obj *)result);
        free(capture);
        free(bc);
        return 1;
    }

    /* =========================================================
    ** test(pattern, subject) → bool
    ** Quick check if pattern matches anywhere in subject.
    ** ========================================================= */
    static int nat_re_test(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_string(args[0]) || !is_string(args[1]))
        {
            vm->runtime_error("re.test() expects (pattern, subject).");
            return -1;
        }

        ObjString *pat = as_string(args[0]);
        ObjString *subj = as_string(args[1]);

        uint8_t *bc = compile_pattern(vm, pat->chars, pat->length, 0);
        if (!bc)
            return -1;

        int capture_count = lre_get_capture_count(bc);
        uint8_t **capture = (uint8_t **)calloc((size_t)(capture_count * 2), sizeof(uint8_t *));

        int ret = lre_exec(capture, bc,
                           (const uint8_t *)subj->chars, 0, subj->length,
                           0, nullptr);

        args[0] = val_bool(ret == 1);
        free(capture);
        free(bc);
        return 1;
    }

    /* =========================================================
    ** Registration
    ** ========================================================= */

    static const NativeReg re_functions[] = {
        {"match", nat_re_match, 2},
        {"search", nat_re_search, 2},
        {"find_all", nat_re_find_all, 2},
        {"replace", nat_re_replace, -1},
        {"split", nat_re_split, -1},
        {"test", nat_re_test, 2},
    };

    const NativeLib zen_lib_re = {
        "re",
        re_functions,
        6,
        nullptr,
        0,
        nullptr
    };

} /* namespace zen */

#endif /* ZEN_ENABLE_REGEX */
