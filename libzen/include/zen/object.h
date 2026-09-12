#ifndef ZEN_OBJECT_H
#define ZEN_OBJECT_H

#include "value.h"

namespace zen
{
    /*
    ** Obj — base of every heap object.
    **
    ** Tri-color GC (like Lua 5.x): WHITE = not visited, GRAY = visited but
    ** children pending, BLACK = done. All objects live in a linked list
    ** (gc_next). The type tag allows safe casts without RTTI.
    */
    enum ObjType : uint8_t
    {
        OBJ_STRING,
        OBJ_FUNC,
        OBJ_NATIVE,
        OBJ_ARRAY,
        OBJ_BUFFER,
        OBJ_STRUCT_DEF,
        OBJ_STRUCT,
    };

    enum GCColor : uint8_t
    {
        GC_WHITE = 0,
        GC_GRAY = 1,
        GC_BLACK = 2,
    };

    struct Obj
    {
        ObjType type;
        GCColor color;
        uint8_t interned; /* 1 = in the intern table (immutable) */
        uint8_t _pad;
        uint32_t hash;    /* string hash; 0 for other objects */
        Obj *gc_next;
    };

    inline bool is_obj_type(Value v, ObjType t)
    {
        return is_obj(v) && v.as.obj->type == t;
    }

    /* =========================================================
    ** ObjString — interned string, inline character buffer.
    ** ========================================================= */
    struct ObjString
    {
        Obj obj;
        int32_t length;
        int32_t capacity; /* allocated size of chars[] (0 = tight, length+1) */
        char chars[];
    };

    inline bool is_string(Value v) { return is_obj_type(v, OBJ_STRING); }
    inline ObjString *as_string(Value v) { return (ObjString *)v.as.obj; }
    inline const char *as_cstring(Value v) { return ((ObjString *)v.as.obj)->chars; }

    inline const void *zen_memmem(const void *hay, size_t haylen,
                                  const void *needle, size_t nlen)
    {
#ifdef _WIN32
        if (nlen == 0) return hay;
        if (haylen < nlen) return nullptr;
        const char *h = (const char *)hay;
        const char *end = h + haylen - nlen + 1;
        const char first = *(const char *)needle;
        while ((h = (const char *)memchr(h, first, (size_t)(end - h))) != nullptr)
        {
            if (memcmp(h, needle, nlen) == 0)
                return h;
            h++;
            if (h >= end)
                break;
        }
        return nullptr;
#else
        return memmem(hay, haylen, needle, nlen);
#endif
    }

    /* FNV-1a hash */
    inline uint32_t hash_string(const char *str, int length)
    {
        uint32_t h = 2166136261u;
        for (int i = 0; i < length; i++)
        {
            h ^= (uint8_t)str[i];
            h *= 16777619u;
        }
        return h;
    }

    /* Lazy string hash: 0 means "not computed yet". */
    inline uint32_t string_hash(ObjString *s)
    {
        uint32_t h = s->obj.hash;
        if (h == 0)
        {
            h = hash_string(s->chars, s->length);
            s->obj.hash = h;
        }
        return h;
    }

    /* =========================================================
    ** ObjFunc — compiled function (bytecode + constants).
    ** ========================================================= */
    typedef uint32_t Instruction;

    struct ObjFunc
    {
        Obj obj;
        int32_t arity;
        int32_t num_regs;   /* registers needed (computed by the compiler) */
        int32_t code_count;
        int32_t code_capacity;
        int32_t const_count;
        int32_t const_capacity;
        Instruction *code;  /* bytecode array (fixed after compilation) */
        int32_t *lines;     /* lines[i] = source line of instruction i */
        Value *constants;   /* constant pool */
        ObjString *name;    /* function name (debug) */
        ObjString *source;  /* source file (debug) */
    };

    inline bool is_func(Value v) { return is_obj_type(v, OBJ_FUNC); }
    inline ObjFunc *as_func(Value v) { return (ObjFunc *)v.as.obj; }

    /* =========================================================
    ** ObjNative — registered C++ function.
    **   args[0..nargs-1] are the arguments; write the result to args[0]
    **   and return 1 (0 = no result, <0 = a runtime error was raised).
    ** ========================================================= */
    typedef int (*NativeFn)(VM *vm, Value *args, int nargs);

