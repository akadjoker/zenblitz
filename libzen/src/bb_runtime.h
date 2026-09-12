/*
** bb_runtime.h — runtime support for Blitz programs running on the Zen VM.
**
**  - registers the Blitz command set (Print, Left$, Sin, ...) as Zen
**    natives and builds the compiler Environ describing them
**  - provides the internal helpers the code generator calls (__bb*):
**    type/object lists, Data/Read, conversions, arrays, handles
*/
#ifndef BB_RUNTIME_H
#define BB_RUNTIME_H

#include "bb_std.h"
#include "bb_type.h"
#include "vm.h"
#include "memory.h"

namespace bb
{
    /* field kinds (what a Type field / array element holds) */
    enum
    {
        KIND_INT = 0, KIND_FLOAT = 1, KIND_STR = 2, KIND_OBJ = 3, KIND_VEC = 4
    };

    /* hidden fields appended after the user fields of every Type */
    enum
    {
        HF_PREV = 0, HF_NEXT = 1, HF_ALIVE = 2, HF_HANDLE = 3, HF_COUNT = 4
    };

    struct BBTypeInfo
    {
        string name;
        zen::ObjStructDef *def;
        int nfields;            /* user fields */
        vector<int> kinds;      /* per user field */
        vector<int> vecSizes;   /* KIND_VEC: element count */
        vector<int> vecKinds;   /* KIND_VEC: element kind */
        int gFirst, gLast;      /* zen globals holding list head/tail */
        int gDef;               /* zen global holding the struct def */
    };

    /* A Blitz command: signature string in the original Blitz format
       ("$Left$string%count", "%Rand%from%to=1", "Print$string=\"\"") + native. */
    struct BBCommand
    {
        const char *sig;
        zen::NativeFn fn;
    };

    struct BBRuntime
    {
        zen::VM *vm;
        Environ *env; /* command declarations for the compiler */

        /* helper natives (zen global indices) */
        int g_ftoi, g_stoi, g_stof, g_ftostr, g_objtostr, g_sgn;
        int g_new, g_delete, g_deleteEach, g_insBefore, g_insAfter, g_after, g_before;
        int g_handle, g_object;
        int g_dim, g_vec;
        int g_readInt, g_readFloat, g_readStr;
        int g_gosubPop, g_rterror;
        int g_end; /* the "End" command decl offset (emitted inline as HALT) */

        /* Data */
        int g_data, g_dataptr;

        vector<BBTypeInfo *> types;
        map<zen::ObjStructDef *, BBTypeInfo *> typeByDef;

        bool isRuntimeDecl(Decl *d) const { return env->funcDecls->findDecl(d->name) == d; }
        int registerType(const string &name, const vector<int> &kinds,
                         const vector<int> &vecSizes, const vector<int> &vecKinds);
        /* Type info for a struct def, rebuilt from the encoded field names
           when the def came from loaded bytecode rather than this compiler. */
        BBTypeInfo *typeForDef(zen::ObjStructDef *def);
        void registerCommands(const BBCommand *cmds, int n);
    };

    BBRuntime *bb_runtime_create(zen::VM *vm);
    BBRuntime *bb_runtime_get();
    BBRuntime *bb_runtime_for(zen::VM *vm); /* create on first use, cached per VM */
    void bb_runtime_destroy(BBRuntime *rt);

    /* value helpers for natives */
    inline long long bb_arg_int(zen::Value v)
    {
        if (zen::is_int(v)) return v.as.integer;
        if (zen::is_float(v)) return bb_round(v.as.number);
        if (zen::is_bool(v)) return v.as.boolean ? 1 : 0;
        return 0;
    }
    inline double bb_arg_float(zen::Value v)
    {
        if (zen::is_float(v)) return v.as.number;
        if (zen::is_int(v)) return (double)v.as.integer;
        return 0.0;
    }
    inline const char *bb_arg_cstr(zen::Value v)
    {
        return zen::is_string(v) ? zen::as_cstring(v) : "";
    }
    inline int bb_arg_len(zen::Value v)
    {
        return zen::is_string(v) ? zen::as_string(v)->length : 0;
    }
    inline string bb_arg_str(zen::Value v)
    {
        return zen::is_string(v) ? string(zen::as_cstring(v), zen::as_string(v)->length) : string();
    }
    inline zen::Value bb_ret_str(zen::VM *vm, const string &s)
    {
        return zen::val_obj((zen::Obj *)zen::new_string(&vm->get_gc(), s.data(), (int)s.size()));
    }
    inline zen::Value bb_ret_str(zen::VM *vm, const char *s, int n)
    {
        return zen::val_obj((zen::Obj *)zen::new_string(&vm->get_gc(), s, n));
    }

    /* text output hook (Print/Write) */
    void bb_print(zen::VM *vm, const char *s, int n);

    /* command tables (bb_cmds.cpp) */
    extern const BBCommand bb_cmds_basic[];
    extern const int bb_cmds_basic_count;
}

#endif
