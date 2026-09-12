#ifndef ZEN_VALUE_H
#define ZEN_VALUE_H

#include <cmath>
#include "common.h"

namespace zen
{

    /*
    ** Value — tagged union, 16 bytes.
    **
    ** Tipos imediatos (vivem dentro do Value, sem GC):
    **   NIL, BOOL, INT, FLOAT
    **
    ** Tipos heap (apontam para GCObject, geridos pelo GC):
    **   OBJ → ObjString, ObjFunc, ObjArray, ObjMap, ObjClass, ObjInstance
    **
    */

    enum ValueType : uint8_t
    {
        VAL_NIL,
        VAL_BOOL,
        VAL_INT,
        VAL_FLOAT,
        VAL_OBJ, /* qualquer objecto no heap (string, func, array...) */
        VAL_PTR, /* raw pointer (não gerido pelo GC) */
    };

    /* Float predicates without <cmath>: isnan/isinf/isfinite only exist as
    ** std:: names, and libzen does not pull in the STL. */
    inline bool zen_isnan(double d) { return d != d; }
    inline bool zen_isinf(double d) { return d == HUGE_VAL || d == -HUGE_VAL; }
    inline bool zen_isfinite(double d) { return d == d && d != HUGE_VAL && d != -HUGE_VAL; }

    struct Value
    {
        ValueType type;
        union
        {
            bool boolean;
            int64_t integer;
            double number;
            Obj *obj;
            void *pointer;
        } as;
    };

    /* Constructores inline — sem overhead */
    inline Value val_nil()
    {
        Value v;
        v.type = VAL_NIL;
        return v;
    }
    inline Value val_bool(bool b)
    {
        Value v;
        v.type = VAL_BOOL;
        v.as.boolean = b;
        return v;
    }
    inline Value val_int(int64_t i)
    {
        Value v;
        v.type = VAL_INT;
        v.as.integer = i;
        return v;
    }
    inline Value val_float(double d)
    {
        Value v;
        v.type = VAL_FLOAT;
        v.as.number = d;
        return v;
    }
    inline Value val_obj(Obj *o)
    {
        Value v;
        v.type = VAL_OBJ;
        v.as.obj = o;
        return v;
    }
    inline Value val_ptr(void *p)
    {
        Value v;
        v.type = VAL_PTR;
        v.as.pointer = p;
        return v;
    }

    /* Type checks */
    inline bool is_nil(Value v) { return v.type == VAL_NIL; }
    inline bool is_bool(Value v) { return v.type == VAL_BOOL; }
    inline bool is_int(Value v) { return v.type == VAL_INT; }
    inline bool is_float(Value v) { return v.type == VAL_FLOAT; }
    inline bool is_obj(Value v) { return v.type == VAL_OBJ; }
    inline bool is_ptr(Value v) { return v.type == VAL_PTR; }
    inline void *as_ptr(Value v) { return v.as.pointer; }

    /* Truthiness — nil e false são falsy, tudo o resto truthy */
    inline bool is_truthy(Value v)
    {
        if (v.type == VAL_NIL)
            return false;
        if (v.type == VAL_BOOL)
            return v.as.boolean;
        return true;
    }

    /* Conversão numérica */
    inline double to_number(Value v)
    {
        if (v.type == VAL_INT)
            return (double)v.as.integer;
        if (v.type == VAL_FLOAT)
            return v.as.number;
        return 0.0;
    }

    /* Conversão para inteiro (bitwise ops) */
    inline int64_t to_integer(Value v)
    {
        if (v.type == VAL_INT)
            return v.as.integer;
        if (v.type == VAL_FLOAT)
            return (int64_t)v.as.number;
        if (v.type == VAL_BOOL)
            return v.as.boolean ? 1 : 0;
        return 0;
    }

    /* Content-based equality for objects (strings compared by value) */
    bool objects_equal(Obj *a, Obj *b);

    /* Comparação de igualdade — fast path for int (most common in tables) */
    inline bool values_equal(Value a, Value b)
    {
        if (a.type == b.type) {
            if (ZEN_LIKELY(a.type == VAL_INT))
                return a.as.integer == b.as.integer;
            switch (a.type)
            {
            case VAL_NIL:
                return true;
            case VAL_BOOL:
                return a.as.boolean == b.as.boolean;
            case VAL_INT:
                ZEN_UNREACHABLE();
            case VAL_FLOAT:
                return a.as.number == b.as.number;
            case VAL_OBJ:
                return objects_equal(a.as.obj, b.as.obj);
            case VAL_PTR:
                return a.as.pointer == b.as.pointer;
            }
            return false;
        }
        /* Mixed int/float: compare numerically (like Lua) */
        if (a.type == VAL_INT && b.type == VAL_FLOAT) {
            return (double)a.as.integer == b.as.number;
        }
        if (a.type == VAL_FLOAT && b.type == VAL_INT) {
            return a.as.number == (double)b.as.integer;
        }
        return false;
    }

} /* namespace zen */

#endif /* ZEN_VALUE_H */