    struct ObjNative
    {
        Obj obj;
        NativeFn fn;
        int32_t arity; /* -1 = variadic */
        ObjString *name;
    };

    inline bool is_native(Value v) { return is_obj_type(v, OBJ_NATIVE); }
    inline ObjNative *as_native(Value v) { return (ObjNative *)v.as.obj; }

    /* =========================================================
    ** ObjArray — dynamic array of Values.
    ** ========================================================= */
    struct ObjArray
    {
        Obj obj;
        Value *data;    /* buffer start */
        Value *end;     /* data + count */
        Value *cap_end; /* data + capacity */
    };

#define arr_count(a) ((int32_t)((a)->end - (a)->data))
#define arr_capacity(a) ((int32_t)((a)->cap_end - (a)->data))

    inline bool is_array(Value v) { return is_obj_type(v, OBJ_ARRAY); }
    inline ObjArray *as_array(Value v) { return (ObjArray *)v.as.obj; }

    /* =========================================================
    ** ObjBuffer — typed byte buffer (Blitz banks, typed arrays).
    ** ========================================================= */
    enum BufferType : uint8_t
    {
        BUF_INT8 = 0,
        BUF_INT16,
        BUF_INT32,
        BUF_UINT8,
        BUF_UINT16,
        BUF_UINT32,
        BUF_FLOAT32,
        BUF_FLOAT64,
    };

    static const int buffer_elem_size[] = {1, 2, 4, 1, 2, 4, 4, 8};

    struct ObjBuffer
    {
        Obj obj;
        BufferType btype;
        uint8_t _pad[3];
        int32_t count;    /* number of elements */
        int32_t capacity; /* allocated elements */
        uint8_t *data;    /* raw byte buffer */
    };

    inline bool is_buffer(Value v) { return is_obj_type(v, OBJ_BUFFER); }
    inline ObjBuffer *as_buffer(Value v) { return (ObjBuffer *)v.as.obj; }

    /* =========================================================
    ** ObjStructDef / ObjStruct — plain record types (Blitz `Type`).
    ** Fields are accessed by index (OP_GETFIELD_IDX / OP_SETFIELD_IDX);
    ** identity comparison, no methods.
    ** ========================================================= */
    struct ObjStructDef
    {
        Obj obj;
        ObjString *name;
        int32_t num_fields;
        ObjString **field_names;
    };

    struct ObjStruct
    {
        Obj obj;
        ObjStructDef *def;
        Value *fields; /* num_fields Values */
    };

    inline bool is_struct_def(Value v) { return is_obj_type(v, OBJ_STRUCT_DEF); }
    inline bool is_struct(Value v) { return is_obj_type(v, OBJ_STRUCT); }
    inline ObjStruct *as_struct(Value v) { return (ObjStruct *)v.as.obj; }
    inline ObjStructDef *as_struct_def(Value v) { return (ObjStructDef *)v.as.obj; }

    /* =========================================================
    ** Fiber — an execution context (register stack + call frames).
    ** Not a GC object: the VM owns it and marks its contents as roots.
    ** The interpreter can stop at a native call (VM::request_suspend) and
    ** continue later from the saved ip — that is how a blocking Blitz main
    ** loop yields to a browser frame.
    ** ========================================================= */
    enum FiberState : uint8_t
    {
        FIBER_READY = 0,
        FIBER_RUNNING,
        FIBER_SUSPENDED,
        FIBER_DONE,
        FIBER_ERROR,
    };

    struct CallFrame
    {
        ObjFunc *func;   /* function being executed */
        Instruction *ip; /* instruction pointer */
        Value *base;     /* base of this frame's registers */
        int ret_reg;     /* caller register where results go */
        int ret_count;   /* results the caller expects */
    };

    struct Fiber
    {
        FiberState state;
        Value *stack;
        int32_t stack_capacity;
        Value *stack_top;
        CallFrame *frames;
        int32_t frame_count;
        int32_t frame_capacity;
    };

} /* namespace zen */

#endif /* ZEN_OBJECT_H */
