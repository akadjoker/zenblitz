/* =========================================================
** builtin_ini.cpp — "ini" module for Zen
**
** Parse and write INI-style configuration files.
** Supports: sections, key=value, # and ; comments,
**           multi-value keys, quoted values, inline comments.
**
** Usage:
**   import ini;
**
**   var cfg = ini.parse("[window]\nwidth=800\nheight=600\ntitle=My Game\n");
**   print(cfg["window"]["width"]);   // 800
**   print(cfg["window"]["title"]);   // My Game
**
**   // load from file
**   var cfg2 = ini.load("settings.ini");
**
**   // stringify back
**   print(ini.stringify(cfg));
**
**   // save
**   ini.save(cfg, "out.ini");
** ========================================================= */

#include "module.h"
#include "vm.h"
#include "memory.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>

namespace zen
{

/* =========================================================
** Internal helpers
** ========================================================= */

static void skip_ws(const char **p)
{
    while (**p == ' ' || **p == '\t') (*p)++;
}

static void skip_line(const char **p)
{
    while (**p && **p != '\n') (*p)++;
    if (**p == '\n') (*p)++;
}

/* Copy trimmed string [start, end) into buf (null-terminated). */
static void trim_copy(char *buf, int bufsz, const char *start, const char *end)
{
    while (start < end && (*start == ' ' || *start == '\t')) start++;
    while (end > start && (*(end-1) == ' ' || *(end-1) == '\t' || *(end-1) == '\r')) end--;
    int len = (int)(end - start);
    if (len >= bufsz) len = bufsz - 1;
    memcpy(buf, start, (size_t)len);
    buf[len] = '\0';
}

/* =========================================================
** ini.parse(text [, keepGlobal]) → map of maps
**
** Returns a map where each key is a section name and each
** value is a map of key→value strings.
** Keys before any section go into "__global__".
** ========================================================= */

static int nat_ini_parse(VM *vm, Value *args, int nargs)
{
    if (nargs < 1 || !is_string(args[0]))
    {
        vm->runtime_error("ini.parse() expects (text)");
        return -1;
    }
    ObjString *src = as_string(args[0]);
    GC &gc = vm->get_gc();

    ObjMap *root = new_map(&gc);
    /* Current section map */
    ObjMap *section = new_map(&gc);
    ObjString *section_key = vm->make_string("__global__");
    map_set(&gc, root, val_obj((Obj *)section_key), val_obj((Obj *)section));

    const char *p = src->chars;
    const char *end = p + src->length;

    char keybuf[256];
    char valbuf[1024];

    while (p < end)
    {
        skip_ws(&p);
        if (p >= end) break;

        char c = *p;

        /* Comment */
        if (c == '#' || c == ';')
        {
            skip_line(&p);
            continue;
        }

        /* Empty line */
        if (c == '\n' || c == '\r')
        {
            if (c == '\r' && p+1 < end && *(p+1) == '\n') p++;
            p++;
            continue;
        }

        /* Section header: [name] */
        if (c == '[')
        {
            p++; /* skip '[' */
            const char *name_start = p;
            while (p < end && *p != ']' && *p != '\n') p++;
            trim_copy(keybuf, sizeof(keybuf), name_start, p);
            if (*p == ']') p++; /* skip ']' */
            skip_line(&p);

            /* Find or create section */
            ObjString *sk = vm->make_string(keybuf);
            Value existing;
            bool found = false;
            existing = map_get(root, val_obj((Obj *)sk), &found);
            if (found && is_map(existing))
            {
                section = as_map(existing);
            }
            else
            {
                section = new_map(&gc);
                map_set(&gc, root, val_obj((Obj *)sk), val_obj((Obj *)section));
            }
            continue;
        }

        /* Key = value */
        const char *key_start = p;
        while (p < end && *p != '=' && *p != ':' && *p != '\n') p++;
        if (*p != '=' && *p != ':')
        {
            skip_line(&p);
            continue;
        }
        trim_copy(keybuf, sizeof(keybuf), key_start, p);
        p++; /* skip '=' or ':' */
        skip_ws(&p);

        /* Value: handle quoted strings */
        const char *val_start = p;
        if (*p == '"' || *p == '\'')
        {
            char quote = *p++;
            val_start = p;
            int vi = 0;
            while (p < end && *p != quote && *p != '\n')
            {
                if (*p == '\\' && p+1 < end)
                {
                    p++;
                    char esc = *p++;
                    if (vi < (int)sizeof(valbuf) - 1)
                    {
                        switch (esc)
                        {
                            case 'n':  valbuf[vi++] = '\n'; break;
                            case 't':  valbuf[vi++] = '\t'; break;
                            default:   valbuf[vi++] = esc;  break;
                        }
                    }
                }
                else
                {
                    if (vi < (int)sizeof(valbuf) - 1) valbuf[vi++] = *p;
                    p++;
                }
            }
            valbuf[vi] = '\0';
            if (*p == quote) p++;
            skip_line(&p);
        }
        else
        {
            /* Unquoted: strip inline comment and trailing whitespace */
            const char *val_end = p;
            while (p < end && *p != '\n')
            {
                if (*p == '#' || *p == ';') break;
                val_end = p + 1;
                p++;
            }
            skip_line(&p);
            trim_copy(valbuf, sizeof(valbuf), val_start, val_end);
        }

        ObjString *k = vm->make_string(keybuf);
        ObjString *v = vm->make_string(valbuf);
        map_set(&gc, section, val_obj((Obj *)k), val_obj((Obj *)v));
    }

    args[0] = val_obj((Obj *)root);
    return 1;
}

/* =========================================================
** ini.load(path) → map of maps
** ========================================================= */

static int nat_ini_load(VM *vm, Value *args, int nargs)
{
    if (nargs < 1 || !is_string(args[0]))
    {
        vm->runtime_error("ini.load() expects (path)");
        return -1;
    }
    FILE *f = fopen(as_cstring(args[0]), "rb");
    if (!f)
    {
        vm->runtime_error("ini.load(): cannot open file");
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); vm->runtime_error("ini.load(): out of memory"); return -1; }
    fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[sz] = '\0';

    ObjString *src = vm->make_string(buf, (int)sz);
    free(buf);
    args[0] = val_obj((Obj *)src);
    return nat_ini_parse(vm, args, 1);
}

/* =========================================================
** ini.stringify(cfg) → string
** ========================================================= */

static int nat_ini_stringify(VM *vm, Value *args, int nargs)
{
    (void)nargs;
    if (!is_map(args[0]))
    {
        vm->runtime_error("ini.stringify() expects (map)");
        return -1;
    }
    ObjMap *root = as_map(args[0]);
    GC &gc = vm->get_gc();

    /* Collect keys */
    ObjArray *sections = new_array(&gc);
    map_keys(&gc, root, sections);

    /* Build output */
    char buf[4096];
    ObjString *result = vm->make_string("", 0);
    /* We build via a growable C buffer */
    size_t cap = 4096;
    char *out = (char *)malloc(cap);
    size_t len = 0;

    auto out_str = [&](const char *s, size_t n) {
        while (len + n + 1 >= cap) { cap *= 2; out = (char *)realloc(out, cap); }
        memcpy(out + len, s, n);
        len += n;
    };

    auto out_cstr = [&](const char *s) { out_str(s, strlen(s)); };

    /* __global__ first, then other sections */
 
    for (int pass = 0; pass < 2; pass++)
    {
        for (int i = 0; i < arr_count(sections); i++)
        {
            Value sk = sections->data[i];
            if (!is_string(sk)) continue;
            ObjString *sec_name = as_string(sk);
            bool is_global = (strcmp(sec_name->chars, "__global__") == 0);
            if (pass == 0 && !is_global) continue;
            if (pass == 1 && is_global) continue;

            bool found;
            Value sv = map_get(root, sk, &found);
            if (!found || !is_map(sv)) continue;
            ObjMap *sec_map = as_map(sv);

            if (!is_global)
            {
                snprintf(buf, sizeof(buf), "[%s]\n", sec_name->chars);
                out_cstr(buf);
            }

            ObjArray *keys = new_array(&gc);
            map_keys(&gc, sec_map, keys);
            for (int j = 0; j < arr_count(keys); j++)
            {
                Value kv = keys->data[j];
                if (!is_string(kv)) continue;
                bool fv;
                Value vv = map_get(sec_map, kv, &fv);
                if (!fv) continue;

                const char *ks = as_string(kv)->chars;
                const char *vs = is_string(vv) ? as_string(vv)->chars : "";

                /* Quote if contains special chars */
                bool needs_q = false;
                for (const char *c = vs; *c; c++)
                    if (*c == '#' || *c == ';' || *c == '\n') { needs_q = true; break; }

                if (needs_q)
                    snprintf(buf, sizeof(buf), "%s=\"%s\"\n", ks, vs);
                else
                    snprintf(buf, sizeof(buf), "%s=%s\n", ks, vs);
                out_cstr(buf);
            }
            out_cstr("\n");
        }
    }

    result = vm->make_string(out, (int)len);
    free(out);
    args[0] = val_obj((Obj *)result);
    return 1;
}

/* =========================================================
** ini.save(cfg, path) → bool
** ========================================================= */

static int nat_ini_save(VM *vm, Value *args, int nargs)
{
    if (nargs < 2 || !is_map(args[0]) || !is_string(args[1]))
    {
        vm->runtime_error("ini.save() expects (cfg, path)");
        return -1;
    }
    /* Stringify first */
    Value str_args[1] = { args[0] };
    if (nat_ini_stringify(vm, str_args, 1) < 0) return -1;

    ObjString *text = as_string(str_args[0]);
    FILE *f = fopen(as_cstring(args[1]), "wb");
    if (!f)
    {
        vm->runtime_error("ini.save(): cannot write file");
        return -1;
    }
    fwrite(text->chars, 1, (size_t)text->length, f);
    fclose(f);
    args[0] = val_bool(true);
    return 1;
}

/* =========================================================
** ini.get(cfg, section, key [, default]) → string
** Convenience: cfg["section"]["key"] with optional default
** ========================================================= */

static int nat_ini_get(VM *vm, Value *args, int nargs)
{
    if (nargs < 3 || !is_map(args[0]) || !is_string(args[1]) || !is_string(args[2]))
    {
        vm->runtime_error("ini.get() expects (cfg, section, key [, default])");
        return -1;
    }
    ObjMap *root = as_map(args[0]);
    bool found;
    Value sec_val = map_get(root, val_obj((Obj *)as_string(args[1])), &found);
    if (!found || !is_map(sec_val))
    {
        args[0] = (nargs >= 4) ? args[3] : val_nil();
        return 1;
    }
    Value v = map_get(as_map(sec_val), val_obj((Obj *)as_string(args[2])), &found);
    if (!found)
        args[0] = (nargs >= 4) ? args[3] : val_nil();
    else
        args[0] = v;
    return 1;
}

/* =========================================================
** Registration
** ========================================================= */

static const NativeReg ini_functions[] = {
    {"parse",     nat_ini_parse,     -1},
    {"load",      nat_ini_load,       1},
    {"stringify", nat_ini_stringify,  1},
    {"save",      nat_ini_save,       2},
    {"get",       nat_ini_get,       -1},
};

extern const NativeLib zen_lib_ini = {
    "ini",
    ini_functions,
    5,
    nullptr,
    0,
    nullptr
};

} /* namespace zen */
