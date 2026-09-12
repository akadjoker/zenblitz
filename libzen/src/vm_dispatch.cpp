#ifdef ZEN_OPCODE_PROFILE
#include <x86intrin.h>
#include <cstdlib>
#include <cstdio>
#endif
#include "vm.h"
#include "debug.h"
#include "name_tables.h"
#include "zenconf.h"
#include <cmath>
#include <ctime>

/* MSVC compatibility: __builtin_expect is GCC/Clang only */
#ifdef _MSC_VER
#define __builtin_expect(expr, val) (expr)
#endif

namespace zen
{
#ifdef ZEN_OPCODE_PROFILE
    /* Per-opcode cycle profile (build with -DZEN_OPCODE_PROFILE). Every
    ** dispatch charges the cycles since the previous one to the opcode that
    ** just ran, so the table shows where the interpreter's time goes without
    ** perf or valgrind. rdtsc adds a constant per dispatch, so compare
    ** opcodes against each other, never against wall-clock. */
    static uint64_t g_prof_cycles[256];
    static uint64_t g_prof_count[256];
    static int g_prof_prev = -1;
    static uint64_t g_prof_t0 = 0;
    static bool g_prof_registered = false;

    static void zen_prof_dump()
    {
        int order[256];
        int n = 0;
        uint64_t total = 0;
        for (int i = 0; i < 256; i++)
        {
            if (g_prof_count[i])
            {
                order[n++] = i;
                total += g_prof_cycles[i];
            }
        }
        for (int a = 1; a < n; a++) /* insertion sort by cycles, descending */
        {
            int key = order[a], b = a - 1;
            while (b >= 0 && g_prof_cycles[order[b]] < g_prof_cycles[key])
            {
                order[b + 1] = order[b];
                b--;
            }
            order[b + 1] = key;
        }
        fprintf(stderr, "\n%-18s %12s %14s %7s %8s\n", "opcode", "count", "cycles", "%", "avg");
        for (int k = 0; k < n && k < 40; k++)
        {
            int i = order[k];
            fprintf(stderr, "%-18s %12llu %14llu %6.1f%% %8.1f\n", opcode_name((OpCode)i),
                    (unsigned long long)g_prof_count[i], (unsigned long long)g_prof_cycles[i],
                    100.0 * (double)g_prof_cycles[i] / (double)(total ? total : 1),
                    (double)g_prof_cycles[i] / (double)g_prof_count[i]);
        }
        uint64_t c = 0;
        for (int i = 0; i < 256; i++)
            c += g_prof_count[i];
        fprintf(stderr, "total dispatches: %llu\n", (unsigned long long)c);
    }

    static inline void zen_prof_tick(int op)
    {
        uint64_t now = __rdtsc();
        if (g_prof_prev >= 0)
            g_prof_cycles[g_prof_prev] += now - g_prof_t0;
        else if (!g_prof_registered)
        {
            g_prof_registered = true;
            atexit(zen_prof_dump);
        }
        g_prof_count[op]++;
        g_prof_prev = op;
        g_prof_t0 = now;
    }
#endif


    static inline void copy_native_results(Value *dst, Value *src, int nret, int nresults)
    {
        int wanted = nresults <= 0 ? 0 : nresults;
        int copy_count = nret > 0 ? (nret < wanted ? nret : wanted) : 0;
        for (int j = 0; j < copy_count; j++)
            dst[j] = src[j];
        for (int j = copy_count; j < wanted; j++)
            dst[j] = val_nil();
    }

    static inline const char *val_type_str(Value v)
    {
        switch (v.type)
        {
            case VAL_NIL:   return "nil";
            case VAL_BOOL:  return "bool";
            case VAL_INT:   return "int";
            case VAL_FLOAT: return "float";
            case VAL_OBJ:
                switch (v.as.obj->type)
                {
                    case OBJ_STRING:            return "string";
                    case OBJ_FUNC:              return "function";
                    case OBJ_NATIVE:            return "native function";
                    case OBJ_CLOSURE:           return "function";
                    case OBJ_ARRAY:             return "array";
                    case OBJ_MAP:               return "map";
                    case OBJ_CLASS:             return "class";
                    case OBJ_INSTANCE:          return "instance";
                    case OBJ_NATIVE_STRUCT_DEF: return "struct_def";
                    case OBJ_NATIVE_STRUCT:     return "struct";
                    default:                    return "object";
                }
            default: return "unknown";
        }
    }


    static inline int int_to_cstr(int64_t n, char *buf)
    {
        bool neg = n < 0;
        uint64_t u = neg ? -(uint64_t)n : (uint64_t)n;
        char tmp[21];
        int i = 20;
        tmp[i] = '\0';
        do { tmp[--i] = '0' + (char)(u % 10); u /= 10; } while (u);
        if (neg) tmp[--i] = '-';
        int len = 20 - i;
        memcpy(buf, tmp + i, (size_t)len + 1);
        return len;
    }

    static void dump_value_rec(Value v, int depth)
    {
        static const int MAX_DEPTH = 8;
        auto do_indent = [](int d) {
            for (int i = 0; i < d * 2; i++) putchar(' ');
        };

        if (is_nil(v)) {
            printf("nil");
        } else if (is_bool(v)) {
            printf(v.as.boolean ? "true" : "false");
        } else if (is_int(v)) {
            printf("%lld", (long long)v.as.integer);
        } else if (is_float(v)) {
            printf("%g", v.as.number);
        } else if (is_string(v)) {
            printf("\"%s\"", as_cstring(v));
        } else if (is_array(v)) {
            ObjArray *a = as_array(v);
            int32_t n = arr_count(a);
            if (n == 0) { printf("[]"); return; }
            if (depth >= MAX_DEPTH) { printf("<array[%d]>", n); return; }
            printf("[\n");
            for (int32_t i = 0; i < n; i++) {
                do_indent(depth + 1);
                dump_value_rec(a->data[i], depth + 1);
                printf(",\n");
            }
            do_indent(depth);
            printf("]");
        } else if (is_map(v)) {
            ObjMap *m = as_map(v);
            if (m->count == 0) { printf("{}"); return; }
            if (depth >= MAX_DEPTH) { printf("<map[%d]>", m->count); return; }
            printf("{\n");
            for (int32_t i = 0; i < m->capacity; i++) {
                if (m->nodes[i].hash == 0xFFFFFFFFu) continue;
                do_indent(depth + 1);
                dump_value_rec(m->nodes[i].key, depth + 1);
                printf(": ");
                dump_value_rec(m->nodes[i].value, depth + 1);
                printf(",\n");
            }
            do_indent(depth);
            printf("}");
        } else if (is_set(v)) {
            ObjSet *s = as_set(v);
            if (s->count == 0) { printf("#{}"); return; }
            if (depth >= MAX_DEPTH) { printf("<set[%d]>", s->count); return; }
            printf("#{\n");
            for (int32_t i = 0; i < s->capacity; i++) {
                if (s->nodes[i].hash == 0xFFFFFFFFu) continue;
                do_indent(depth + 1);
                dump_value_rec(s->nodes[i].key, depth + 1);
                printf(",\n");
            }
            do_indent(depth);
            printf("}");
        } else if (is_obj(v)) {
            Obj *obj = v.as.obj;
            switch (obj->type) {
            case OBJ_FUNC:
            case OBJ_CLOSURE:   printf("<function>"); break;
            case OBJ_NATIVE:    printf("<native %s>", ((ObjNative *)obj)->name->chars); break;
            case OBJ_INSTANCE:  printf("<instance %s>", ((ObjInstance *)obj)->klass->name->chars); break;
            case OBJ_CLASS:     printf("<class %s>", ((ObjClass *)obj)->name->chars); break;
            default:            printf("<object>"); break;
            }
        } else {
            printf("?");
        }
    }

    static const char *value_debug_type(Value v)
    {
        switch (v.type)
        {
        case VAL_NIL: return "nil";
        case VAL_BOOL: return "bool";
        case VAL_INT: return "int";
        case VAL_FLOAT: return "float";
        case VAL_PTR: return "ptr";
        case VAL_OBJ:
            if (!v.as.obj) return "obj(null)";
            switch (v.as.obj->type)
            {
            case OBJ_STRING: return "string";
            case OBJ_FUNC: return "function";
            case OBJ_NATIVE: return "native";
            case OBJ_UPVALUE: return "upvalue";
            case OBJ_CLOSURE: return "closure";
            case OBJ_FIBER: return "fiber";
            case OBJ_PROCESS: return "process";
            case OBJ_ARRAY: return "array";
            case OBJ_MAP: return "map";
            case OBJ_SET: return "set";
            case OBJ_BUFFER: return "buffer";
            case OBJ_STRUCT_DEF: return "struct_def";
            case OBJ_STRUCT: return "struct";
            case OBJ_NATIVE_STRUCT_DEF: return "native_struct_def";
            case OBJ_NATIVE_STRUCT: return "native_struct";
            case OBJ_CLASS: return "class";
            case OBJ_INSTANCE: return "instance";
            }
            return "obj(unknown)";
        }
        return "unknown";
    }

    static bool instance_has_method_slot(Value receiver, int slot)
    {
        if (!is_instance(receiver) || slot < 0)
            return false;

        ObjClass *klass = as_instance(receiver)->klass;
        return slot < VM::SLOT_OPERATOR_COUNT && !is_nil(klass->operator_slots[slot]);
    }

    static bool try_binary_operator(VM *vm, Value lhs, Value rhs,
                                    int slot, int reflected_slot,
                                    Value *out)
    {
        if (instance_has_method_slot(lhs, slot))
        {
            Value args[1] = { rhs };
            *out = vm->invoke_operator(lhs, slot, args, 1);
            return true;
        }

        if (reflected_slot >= 0 && instance_has_method_slot(rhs, reflected_slot))
        {
            Value args[1] = { lhs };
            *out = vm->invoke_operator(rhs, reflected_slot, args, 1);
            return true;
        }

        return false;
    }

    static bool try_unary_operator(VM *vm, Value operand, int slot, Value *out)
    {
        if (!instance_has_method_slot(operand, slot))
            return false;

        *out = vm->invoke_operator(operand, slot, nullptr, 0);
        return true;
    }

    static bool try_string_operator(VM *vm, Value receiver, Value *out)
    {
        if (!instance_has_method_slot(receiver, VM::SLOT_STR))
            return false;

        *out = vm->invoke_operator(receiver, VM::SLOT_STR, nullptr, 0);
        return true;
    }

    static Value default_to_string(GC *gc, Value v)
    {
        if (is_string(v))
            return v;

        char buf[64];
        int len = 0;
        if (is_nil(v))
            len = snprintf(buf, sizeof(buf), "nil");
        else if (is_bool(v))
            len = snprintf(buf, sizeof(buf), "%s", v.as.boolean ? "true" : "false");
        else if (is_int(v))
            len = int_to_cstr(v.as.integer, buf);
        else if (is_float(v))
            len = snprintf(buf, sizeof(buf), "%g", v.as.number);
        else
            len = snprintf(buf, sizeof(buf), "<object>");

        return val_obj((Obj *)new_string(gc, buf, len));
    }

    void VM::execute(ObjFiber *fiber)
    {
        /* Cache hot state em locals */
        CallFrame *frame = &fiber->frames[fiber->frame_count - 1];
        Instruction *ip = frame->ip;
        Value *R = frame->base;
        Value *K = frame->func->constants;
        ObjUpvalue **UV = frame->closure ? frame->closure->upvalues : nullptr;
        bool tail_flag = false; /* set by OP_TAILCALL, consumed in the OP_CALL body */

/* Macro para reload após CALL/RETURN (frame mudou) */
#define LOAD_STATE()                                \
    frame = &fiber->frames[fiber->frame_count - 1]; \
    ip = frame->ip;                                 \
    R = frame->base;                                \
    K = frame->func->constants;                     \
    UV = frame->closure ? frame->closure->upvalues : nullptr

#define SAVE_IP() frame->ip = ip

/* Save IP before runtime errors so stack traces report correct lines */
#define RT_ERROR(...)        \
    do                       \
    {                        \
        SAVE_IP();           \
        runtime_error(__VA_ARGS__); \
        return;              \
    } while (0)

/* Aritmética helpers — use int64_t wrapping via unsigned cast to avoid UB */
#define NUM_BINOP(op)                                                         \
    do                                                                        \
    {                                                                         \
        Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];                             \
        if (vb.type == VAL_INT && vc.type == VAL_INT)                         \
            R[ZEN_A(i)] = val_int((int64_t)((uint64_t)vb.as.integer           \
                                                op(uint64_t) vc.as.integer)); \
        else                                                                  \
            R[ZEN_A(i)] = val_float(to_number(vb) op to_number(vc));          \
    } while (0)

        /* =================================================================
        **  DISPATCH SETUP
        ** ================================================================= */

#ifdef ZEN_COMPUTED_GOTO

        /* --- Computed goto (GCC/Clang) --- */
        static const void *const dispatch_table[] = {
            &&lbl_OP_LOADNIL,
            &&lbl_OP_LOADBOOL,
            &&lbl_OP_LOADK,
            &&lbl_OP_LOADI,
            &&lbl_OP_MOVE,
            &&lbl_OP_GETGLOBAL,
            &&lbl_OP_SETGLOBAL,
            &&lbl_OP_ADD,
            &&lbl_OP_SUB,
            &&lbl_OP_MUL,
            &&lbl_OP_DIV,
            &&lbl_OP_MOD,
            &&lbl_OP_IDIV,
            &&lbl_OP_NEG,
            &&lbl_OP_ADD_OBJ,
            &&lbl_OP_SUB_OBJ,
            &&lbl_OP_MUL_OBJ,
            &&lbl_OP_DIV_OBJ,
            &&lbl_OP_MOD_OBJ,
            &&lbl_OP_NEG_OBJ,
            &&lbl_OP_EQ_OBJ,
            &&lbl_OP_LT_OBJ,
            &&lbl_OP_LE_OBJ,
            &&lbl_OP_ADDI,
            &&lbl_OP_SUBI,
            &&lbl_OP_BAND,
            &&lbl_OP_BOR,
            &&lbl_OP_BXOR,
            &&lbl_OP_BNOT,
            &&lbl_OP_SHL,
            &&lbl_OP_SHR,
            &&lbl_OP_EQ,
            &&lbl_OP_LT,
            &&lbl_OP_LE,
            &&lbl_OP_NOT,
            &&lbl_OP_JMP,
            &&lbl_OP_JMPIF,
            &&lbl_OP_JMPIFNOT,
            &&lbl_OP_CALL,
            &&lbl_OP_CALLGLOBAL,
            &&lbl_OP_RETURN,
            &&lbl_OP_CLOSURE,
            &&lbl_OP_GETUPVAL,
            &&lbl_OP_SETUPVAL,
            &&lbl_OP_CLOSE,
            &&lbl_OP_NEWFIBER,
            &&lbl_OP_RESUME,
            &&lbl_OP_YIELD,
            &&lbl_OP_FRAME,
            &&lbl_OP_FRAME_N,
            &&lbl_OP_SPAWN,
            &&lbl_OP_PROC_GET,
            &&lbl_OP_PROC_SET,
            &&lbl_OP_NEWARRAY,
            &&lbl_OP_NEWMAP,
            &&lbl_OP_NEWSET,
            &&lbl_OP_NEWBUFFER,
            &&lbl_OP_APPEND,
            &&lbl_OP_SETADD,
            &&lbl_OP_GETFIELD,
            &&lbl_OP_SETFIELD,
            &&lbl_OP_GETFIELD_IDX,
            &&lbl_OP_SETFIELD_IDX,
            &&lbl_OP_GETINDEX,
            &&lbl_OP_SETINDEX,
            &&lbl_OP_INVOKE,
            &&lbl_OP_INVOKE_VT,
            &&lbl_OP_SUPER_INVOKE,
            &&lbl_OP_CONCAT,
            &&lbl_OP_STRADD,
            &&lbl_OP_TOSTRING,
            &&lbl_OP_TOSTRING_OBJ,
            &&lbl_OP_LEN,
            &&lbl_OP_PRINT,
            &&lbl_OP_SIN,
            &&lbl_OP_COS,
            &&lbl_OP_TAN,
            &&lbl_OP_ASIN,
            &&lbl_OP_ACOS,
            &&lbl_OP_ATAN,
            &&lbl_OP_ATAN2,
            &&lbl_OP_SQRT,
            &&lbl_OP_POW,
            &&lbl_OP_LOG,
            &&lbl_OP_ABS,
            &&lbl_OP_FLOOR,
            &&lbl_OP_CEIL,
            &&lbl_OP_DEG,
            &&lbl_OP_RAD,
            &&lbl_OP_EXP,
            &&lbl_OP_CLOCK,
            &&lbl_OP_LTJMPIFNOT,
            &&lbl_OP_LEJMPIFNOT,
            &&lbl_OP_FORPREP,
            &&lbl_OP_FORLOOP,
            &&lbl_OP_GETFIELD_MUL,
            &&lbl_OP_GETFIELD_SUB,
            &&lbl_OP_ITER_ELEM,
            &&lbl_OP_CONTAINS,
            &&lbl_OP_DELINDEX,
            &&lbl_OP_GETSLICE,
            &&lbl_OP_IS,
            &&lbl_OP_TAILCALL,
            &&lbl_OP_RETURNNIL,
            &&lbl_OP_JMPIFNIL,
            &&lbl_OP_LTIJMPIFNOT,
            &&lbl_OP_GTIJMPIFNOT,
            &&lbl_OP_EQJMPIFNOT,
            &&lbl_OP_NEJMPIFNOT,
            &&lbl_OP_INVOKE_VT_FAST,
            &&lbl_OP_CALL_GENERIC,
            &&lbl_OP_INVOKE_GENERIC,
            &&lbl_OP_HALT,
        };

        /* Indexed by OpCode, like s_opnames in debug.cpp. An opcode added to
        ** the enum without a slot here shifts every later entry, so dispatch
        ** silently jumps to the wrong handler — no crash at the edit, just a
        ** VM that runs the wrong instruction. s_opnames had this guard and
        ** caught the same mistake; this table did not. */
        static_assert(sizeof(dispatch_table) / sizeof(dispatch_table[0]) == (size_t)OP_HALT + 1,
                      "dispatch_table is out of sync with the OpCode enum");

#ifdef ZEN_OPCODE_PROFILE
#define DISPATCH()                         \
    do                                     \
    {                                      \
        zen_prof_tick(ZEN_OP(*ip));        \
        goto *dispatch_table[ZEN_OP(*ip)]; \
    } while (0)
#else
#define DISPATCH() goto *dispatch_table[ZEN_OP(*ip)]
#endif
#define CASE(op) lbl_##op:
#define NEXT()      \
    do              \
    {               \
        ++ip;       \
        DISPATCH(); \
    } while (0)

        DISPATCH();

#else

/* --- Switch (portável) --- */
#define DISPATCH() continue
#define CASE(op) case op:
#define NEXT()    \
    do            \
    {             \
        ++ip;     \
        continue; \
    } while (0)

        for (;;)
        {

#define SWITCH_TOP       \
    switch (ZEN_OP(*ip)) \
    {
            SWITCH_TOP

#endif

        /* =================================================================
        **  OPCODES
        ** ================================================================= */

        CASE(OP_LOADNIL)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_nil();
            NEXT();
        }

        CASE(OP_LOADBOOL)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_bool(ZEN_B(i) != 0);
            if (ZEN_C(i))
                ++ip; /* skip next if C */
            NEXT();
        }

        CASE(OP_LOADK)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = K[ZEN_BX(i)];
            NEXT();
        }

        CASE(OP_LOADI)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_int(ZEN_SBX(i));
            NEXT();
        }

        CASE(OP_MOVE)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = R[ZEN_B(i)];
            NEXT();
        }

        CASE(OP_GETGLOBAL)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = globals_[ZEN_BX(i)];
            NEXT();
        }

        CASE(OP_SETGLOBAL)
        {
            uint32_t i = *ip;
            globals_[ZEN_BX(i)] = R[ZEN_A(i)];
            NEXT();
        }

        /* --- Aritmética --- */
        CASE(OP_ADD)
        {
            uint32_t i = *ip;
            Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];
            if (__builtin_expect(!is_obj(vb) && !is_obj(vc), 1))
            {
                /* Fast path: numerics (int/float) — no objects involved */
                NUM_BINOP(+);
            }
            else if (is_string(vb) && is_string(vc))
            {
                /* Fix: usa string_append_inplace para aproveitar capacidade extra */
                R[ZEN_A(i)] = val_obj((Obj *)string_append_inplace(&gc_, as_string(vb), as_string(vc)));
            }
            else if (is_instance(vb) || is_instance(vc))
            {
                /* Object operator overload + __str__ fallback */
                Value result;
                SAVE_IP();
                if (try_binary_operator(this, vb, vc, SLOT_ADD, SLOT_RADD, &result))
                {
                    if (had_error_) return;
                    LOAD_STATE();
                    R[ZEN_A(i)] = result;
                }
                else
                {
                    LOAD_STATE();
                    /* Instance in string context: coerce via __str__.
                       Fix: a naive fix here once parked each coercion result
                       into a register to make it a GC root — but ZEN_A(i)/
                       ZEN_C(i) are NOT reliably scratch: binary()'s dest<0
                       path (compiler_expressions.cpp) deliberately returns a
                       bare local/parameter's OWN register instead of
                       MOVE-ing it into a temp, so C can be a live user
                       variable's home register — writing to it there
                       silently corrupted that variable. Pausing the GC for
                       this whole two-sided coercion instead: no allocation
                       in this window can trigger a collection, so neither
                       coercion result ever needs to be a root, and no
                       register gets written that the compiler didn't
                       already intend for this instruction. */
                    Value sv = vb, sc = vc;
                    gc_pause(&gc_);
                    if (!is_string(sv))
                    {
                        Value str_result;
                        if (try_string_operator(this, sv, &str_result))
                            sv = str_result;
                        else
                            sv = default_to_string(&gc_, sv);
                        if (had_error_) { gc_resume(&gc_); return; }
                        LOAD_STATE();
                    }
                    if (!is_string(sc))
                    {
                        Value str_result;
                        if (try_string_operator(this, sc, &str_result))
                            sc = str_result;
                        else
                            sc = default_to_string(&gc_, sc);
                        if (had_error_) { gc_resume(&gc_); return; }
                        LOAD_STATE();
                    }
                    /* Fix: string_append_inplace em vez de new_string_concat */
                    R[ZEN_A(i)] = val_obj((Obj *)string_append_inplace(&gc_, as_string(sv), as_string(sc)));
                    gc_resume(&gc_);
                }
            }
            else if (is_string(vb) || is_string(vc))
            {
                /* Mixed string + primitive: coerce to string and concat */
                char buf[64], buf2[64];
                const char *sa; int la;
                const char *sb; int lb;
                if (is_string(vb)) { sa = as_cstring(vb); la = as_string(vb)->length; }
                else if (is_int(vb)) { la = int_to_cstr(vb.as.integer, buf); sa = buf; }
                else if (is_float(vb)) { la = snprintf(buf, sizeof(buf), "%g", vb.as.number); sa = buf; }
                else if (is_bool(vb)) { sa = vb.as.boolean ? "true" : "false"; la = vb.as.boolean ? 4 : 5; }
                else { sa = "nil"; la = 3; }

                if (is_string(vc)) { sb = as_cstring(vc); lb = as_string(vc)->length; }
                else if (is_int(vc)) { lb = int_to_cstr(vc.as.integer, buf2); sb = buf2; }
                else if (is_float(vc)) { lb = snprintf(buf2, sizeof(buf2), "%g", vc.as.number); sb = buf2; }
                else if (is_bool(vc)) { sb = vc.as.boolean ? "true" : "false"; lb = vc.as.boolean ? 4 : 5; }
                else { sb = "nil"; lb = 3; }

                int len = la + lb;
                char tmp[256];
                char *p = (len <= 256) ? tmp : (char *)malloc(len);
                memcpy(p, sa, la);
                memcpy(p + la, sb, lb);
                R[ZEN_A(i)] = val_obj((Obj *)new_string(&gc_, p, len));
                if (p != tmp) free(p);
            }
            else
            {
                NUM_BINOP(+);
            }
            NEXT();
        }
        CASE(OP_SUB)
        {
            uint32_t i = *ip;
            Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];
            if (__builtin_expect(!is_obj(vb) && !is_obj(vc), 1))
            {
                NUM_BINOP(-);
            }
            else if (is_instance(vb) || is_instance(vc))
            {
                Value result;
                SAVE_IP();
                if (try_binary_operator(this, vb, vc, SLOT_SUB, SLOT_RSUB, &result))
                {
                    if (had_error_) return;
                    LOAD_STATE();
                    R[ZEN_A(i)] = result;
                }
                else
                {
                    LOAD_STATE();
                    NUM_BINOP(-);
                }
            }
            else
            {
                NUM_BINOP(-);
            }
            NEXT();
        }
        CASE(OP_MUL)
        {
            uint32_t i = *ip;
            Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];
            if (__builtin_expect(!is_obj(vb) && !is_obj(vc), 1))
            {
                NUM_BINOP(*);
            }
            else if (is_instance(vb) || is_instance(vc))
            {
                Value result;
                SAVE_IP();
                if (try_binary_operator(this, vb, vc, SLOT_MUL, SLOT_RMUL, &result))
                {
                    if (had_error_) return;
                    LOAD_STATE();
                    R[ZEN_A(i)] = result;
                }
                else
                {
                    LOAD_STATE();
                    NUM_BINOP(*);
                }
            }
            else
            {
                NUM_BINOP(*);
            }
            NEXT();
        }
        CASE(OP_DIV)
        {
            uint32_t i = *ip;
            Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];
            if (__builtin_expect(!is_obj(vb) && !is_obj(vc), 1))
            {
                R[ZEN_A(i)] = val_float(to_number(vb) / to_number(vc));
            }
            else if (is_instance(vb) || is_instance(vc))
            {
                Value result;
                SAVE_IP();
                if (try_binary_operator(this, vb, vc, SLOT_DIV, SLOT_RDIV, &result))
                {
                    if (had_error_) return;
                    LOAD_STATE();
                    R[ZEN_A(i)] = result;
                }
                else
                {
                    LOAD_STATE();
                    R[ZEN_A(i)] = val_float(to_number(vb) / to_number(vc));
                }
            }
            else
            {
                R[ZEN_A(i)] = val_float(to_number(vb) / to_number(vc));
            }
            NEXT();
        }
        CASE(OP_MOD)
        {
            uint32_t i = *ip;
            Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];
            if (__builtin_expect(!is_obj(vb) && !is_obj(vc), 1))
            {
                if (vb.type == VAL_INT && vc.type == VAL_INT)
                {
                    int64_t divisor = vc.as.integer;
                    if (divisor == 0)
                        R[ZEN_A(i)] = val_int(0);
                    else
                        R[ZEN_A(i)] = val_int(vb.as.integer % divisor);
                }
                else
                {
                    double a = to_number(vb), b = to_number(vc);
                    if (b == 0.0)
                        R[ZEN_A(i)] = val_float(__builtin_nan(""));
                    else
                        R[ZEN_A(i)] = val_float(a - (int64_t)(a / b) * b);
                }
            }
            else if (is_instance(vb) || is_instance(vc))
            {
                Value result;
                SAVE_IP();
                if (try_binary_operator(this, vb, vc, SLOT_MOD, SLOT_RMOD, &result))
                {
                    if (had_error_) return;
                    LOAD_STATE();
                    R[ZEN_A(i)] = result;
                }
                else
                {
                    LOAD_STATE();
                    double a = to_number(vb), b = to_number(vc);
                    if (b == 0.0)
                        R[ZEN_A(i)] = val_float(__builtin_nan(""));
                    else
                        R[ZEN_A(i)] = val_float(a - (int64_t)(a / b) * b);
                }
            }
            else
            {
                double a = to_number(vb), b = to_number(vc);
                if (b == 0.0)
                    R[ZEN_A(i)] = val_float(__builtin_nan(""));
                else
                    R[ZEN_A(i)] = val_float(a - (int64_t)(a / b) * b);
            }
            NEXT();
        }
        CASE(OP_IDIV)
        {
            uint32_t i = *ip;
            Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];
            if (vb.type == VAL_INT && vc.type == VAL_INT)
            {
                int64_t divisor = vc.as.integer;
                if (divisor == 0)
                    R[ZEN_A(i)] = val_int(0);
                else
                    R[ZEN_A(i)] = val_int(vb.as.integer / divisor);
            }
            else
            {
                double a = to_number(vb), b = to_number(vc);
                if (b == 0.0)
                    R[ZEN_A(i)] = val_int(0);
                else
                    R[ZEN_A(i)] = val_int((int64_t)(a / b));
            }
            NEXT();
        }
        CASE(OP_NEG)
        {
            uint32_t i = *ip;
            Value v = R[ZEN_B(i)];
            if (__builtin_expect(!is_obj(v), 1))
            {
                if (v.type == VAL_INT)
                    R[ZEN_A(i)] = val_int((int64_t)(-(uint64_t)v.as.integer));
                else
                    R[ZEN_A(i)] = val_float(-to_number(v));
            }
            else if (is_instance(v))
            {
                Value result;
                SAVE_IP();
                if (try_unary_operator(this, v, SLOT_NEG, &result))
                {
                    if (had_error_) return;
                    LOAD_STATE();
                    R[ZEN_A(i)] = result;
                }
                else
                {
                    LOAD_STATE();
                    R[ZEN_A(i)] = val_float(-to_number(v));
                }
            }
            else
            {
                R[ZEN_A(i)] = val_float(-to_number(v));
            }
            NEXT();
        }

        CASE(OP_ADD_OBJ)
        {
            uint32_t i = *ip;
            uint8_t dst = ZEN_A(i);
            Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];
            Value result;
            SAVE_IP();
            if (try_binary_operator(this, vb, vc, SLOT_ADD, SLOT_RADD, &result))
            {
                if (had_error_) return;
                LOAD_STATE();
                R[dst] = result;
                NEXT();
            }
            /* Fallback: string concat with __str__ coercion for instances */
            if (is_string(vb) || is_string(vc) || is_instance(vb) || is_instance(vc))
            {
                /* Coerce both sides to string, calling __str__ on instances.
                   Fix: a naive fix here once parked each coercion result into
                   a register (dst, then C) to make it a GC root — but ZEN_C(i)
                   is NOT reliably scratch: binary()'s dest<0 path
                   (compiler_expressions.cpp) deliberately returns a bare
                   local/parameter's OWN register instead of MOVE-ing it into
                   a temp before choosing OP_ADD_OBJ over OP_ADD, so C can be
                   a live user variable's home register — writing to it there
                   silently corrupted that variable. Pausing the GC for this
                   whole two-sided coercion instead: no allocation in this
                   window can trigger a collection, so neither coercion
                   result ever needs to be a root. */
                Value sv = vb, sc = vc;
                gc_pause(&gc_);
                if (!is_string(sv))
                {
                    Value str_result;
                    if (try_string_operator(this, sv, &str_result))
                        sv = str_result;
                    else
                        sv = default_to_string(&gc_, sv);
                    if (had_error_) { gc_resume(&gc_); return; }
                    LOAD_STATE();
                }
                if (!is_string(sc))
                {
                    Value str_result;
                    if (try_string_operator(this, sc, &str_result))
                        sc = str_result;
                    else
                        sc = default_to_string(&gc_, sc);
                    if (had_error_) { gc_resume(&gc_); return; }
                    LOAD_STATE();
                }
                R[dst] = val_obj((Obj *)string_append_inplace(&gc_, as_string(sv), as_string(sc)));
                gc_resume(&gc_);
            }
            else
            {
                LOAD_STATE();
                NUM_BINOP(+);
            }
            NEXT();
        }
        CASE(OP_SUB_OBJ)
        {
            uint32_t i = *ip;
            uint8_t dst = ZEN_A(i);
            Value result;
            SAVE_IP();
            if (try_binary_operator(this, R[ZEN_B(i)], R[ZEN_C(i)], SLOT_SUB, SLOT_RSUB, &result))
            {
                if (had_error_) return;
                LOAD_STATE();
                R[dst] = result;
                NEXT();
            }
            LOAD_STATE();
            NUM_BINOP(-);
            NEXT();
        }
        CASE(OP_MUL_OBJ)
        {
            uint32_t i = *ip;
            uint8_t dst = ZEN_A(i);
            Value result;
            SAVE_IP();
            if (try_binary_operator(this, R[ZEN_B(i)], R[ZEN_C(i)], SLOT_MUL, SLOT_RMUL, &result))
            {
                if (had_error_) return;
                LOAD_STATE();
                R[dst] = result;
                NEXT();
            }
            LOAD_STATE();
            NUM_BINOP(*);
            NEXT();
        }
        CASE(OP_DIV_OBJ)
        {
            uint32_t i = *ip;
            uint8_t dst = ZEN_A(i);
            Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];
            Value result;
            SAVE_IP();
            if (try_binary_operator(this, vb, vc, SLOT_DIV, SLOT_RDIV, &result))
            {
                if (had_error_) return;
                LOAD_STATE();
                R[dst] = result;
                NEXT();
            }
            LOAD_STATE();
            R[dst] = val_float(to_number(vb) / to_number(vc));
            NEXT();
        }
        CASE(OP_MOD_OBJ)
        {
            uint32_t i = *ip;
            uint8_t dst = ZEN_A(i);
            Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];
            Value result;
            SAVE_IP();
            if (try_binary_operator(this, vb, vc, SLOT_MOD, SLOT_RMOD, &result))
            {
                if (had_error_) return;
                LOAD_STATE();
                R[dst] = result;
                NEXT();
            }
            LOAD_STATE();
            if (vb.type == VAL_INT && vc.type == VAL_INT)
            {
                int32_t divisor = vc.as.integer;
                if (divisor == 0)
                    R[dst] = val_int(0);
                else
                    R[dst] = val_int(vb.as.integer % divisor);
            }
            else
            {
                double a = to_number(vb), b = to_number(vc);
                if (b == 0.0)
                    R[dst] = val_float(__builtin_nan(""));
                else
                    R[dst] = val_float(a - (int64_t)(a / b) * b);
            }
            NEXT();
        }
        CASE(OP_NEG_OBJ)
        {
            uint32_t i = *ip;
            uint8_t dst = ZEN_A(i);
            Value result;
            SAVE_IP();
            if (!try_unary_operator(this, R[ZEN_B(i)], SLOT_NEG, &result))
            {
                /* Stale class hint on a numeric value: negate numerically
                   instead of erroring. */
                LOAD_STATE();
                Value vb = R[ZEN_B(i)];
                if (vb.type == VAL_INT)
                    R[dst] = val_int(-vb.as.integer);
                else
                    R[dst] = val_float(-to_number(vb));
                NEXT();
            }
            if (had_error_) return;
            LOAD_STATE();
            R[dst] = result;
            NEXT();
        }
        CASE(OP_EQ_OBJ)
        {
            uint32_t i = *ip;
            uint8_t dst = ZEN_A(i);
            Value result;
            SAVE_IP();
            if (!try_binary_operator(this, R[ZEN_B(i)], R[ZEN_C(i)], SLOT_EQ, -1, &result))
            {
                LOAD_STATE();
                R[dst] = val_bool(values_equal(R[ZEN_B(i)], R[ZEN_C(i)]));
                NEXT();
            }
            if (had_error_) return;
            LOAD_STATE();
            R[dst] = val_bool(is_truthy(result));
            NEXT();
        }
        CASE(OP_LT_OBJ)
        {
            uint32_t i = *ip;
            uint8_t dst = ZEN_A(i);
            Value result;
            SAVE_IP();
            if (!try_binary_operator(this, R[ZEN_B(i)], R[ZEN_C(i)], SLOT_LT, -1, &result))
            {
                /* Operand wasn't an instance with operator< (e.g. a stale class
                   hint on a numeric/string value). Fall back like OP_LT rather
                   than erroring, so a wrong compile-time hint stays harmless. */
                LOAD_STATE();
                Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];
                if (is_string(vb) && is_string(vc))
                    R[dst] = val_bool(strcmp(as_cstring(vb), as_cstring(vc)) < 0);
                else
                    R[dst] = val_bool(to_number(vb) < to_number(vc));
                NEXT();
            }
            if (had_error_) return;
            LOAD_STATE();
            R[dst] = val_bool(is_truthy(result));
            NEXT();
        }
        CASE(OP_LE_OBJ)
        {
            uint32_t i = *ip;
            uint8_t dst = ZEN_A(i);
            Value result;
            SAVE_IP();
            if (!try_binary_operator(this, R[ZEN_B(i)], R[ZEN_C(i)], SLOT_LE, -1, &result))
            {
                /* See OP_LT_OBJ: fall back to numeric/string instead of erroring
                   when the hinted operand isn't actually an operator-bearing
                   instance. */
                LOAD_STATE();
                Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];
                if (is_string(vb) && is_string(vc))
                    R[dst] = val_bool(strcmp(as_cstring(vb), as_cstring(vc)) <= 0);
                else
                    R[dst] = val_bool(to_number(vb) <= to_number(vc));
                NEXT();
            }
            if (had_error_) return;
            LOAD_STATE();
            R[dst] = val_bool(is_truthy(result));
            NEXT();
        }

        /* --- Superinstructions (immediate) --- */
        CASE(OP_ADDI)
        {
            uint32_t i = *ip;
            Value vb = R[ZEN_B(i)];
            int8_t imm = (int8_t)ZEN_C(i);
            if (vb.type == VAL_INT)
                R[ZEN_A(i)] = val_int((int64_t)((uint64_t)vb.as.integer + (int64_t)imm));
            else if (__builtin_expect(is_string(vb), 0))
            {
                /* Fix: the immediate fold must not silently swallow a string
                   left operand — OP_ADD concatenates "x" + 1 into "x1"; this
                   deoptimizes back to that same behaviour instead of
                   coercing the string to 0.0 via to_number(). */
                char buf[32];
                int lb = int_to_cstr(imm, buf);
                int la = as_string(vb)->length;
                int len = la + lb;
                char tmp[256];
                char *p = (len <= 256) ? tmp : (char *)malloc(len);
                memcpy(p, as_cstring(vb), la);
                memcpy(p + la, buf, lb);
                R[ZEN_A(i)] = val_obj((Obj *)new_string(&gc_, p, len));
                if (p != tmp) free(p);
            }
            else if (__builtin_expect(is_instance(vb), 0))
            {
                /* Fix: same reasoning as the string case above — this fold
                   only fires when the compiler couldn't statically prove vb
                   is numeric (an untyped parameter, e.g.), so a left operand
                   that turns out to be a class instance must still honour
                   __add__/__radd__ instead of silently reading garbage via
                   to_number() (which returns 0.0 for any object). Deoptimize
                   back to OP_ADD's own instance path, immediate re-boxed as
                   an int Value so try_binary_operator sees a normal operand. */
                Value result;
                SAVE_IP();
                if (try_binary_operator(this, vb, val_int(imm), SLOT_ADD, SLOT_RADD, &result))
                {
                    if (had_error_) return;
                    LOAD_STATE();
                    R[ZEN_A(i)] = result;
                }
                else
                {
                    LOAD_STATE();
                    /* No __add__/__radd__: fall back to __str__ concat, same
                       as OP_ADD's own fallback for an instance with no
                       overload — never silently reads the pointer as 0.0. */
                    Value str_result;
                    char buf[32];
                    int lb = int_to_cstr(imm, buf);
                    Value coerced;
                    if (try_string_operator(this, vb, &str_result))
                        coerced = str_result;
                    else
                        coerced = default_to_string(&gc_, vb);
                    if (had_error_) return;
                    LOAD_STATE();
                    int la = as_string(coerced)->length;
                    int len = la + lb;
                    char tmp[256];
                    char *p = (len <= 256) ? tmp : (char *)malloc(len);
                    memcpy(p, as_cstring(coerced), la);
                    memcpy(p + la, buf, lb);
                    R[ZEN_A(i)] = val_obj((Obj *)new_string(&gc_, p, len));
                    if (p != tmp) free(p);
                }
            }
            else
                R[ZEN_A(i)] = val_float(to_number(vb) + imm);
            NEXT();
        }
        CASE(OP_SUBI)
        {
            uint32_t i = *ip;
            Value vb = R[ZEN_B(i)];
            int8_t imm = (int8_t)ZEN_C(i);
            if (vb.type == VAL_INT)
                R[ZEN_A(i)] = val_int((int64_t)((uint64_t)vb.as.integer - (int64_t)imm));
            else if (__builtin_expect(is_instance(vb), 0))
            {
                /* Fix: mirror OP_SUB's own instance path — __sub__/__rsub__
                   first, numeric fallback (never string concat: '-' has no
                   string form, matching OP_SUB) if there's no overload. */
                Value result;
                SAVE_IP();
                if (try_binary_operator(this, vb, val_int(imm), SLOT_SUB, SLOT_RSUB, &result))
                {
                    if (had_error_) return;
                    LOAD_STATE();
                    R[ZEN_A(i)] = result;
                }
                else
                {
                    LOAD_STATE();
                    R[ZEN_A(i)] = val_float(to_number(vb) - imm);
                }
            }
            else
                R[ZEN_A(i)] = val_float(to_number(vb) - imm);
            NEXT();
        }

        /* --- Bitwise --- */
        CASE(OP_BAND)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_int(to_integer(R[ZEN_B(i)]) & to_integer(R[ZEN_C(i)]));
            NEXT();
        }
        CASE(OP_BOR)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_int(to_integer(R[ZEN_B(i)]) | to_integer(R[ZEN_C(i)]));
            NEXT();
        }
        CASE(OP_BXOR)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_int(to_integer(R[ZEN_B(i)]) ^ to_integer(R[ZEN_C(i)]));
            NEXT();
        }
        CASE(OP_BNOT)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_int(~to_integer(R[ZEN_B(i)]));
            NEXT();
        }
        CASE(OP_SHL)
        {
            uint32_t i = *ip;
            int64_t val = to_integer(R[ZEN_B(i)]);
            int shift = (int)(to_integer(R[ZEN_C(i)]) & 63);
            R[ZEN_A(i)] = val_int((int64_t)((uint64_t)val << shift));
            NEXT();
        }
        CASE(OP_SHR)
        {
            uint32_t i = *ip;
            int64_t val = to_integer(R[ZEN_B(i)]);
            int shift = (int)(to_integer(R[ZEN_C(i)]) & 63);
            /* Arithmetic right shift (sign-preserving) — portable implementation */
            R[ZEN_A(i)] = val_int((int64_t)(
                ((uint64_t)val >> shift) | (val < 0 ? ~(~(uint64_t)0 >> shift) : 0)));
            NEXT();
        }

        /* --- Comparação --- */
        CASE(OP_EQ)
        {
            uint32_t i = *ip;
            Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];
            if (__builtin_expect(is_instance(vb) || is_instance(vc), 0))
            {
                Value result;
                SAVE_IP();
                if (try_binary_operator(this, vb, vc, SLOT_EQ, SLOT_EQ, &result))
                {
                    if (had_error_) return;
                    LOAD_STATE();
                    R[ZEN_A(i)] = val_bool(is_truthy(result));
                }
                else
                {
                    LOAD_STATE();
                    R[ZEN_A(i)] = val_bool(values_equal(vb, vc));
                }
            }
            else
            {
                R[ZEN_A(i)] = val_bool(values_equal(vb, vc));
            }
            NEXT();
        }
        CASE(OP_LT)
        {
            uint32_t i = *ip;
            Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];
            if (__builtin_expect(is_instance(vb) || is_instance(vc), 0))
            {
                Value result;
                SAVE_IP();
                if (try_binary_operator(this, vb, vc, SLOT_LT, SLOT_LT, &result))
                {
                    if (had_error_) return;
                    LOAD_STATE();
                    R[ZEN_A(i)] = val_bool(is_truthy(result));
                }
                else
                {
                    LOAD_STATE();
                    R[ZEN_A(i)] = val_bool(to_number(vb) < to_number(vc));
                }
            }
            else if (is_string(vb) && is_string(vc))
                R[ZEN_A(i)] = val_bool(strcmp(as_cstring(vb), as_cstring(vc)) < 0);
            else if (vb.type == VAL_INT && vc.type == VAL_INT)
                R[ZEN_A(i)] = val_bool(vb.as.integer < vc.as.integer);
            else
                R[ZEN_A(i)] = val_bool(to_number(vb) < to_number(vc));
            NEXT();
        }
        CASE(OP_LE)
        {
            uint32_t i = *ip;
            Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];
            if (__builtin_expect(is_instance(vb) || is_instance(vc), 0))
            {
                Value result;
                SAVE_IP();
                if (try_binary_operator(this, vb, vc, SLOT_LE, SLOT_LE, &result))
                {
                    if (had_error_) return;
                    LOAD_STATE();
                    R[ZEN_A(i)] = val_bool(is_truthy(result));
                }
                else
                {
                    LOAD_STATE();
                    R[ZEN_A(i)] = val_bool(to_number(vb) <= to_number(vc));
                }
            }
            else if (is_string(vb) && is_string(vc))
                R[ZEN_A(i)] = val_bool(strcmp(as_cstring(vb), as_cstring(vc)) <= 0);
            else if (vb.type == VAL_INT && vc.type == VAL_INT)
                R[ZEN_A(i)] = val_bool(vb.as.integer <= vc.as.integer);
            else
                R[ZEN_A(i)] = val_bool(to_number(vb) <= to_number(vc));
            NEXT();
        }
        CASE(OP_NOT)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_bool(!is_truthy(R[ZEN_B(i)]));
            NEXT();
        }

        /* --- Jumps --- */
        CASE(OP_JMP)
        {
            uint32_t i = *ip;
            ip += ZEN_SBX(i);
            NEXT();
        }
        CASE(OP_JMPIF)
        {
            uint32_t i = *ip;
            if (is_truthy(R[ZEN_A(i)]))
                ip += ZEN_SBX(i);
            NEXT();
        }
        CASE(OP_JMPIFNOT)
        {
            uint32_t i = *ip;
            if (!is_truthy(R[ZEN_A(i)]))
                ip += ZEN_SBX(i);
            NEXT();
        }

        /* --- Funções --- */
        CASE(OP_TAILCALL)
            /* `return f(args)` in tail position. Falls into OP_CALL with the tail
               flag set — only the script-closure branch differs (reuse frame). */
            tail_flag = true;
            goto call_shared;
        CASE(OP_CALL)
            tail_flag = false;
        call_shared:
        {
            uint32_t i = *ip;
            int a = ZEN_A(i);
            int nargs = ZEN_B(i);
            int nresults = ZEN_C(i);
            ++ip;
            SAVE_IP();

            Value callee = R[a];
            if (is_closure(callee))
            {
                /* Script closure — hot path */
                ObjClosure *cl = as_closure(callee);
                ObjFunc *fn = cl->func;

                /* A generic function called without <...> must not silently
                ** treat its first value argument(s) as the type parameter(s):
                ** OP_CALL_GENERIC is the only opcode that knows how to split
                ** the two. Covers OP_TAILCALL too — it shares this body. */
                if (fn->generic_arity > 0)
                {
                    RT_ERROR("'%s' is generic and must be called with <...> type arguments",
                             fn->name ? fn->name->chars : "?");
                }

                /* Process? Spawn instead of call */
                if (fn->is_process)
                {
                    int pid = spawn_process(cl, &R[a + 1], nargs);
                    R[a] = val_int(pid);
                    LOAD_STATE();
                    DISPATCH();
                }

                /* Tail call to a script closure: reuse the current frame so the
                   register/frame stacks don't grow → unbounded tail recursion. */
                if (tail_flag)
                {
                    if (fn->arity >= 0 && nargs != fn->arity)
                    {
                        RT_ERROR("expected %d args but got %d", fn->arity, nargs);
                    }
                    if (fiber->open_upvalues && fiber->open_upvalues->location >= frame->base)
                        close_upvalues(fiber, frame->base);
                    Value *nb = frame->base;
                    for (int k = 0; k < nargs; k++)
                        nb[k] = R[a + 1 + k]; /* dst k < src a+1+k → safe */
                    frame->closure = cl;
                    frame->func = fn;
                    frame->ip = fn->code;
                    /* base/ret_reg/ret_count preserved → returns to original caller */
                    fiber->stack_top = nb + fn->num_regs;
                    LOAD_STATE();
                    DISPATCH();
                }

                if (fiber->frame_count >= kMaxFrames ||
                    &R[a + 1] + fn->num_regs > fiber->stack + fiber->stack_capacity)
                {
                    RT_ERROR("stack overflow");
                }
                CallFrame *new_frame = &fiber->frames[fiber->frame_count++];
                new_frame->closure = cl;
                new_frame->func = fn;
                new_frame->ip = fn->code;
                new_frame->base = &R[a + 1];
                new_frame->ret_reg = a;
                new_frame->ret_count = nresults;
                fiber->stack_top = new_frame->base + fn->num_regs;
                LOAD_STATE();
                DISPATCH();
            }
            if (is_native(callee))
            {
                ObjNative *nat = as_native(callee);
                /* Reading `fn` while `generic_fn` is the live union member is
                ** UB, not merely a wrong result — see ObjNative. */
                if (nat->generic_arity > 0)
                {
                    RT_ERROR("'%s' is a generic native function and must be called with <...> type arguments",
                             nat->name ? nat->name->chars : "?");
                }
                int nret = nat->fn(this, &R[a + 1], nargs);
                if (had_error_) return;
                copy_native_results(&R[a], &R[a + 1], nret, nresults);
                DISPATCH();
            }
            if (is_struct_def(callee))
            {
                ObjStructDef *def = as_struct_def(callee);
                gc_pause(&gc_);
                ObjStruct *s = (ObjStruct *)zen_alloc(&gc_, sizeof(ObjStruct));
                s->obj.type = OBJ_STRUCT;
                s->obj.color = gc_.black_val;
                s->obj.hash = 0;
                s->obj.interned = 0;
                s->obj._pad = 0;
                s->obj.gc_next = gc_.objects;
                gc_.objects = (Obj *)s;
                s->def = def;
                s->fields = (Value *)zen_alloc(&gc_, sizeof(Value) * def->num_fields);
                gc_resume(&gc_);
                for (int32_t fi = 0; fi < def->num_fields; fi++)
                    s->fields[fi] = (fi < nargs) ? R[a + 1 + fi] : val_nil();
                R[a] = val_obj((Obj *)s);
                DISPATCH();
            }
            if (is_native_struct_def(callee))
            {
                NativeStructDef *def = as_native_struct_def(callee);
                gc_pause(&gc_);
                ObjNativeStruct *ns = (ObjNativeStruct *)zen_alloc(&gc_, sizeof(ObjNativeStruct));
                ns->obj.type = OBJ_NATIVE_STRUCT;
                ns->obj.color = gc_.black_val;
                ns->obj.hash = 0;
                ns->obj.interned = 0;
                ns->obj._pad = 0;
                ns->obj.gc_next = gc_.objects;
                gc_.objects = (Obj *)ns;
                ns->def = def;
                ns->data = zen_alloc(&gc_, def->struct_size);
                gc_resume(&gc_);
                memset(ns->data, 0, def->struct_size);
                if (def->ctor)
                    def->ctor(this, ns->data, nargs, &R[a + 1]);
                R[a] = val_obj((Obj *)ns);
                DISPATCH();
            }
            if (is_class(callee))
            {
                /* Class call → create instance + call init */
                ObjClass *klass = as_class(callee);

                /* Check if script is allowed to instantiate this class */
                if (!klass->constructable)
                {
                    RT_ERROR("class '%s' cannot be instantiated from script", klass->name->chars);
                }

                ObjInstance *inst = new_instance(&gc_, klass);
                R[a] = val_obj((Obj *)inst);

                /* Call native constructor if the class (or parent) has one */
                ObjClass *ctor_src = klass;
                while (ctor_src && !ctor_src->native_ctor)
                    ctor_src = ctor_src->parent;
                if (ctor_src && ctor_src->native_ctor)
                {
                    inst->native_data = ctor_src->native_ctor(this, nargs, &R[a + 1]);
                }

                /* Look for init method */
                ObjString *s_init = intern_string(&gc_, "init", 4, hash_string("init", 4));

                bool found;
                Value init_method = map_get(klass->methods, val_obj((Obj *)s_init), &found);
                if (!found && klass->parent)
                    init_method = map_get(klass->parent->methods, val_obj((Obj *)s_init), &found);

                if (found && is_closure(init_method))
                {
                    ObjClosure *cl = as_closure(init_method);
                    ObjFunc *fn = cl->func;
                    /* A generic init<T> constructed via plain ClassName(x) —
                    ** same failure mode as a generic function called without
                    ** <...>, just reached through construction. `Foo<T>(x)` is
                    ** itself unsupported (OP_CALL_GENERIC needs a closure
                    ** callee), so this is a rejection, not a redirect. */
                    if (fn->generic_arity > 0)
                    {
                        RT_ERROR("'%s.init' is generic and cannot be constructed — generic constructors are not supported",
                                 klass->name->chars);
                    }
                    /* Check arity (init's arity = user params, self is implicit) */
                    if (fn->arity >= 0 && nargs != fn->arity)
                    {
                        RT_ERROR("init() expects %d args but got %d", fn->arity, nargs);
                    }
                    /* Set up frame: R[a] = instance (self), args at R[a+1..] */
                    if (fiber->frame_count >= kMaxFrames ||
                        &R[a] + fn->num_regs > fiber->stack + fiber->stack_capacity)
                    {
                        RT_ERROR("stack overflow");
                    }
                    CallFrame *new_frame = &fiber->frames[fiber->frame_count++];
                    new_frame->closure = cl;
                    new_frame->func = fn;
                    new_frame->ip = fn->code;
                    new_frame->base = &R[a]; /* self at base[0], args at base[1..] */
                    new_frame->ret_reg = a;
                    new_frame->ret_count = nresults;
                    fiber->stack_top = new_frame->base + fn->num_regs;
                    LOAD_STATE();
                    DISPATCH();
                }
                else if (nargs > 0 && !ctor_src)
                {
                    /* Error only if no init AND no native_ctor handled the args */
                    RT_ERROR("class '%s' has no init() but received %d args", klass->name->chars, nargs);
                }
                /* No init and no args (or native_ctor consumed them) — just return the instance */
                DISPATCH();
            }
            RT_ERROR("attempt to call non-function (got %s)", val_type_str(R[a]));
        }

        CASE(OP_CALL_GENERIC)
        {
            /* 2-word: word1=[OP|base|nargs|nresults] where nargs = ngeneric +
            ** nvalue over contiguous registers, exactly like OP_CALL;
            ** word2 = ngeneric (low 16 bits). */
            uint32_t i = *ip;
            int a = ZEN_A(i);
            int nargs = ZEN_B(i);
            int nresults = ZEN_C(i);
            ++ip;
            int ngeneric = (int)(*ip & 0xFFFF);
            ++ip;
            SAVE_IP();

            Value callee = R[a];
            /* Closure-only: `Foo<T>(x)` construction syntax is not supported
            ** (no generic constructors), and a native free function has no
            ** way to be declared generic today. */
            if (!is_closure(callee))
            {
                RT_ERROR("generic call target must be a script function (got %s)", val_type_str(callee));
            }
            ObjClosure *cl = as_closure(callee);
            ObjFunc *fn = cl->func;

            if (fn->generic_arity == 0)
            {
                RT_ERROR("'%s' is not generic — called with <...> but takes no type arguments",
                         fn->name ? fn->name->chars : "?");
            }
            if (ngeneric != fn->generic_arity)
            {
                RT_ERROR("'%s' expects %d type argument%s but got %d",
                         fn->name ? fn->name->chars : "?", fn->generic_arity,
                         fn->generic_arity == 1 ? "" : "s", ngeneric);
            }
            for (int gi = 0; gi < ngeneric; gi++)
            {
                if (!is_class(R[a + 1 + gi]))
                {
                    RT_ERROR("'%s': type argument %d is not a type",
                             fn->name ? fn->name->chars : "?", gi + 1);
                }
            }

            int nvalue = nargs - ngeneric;
            if (fn->arity >= 0 && nvalue != fn->arity)
            {
                RT_ERROR("%s() expects %d args but got %d",
                         fn->name ? fn->name->chars : "?", fn->arity, nvalue);
            }
            /* A process body is spawned, never called — and it has no way to
            ** receive type arguments once spawned. */
            if (fn->is_process)
            {
                RT_ERROR("'%s' is a process and cannot be called with type arguments",
                         fn->name ? fn->name->chars : "?");
            }

            if (fiber->frame_count >= kMaxFrames ||
                &R[a + 1] + fn->num_regs > fiber->stack + fiber->stack_capacity)
            {
                RT_ERROR("stack overflow");
            }
            CallFrame *new_frame = &fiber->frames[fiber->frame_count++];
            new_frame->closure = cl;
            new_frame->func = fn;
            new_frame->ip = fn->code;
            /* base[0..ngeneric-1] = types, base[ngeneric..] = values */
            new_frame->base = &R[a + 1];
            new_frame->ret_reg = a;
            new_frame->ret_count = nresults;
            fiber->stack_top = new_frame->base + fn->num_regs;
            LOAD_STATE();
            DISPATCH();
        }

        CASE(OP_CALLGLOBAL)
        {
            uint32_t i = *ip;
            int a = ZEN_A(i);
            int nargs = ZEN_B(i);
            int nresults = ZEN_C(i);
            ++ip;
            int gidx = ZEN_BX(*ip); /* word 2: global index */
            ++ip;
            SAVE_IP();

            Value callee = globals_[gidx];
            if (is_closure(callee))
            {
                ObjClosure *cl = as_closure(callee);
                ObjFunc *fn = cl->func;
                if (fn->generic_arity > 0)
                {
                    RT_ERROR("'%s' is generic and must be called with <...> type arguments",
                             fn->name ? fn->name->chars : "?");
                }
                if (fiber->frame_count >= kMaxFrames ||
                    &R[a + 1] + fn->num_regs > fiber->stack + fiber->stack_capacity)
                {
                    RT_ERROR("stack overflow");
                }
                CallFrame *new_frame = &fiber->frames[fiber->frame_count++];
                new_frame->closure = cl;
                new_frame->func = fn;
                new_frame->ip = fn->code;
                new_frame->base = &R[a + 1];
                new_frame->ret_reg = a;
                new_frame->ret_count = nresults;
                fiber->stack_top = new_frame->base + fn->num_regs;
                LOAD_STATE();
                DISPATCH();
            }
            if (is_native(callee))
            {
                ObjNative *nat = as_native(callee);
                if (nat->generic_arity > 0)
                {
                    RT_ERROR("'%s' is a generic native function and must be called with <...> type arguments",
                             nat->name ? nat->name->chars : "?");
                }
                int nret = nat->fn(this, &R[a + 1], nargs);
                if (had_error_) return;
                copy_native_results(&R[a], &R[a + 1], nret, nresults);
                DISPATCH();
            }
            RT_ERROR("attempt to call non-function (got %s)", val_type_str(callee));
        }

        CASE(OP_RETURNNIL)
        {
            /* `return` with no value, and the implicit return the compiler
            ** appends to every function. The returned value is the nil
            ** constant, so the general OP_RETURN work — reading R[a],
            ** counting results, nil-filling — is all known in advance.
            ** Falls through to OP_RETURN's path for the cases that are not
            ** a plain script-to-script return. */
            if (fiber->open_upvalues && fiber->open_upvalues->location >= frame->base)
                close_upvalues(fiber, frame->base);

            if (__builtin_expect(fiber->frame_count > 1 && frame->ret_count == 1 &&
                                     external_call_stop_depth_ < 0, 1))
            {
                int ret_reg = frame->ret_reg;
                fiber->frame_count--;
                CallFrame *caller_frame = &fiber->frames[fiber->frame_count - 1];
                caller_frame->base[ret_reg] = val_nil();
                fiber->stack_top = caller_frame->base + caller_frame->func->num_regs;
                LOAD_STATE();
                DISPATCH();
            }

            /* Anything else — top-level return, a native waiting at a stop
            ** depth, a multi-value caller — takes the general path below.
            ** Upvalues are already closed, so this repeats only the result
            ** handling, with the returned value known to be nil. */
            {
                int ret_reg = frame->ret_reg;
                int ret_count = frame->ret_count;
                fiber->frame_count--;
                if (fiber->frame_count == 0)
                {
                    fiber->state = FIBER_DONE;
                    if (fiber->caller)
                    {
                        fiber->caller->transfer_value = val_nil();
                        fiber->caller->state = FIBER_RUNNING;
                        current_fiber_ = fiber->caller;
                    }
                    return;
                }
                CallFrame *caller_frame = &fiber->frames[fiber->frame_count - 1];
                Value *caller_base = caller_frame->base;
                /* One value produced (nil); nil-fill whatever else was asked for. */
                for (int j = 0; j < ret_count; j++)
                    caller_base[ret_reg + j] = val_nil();
                fiber->stack_top = caller_base + caller_frame->func->num_regs;
                if (external_call_stop_depth_ >= 0 &&
                    fiber->frame_count <= external_call_stop_depth_)
                {
                    return;
                }
                LOAD_STATE();
                DISPATCH();
            }
        }

        CASE(OP_RETURN)
        {
            uint32_t i = *ip;
            int a = ZEN_A(i);
            int nresults = ZEN_B(i);

            /* Close upvalues do frame actual — skip se não há nenhum aberto */
            if (fiber->open_upvalues && fiber->open_upvalues->location >= frame->base)
                close_upvalues(fiber, frame->base);

            /* Copiar resultados para o caller */
            int ret_reg = frame->ret_reg;
            int ret_count = frame->ret_count;

            fiber->frame_count--;
            if (fiber->frame_count == 0)
            {
                /* Retorno do top-level. The return value travels in the
                   FINISHING fiber's transfer_value — that is what OP_RESUME
                   and VM::resume_fiber read after execute() returns (writing
                   it to the caller's slot lost the value and handed back the
                   stale sent value instead). */
                fiber->state = FIBER_DONE;
                fiber->transfer_value = nresults > 0 ? R[a] : val_nil();
                if (fiber->caller)
                {
                    fiber->caller->state = FIBER_RUNNING;
                    current_fiber_ = fiber->caller;
                }
                return;
            }

            /* Copiar resultados */
            CallFrame *caller_frame = &fiber->frames[fiber->frame_count - 1];
            Value *caller_base = caller_frame->base;
            int copy_count = ret_count < 0 ? nresults : ret_count;
            if (copy_count > nresults)
                copy_count = nresults;

            /* Fast path: single return (most common), then void, then multi */
            if (__builtin_expect(copy_count == 1, 1))
            {
                caller_base[ret_reg] = R[a];
            }
            else if (__builtin_expect(copy_count <= 0, 0))
            {
                /* void return — nothing to copy */
            }
            else
            {
                for (int j = 0; j < copy_count; j++)
                    caller_base[ret_reg + j] = R[a + j];
            }
            /* Nil-fill remaining */
            for (int j = copy_count; j < ret_count && ret_count > 0; j++)
            {
                caller_base[ret_reg + j] = val_nil();
            }

            fiber->stack_top = caller_base + caller_frame->func->num_regs;
            if (external_call_stop_depth_ >= 0 && fiber->frame_count <= external_call_stop_depth_)
            {
                return;
            }
            LOAD_STATE();
            DISPATCH();
        }

        /* --- Closures / Upvalues --- */
        CASE(OP_CLOSURE)
        {
            uint32_t i = *ip;
            int a = ZEN_A(i);
            ObjFunc *fn = as_func(K[ZEN_BX(i)]);

            int nuv = fn->upvalue_count;
            ObjClosure *cl = (ObjClosure *)zen_alloc(&gc_, sizeof(ObjClosure));
            cl->obj.type = OBJ_CLOSURE;
            cl->obj.color = gc_.black_val;
            cl->obj.interned = 0;
            cl->obj.hash = 0;
            cl->obj.gc_next = gc_.objects;
            gc_.objects = (Obj *)cl;
            cl->func = fn;
            cl->upvalues = nullptr;
            cl->upvalue_count = 0;
            R[a] = val_obj((Obj *)cl);
            if (nuv > 0)
            {
                cl->upvalues = (ObjUpvalue **)zen_alloc(&gc_, nuv * sizeof(ObjUpvalue *));
                for (int j = 0; j < nuv; j++)
                    cl->upvalues[j] = nullptr;
                cl->upvalue_count = nuv;
                for (int j = 0; j < nuv; j++)
                {
                    UpvalDesc &desc = fn->upval_descs[j];
                    if (desc.is_local)
                    {
                        /* Capture from enclosing frame's registers */
                        cl->upvalues[j] = capture_upvalue(fiber, &R[desc.index]);
                    }
                    else
                    {
                        /* Copy from enclosing closure's upvalue */
                        cl->upvalues[j] = UV[desc.index];
                    }
                }
            }
            else
            {
                cl->upvalue_count = 0;
            }
            NEXT();
        }

        CASE(OP_GETUPVAL)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = *UV[ZEN_B(i)]->location;
            NEXT();
        }

        CASE(OP_SETUPVAL)
        {
            uint32_t i = *ip;
            *UV[ZEN_B(i)]->location = R[ZEN_A(i)];
            NEXT();
        }

        CASE(OP_CLOSE)
        {
            uint32_t i = *ip;
            close_upvalues(fiber, &R[ZEN_A(i)]);
            NEXT();
        }

        /* --- Fibers --- */
        CASE(OP_NEWFIBER)
        {
            uint32_t i = *ip;
            if (!is_closure(R[ZEN_B(i)]))
            {
                RT_ERROR("spawn expects a function");
            }
            ObjClosure *cl = as_closure(R[ZEN_B(i)]);
            SAVE_IP();
            ObjFiber *f = new_fiber(cl, 256);
            if (!f)
                return; /* new_fiber already raised (e.g. a generic body) */
            R[ZEN_A(i)] = val_obj((Obj *)f);
            NEXT();
        }

        CASE(OP_SPAWN)
        {
            uint32_t i = *ip;
            int a = ZEN_A(i);
            int nargs = ZEN_B(i);
            if (!is_closure(R[a]))
            {
                RT_ERROR("spawn expects a process function");
            }
            ObjClosure *cl = as_closure(R[a]);
            int pid = spawn_process(cl, &R[a + 1], nargs);
            R[a] = val_int(pid);
            NEXT();
        }

        CASE(OP_PROC_GET)
        {
            /* R[A] = process[mode].privates[C]
            ** B: 0=self, 1=father, 2=son */
            uint32_t i = *ip;
            int a = ZEN_A(i);
            int mode = ZEN_B(i);
            int field = ZEN_C(i);
            ProcessSlot *target = nullptr;

            /* current_slot() is O(1): the scheduler records the running
            ** slot index. find_slot() is a linear scan over the pool, so
            ** only father/son (which need another process) pay for it. */
            ProcessSlot *self = current_slot();
            if (mode == 0) /* self */
                target = self;
            else if (self) /* father / son */
                target = find_slot(mode == 1 ? self->parent_id : self->last_child_id);

            if (target && field < MAX_PRIVATES)
                R[a] = target->privates[field];
            else
                R[a] = val_nil();
            NEXT();
        }

        CASE(OP_PROC_SET)
        {
            /* process[mode].privates[C] = R[A]
            ** B: 0=self, 1=father, 2=son */
            uint32_t i = *ip;
            int a = ZEN_A(i);
            int mode = ZEN_B(i);
            int field = ZEN_C(i);
            ProcessSlot *target = nullptr;

            /* current_slot() is O(1): the scheduler records the running
            ** slot index. find_slot() is a linear scan over the pool, so
            ** only father/son (which need another process) pay for it. */
            ProcessSlot *self = current_slot();
            if (mode == 0) /* self */
                target = self;
            else if (self) /* father / son */
                target = find_slot(mode == 1 ? self->parent_id : self->last_child_id);

            if (target && field < MAX_PRIVATES)
                target->privates[field] = R[a];
            NEXT();
        }

        CASE(OP_RESUME)
        {
            uint32_t i = *ip;
            if (!is_fiber(R[ZEN_B(i)]))
            {
                RT_ERROR("resume expects a fiber");
            }
            ObjFiber *target = as_fiber(R[ZEN_B(i)]);
            Value send_val = R[ZEN_C(i)];
            ++ip;
            SAVE_IP();

            if (target->state == FIBER_DONE || target->state == FIBER_ERROR)
            {
                /* Dead fiber (finished or died with an error): resume → nil */
                R[ZEN_A(i)] = val_nil();
                DISPATCH();
            }
            if (target->state == FIBER_RUNNING)
            {
                RT_ERROR("cannot resume running fiber");
            }

            target->transfer_value = send_val;
            target->caller = fiber;
            target->state = FIBER_RUNNING;
            fiber->state = FIBER_SUSPENDED;
            current_fiber_ = target;

            /* If resuming a suspended fiber (not first time), write
               transfer_value into the yield_dest register */
            if (target->yield_dest >= 0)
            {
                CallFrame &tf = target->frames[target->frame_count - 1];
                Value *target_regs = tf.base;
                target_regs[target->yield_dest] = send_val;
                target->yield_dest = -1;
            }

            if (fiber_depth_ >= kMaxFiberDepth)
            {
                RT_ERROR("fiber resume depth exceeded");
            }
            ++fiber_depth_;
            execute(target);
            --fiber_depth_;

            /* Error escaped the resumed fiber: it is already FIBER_ERROR
               (runtime_error marks it); restore ourselves and keep unwinding
               so an enclosing pcall can catch it. */
            if (had_error_)
            {
                fiber->state = FIBER_RUNNING;
                current_fiber_ = fiber;
                return;
            }

            /* Voltámos — target fez yield ou terminou */
            fiber->state = FIBER_RUNNING;
            current_fiber_ = fiber;
            R[ZEN_A(i)] = target->transfer_value;
            LOAD_STATE();
            DISPATCH();
        }

        CASE(OP_YIELD)
        {
            uint32_t i = *ip;
            ++ip;
            SAVE_IP();

            if (!fiber->caller)
            {
                RT_ERROR("yield outside of fiber");
            }

            fiber->transfer_value = R[ZEN_B(i)];
            fiber->yield_dest = ZEN_A(i);
            fiber->state = FIBER_SUSPENDED;
            /* Retorna ao execute() do caller (que está em OP_RESUME) */
            return;
        }

        CASE(OP_FRAME)
        {
            ++ip;
            SAVE_IP();
            fiber->frame_speed = 100;
            fiber->state = FIBER_SUSPENDED;
            return;
        }

        CASE(OP_FRAME_N)
        {
            uint32_t i = *ip;
            ++ip;
            SAVE_IP();
            Value fv = R[ZEN_A(i)];
            fiber->frame_speed = (fv.type == VAL_INT) ? fv.as.integer : 100;
            fiber->state = FIBER_SUSPENDED;
            return;
        }

        /* --- Collections --- */
        CASE(OP_NEWARRAY)
        {
            uint32_t i = *ip;
            ObjArray *arr = new_array(&gc_);
            R[ZEN_A(i)] = val_obj((Obj *)arr);
            NEXT();
        }

        CASE(OP_NEWMAP)
        {
            uint32_t i = *ip;
            ObjMap *map = new_map(&gc_);
            R[ZEN_A(i)] = val_obj((Obj *)map);
            NEXT();
        }

        CASE(OP_NEWSET)
        {
            uint32_t i = *ip;
            ObjSet *set = new_set(&gc_);
            R[ZEN_A(i)] = val_obj((Obj *)set);
            NEXT();
        }

        CASE(OP_NEWBUFFER)
        {
            uint32_t i = *ip;
            int a = ZEN_A(i);
            int b = ZEN_B(i);
            BufferType btype = (BufferType)ZEN_C(i);
            Value arg = R[b];
            if (is_int(arg)) {
                int32_t count = arg.as.integer;
                if (count < 0) { RT_ERROR("buffer size must be non-negative"); }
                ObjBuffer *buf = new_buffer(&gc_, btype, count);
                R[a] = val_obj((Obj *)buf);
            } else if (is_array(arg)) {
                ObjArray *src = as_array(arg);
                int32_t count = arr_count(src);
                ObjBuffer *buf = new_buffer(&gc_, btype, count);
                for (int32_t idx = 0; idx < count; idx++) {
                    double v = 0;
                    if (is_int(src->data[idx])) v = (double)src->data[idx].as.integer;
                    else if (is_float(src->data[idx])) v = src->data[idx].as.number;
                    buffer_set(buf, idx, v);
                }
                R[a] = val_obj((Obj *)buf);
            } else {
                RT_ERROR("buffer constructor expects int or array");
            }
            NEXT();
        }

        CASE(OP_APPEND)
        {
            uint32_t i = *ip;
            Value aval = R[ZEN_A(i)];
            if (!is_array(aval))
            {
                RT_ERROR("APPEND expected array: receiver=R%d receiver_type=%s", ZEN_A(i), value_debug_type(aval));
            }
            ObjArray *arr = as_array(aval);
            array_push(&gc_, arr, R[ZEN_B(i)]);
            NEXT();
        }

        CASE(OP_SETADD)
        {
            uint32_t i = *ip;
            Value sval = R[ZEN_A(i)];
            if (!is_set(sval))
            {
                RT_ERROR("SETADD expected set: receiver=R%d receiver_type=%s", ZEN_A(i), value_debug_type(sval));
            }
            ObjSet *set = as_set(sval);
            set_add(&gc_, set, R[ZEN_B(i)]);
            NEXT();
        }

        /* --- Field/Index access --- */
        CASE(OP_GETFIELD)
        {
            /* R[A] = R[B].field_by_name(constants[C]) — pointer compare fallback */
            uint32_t i = *ip;
            Value receiver = R[ZEN_B(i)];
            ObjString *name = as_string(K[ZEN_C(i)]);
            if (is_struct(receiver))
            {
                ObjStruct *s = as_struct(receiver);
                ObjStructDef *def = s->def;
                for (int32_t fi = 0; fi < def->num_fields; fi++)
                {
                    if (def->field_names[fi] == name) /* pointer compare (interned) */
                    {
                        R[ZEN_A(i)] = s->fields[fi];
                        goto getfield_done;
                    }
                }
                RT_ERROR("struct '%s' has no field '%s'", def->name->chars, name->chars);
            }
            if (is_native_struct(receiver))
            {
                ObjNativeStruct *ns = as_native_struct(receiver);
                NativeStructDef *def = ns->def;
                for (int16_t fi = 0; fi < def->num_fields; fi++)
                {
                    if (def->fields[fi].name == name) /* pointer compare (interned) */
                    {
                        uint8_t *ptr = (uint8_t *)ns->data + def->fields[fi].offset;
                        switch (def->fields[fi].type)
                        {
                        case FIELD_BYTE:    R[ZEN_A(i)] = val_int(*(uint8_t *)ptr); break;
                        case FIELD_INT:     R[ZEN_A(i)] = val_int(*(int32_t *)ptr); break;
                        case FIELD_UINT:    R[ZEN_A(i)] = val_int((int64_t)*(uint32_t *)ptr); break;
                        case FIELD_FLOAT:   R[ZEN_A(i)] = val_float(*(float *)ptr); break;
                        case FIELD_DOUBLE:  R[ZEN_A(i)] = val_float(*(double *)ptr); break;
                        case FIELD_BOOL:    R[ZEN_A(i)] = val_bool(*(bool *)ptr); break;
                        case FIELD_POINTER: R[ZEN_A(i)] = val_ptr(*(void **)ptr); break;
                        }
                        goto getfield_done;
                    }
                }
                RT_ERROR("native struct '%s' has no field '%s'", def->name->chars, name->chars);
            }
            if (is_instance(receiver))
            {
                ObjInstance *inst = as_instance(receiver);
                ObjClass *klass = inst->klass;
                for (int32_t fi = 0; fi < klass->num_fields; fi++)
                {
                    if (klass->field_names[fi] == name) /* pointer compare (interned) */
                    {
                        R[ZEN_A(i)] = inst->fields[fi];
                        goto getfield_done;
                    }
                }
                RT_ERROR("instance of '%s' has no field '%s'", klass->name->chars, name->chars);
            }
            /* Static member read: Class.member (only reached for class receivers,
               never for the hot instance/struct paths above). */
            if (is_class(receiver))
            {
                ObjClass *cls = as_class(receiver);
                if (cls->statics)
                {
                    bool found = false;
                    Value sv = map_get(cls->statics, val_obj((Obj *)name), &found);
                    if (found) { R[ZEN_A(i)] = sv; goto getfield_done; }
                }
                RT_ERROR("class '%s' has no static member '%s'", cls->name->chars, name->chars);
            }
            RT_ERROR("cannot access field '%s' on this type", name->chars);
            getfield_done:
            NEXT();
        }
        CASE(OP_SETFIELD)
        {
            /* R[A].field_by_name(constants[B]) = R[C] — pointer compare fallback */
            uint32_t i = *ip;
            Value receiver = R[ZEN_A(i)];
            ObjString *name = as_string(K[ZEN_B(i)]);
            Value val = R[ZEN_C(i)];
            if (is_struct(receiver))
            {
                ObjStruct *s = as_struct(receiver);
                ObjStructDef *def = s->def;
                for (int32_t fi = 0; fi < def->num_fields; fi++)
                {
                    if (def->field_names[fi] == name) /* pointer compare (interned) */
                    {
                        s->fields[fi] = val;
                        goto setfield_done;
                    }
                }
                RT_ERROR("struct '%s' has no field '%s'", def->name->chars, name->chars);
            }
            if (is_native_struct(receiver))
            {
                ObjNativeStruct *ns = as_native_struct(receiver);
                NativeStructDef *def = ns->def;
                for (int16_t fi = 0; fi < def->num_fields; fi++)
                {
                    if (def->fields[fi].name == name) /* pointer compare (interned) */
                    {
                        if (def->fields[fi].read_only)
                        {
                            RT_ERROR("field '%s' is read-only", name->chars);
                        }
                        uint8_t *ptr = (uint8_t *)ns->data + def->fields[fi].offset;
                        switch (def->fields[fi].type)
                        {
                        case FIELD_BYTE:    *(uint8_t *)ptr = (uint8_t)to_integer(val); break;
                        case FIELD_INT:     *(int32_t *)ptr = (int32_t)to_integer(val); break;
                        case FIELD_UINT:    *(uint32_t *)ptr = (uint32_t)to_integer(val); break;
                        case FIELD_FLOAT:   *(float *)ptr = (float)to_number(val); break;
                        case FIELD_DOUBLE:  *(double *)ptr = to_number(val); break;
                        case FIELD_BOOL:    *(bool *)ptr = is_truthy(val); break;
                        case FIELD_POINTER: *(void **)ptr = val.as.pointer; break;
                        }
                        goto setfield_done;
                    }
                }
                RT_ERROR("native struct '%s' has no field '%s'", def->name->chars, name->chars);
            }
            if (is_instance(receiver))
            {
                ObjInstance *inst = as_instance(receiver);
                ObjClass *klass = inst->klass;
                for (int32_t fi = 0; fi < klass->num_fields; fi++)
                {
                    if (klass->field_names[fi] == name) /* pointer compare (interned) */
                    {
                        inst->fields[fi] = val;
                        goto setfield_done;
                    }
                }
                RT_ERROR("instance of '%s' has no field '%s'", klass->name->chars, name->chars);
            }
            /* Static member write: Class.member = val (class receivers only). */
            if (is_class(receiver))
            {
                ObjClass *cls = as_class(receiver);
                if (!cls->statics)
                    cls->statics = new_map(&gc_);
                map_set(&gc_, cls->statics, val_obj((Obj *)name), val);
                goto setfield_done;
            }
            RT_ERROR("cannot set field '%s' on this type", name->chars);
            setfield_done:
            NEXT();
        }
        CASE(OP_GETFIELD_IDX)
        {
            /* R[A] = R[B].fields[C] — O(1) direct index */
            uint32_t i = *ip;
            Value obj = R[ZEN_B(i)];
            const int field_idx = ZEN_C(i);
            if (is_instance(obj))
            {
                ObjInstance *inst = as_instance(obj);
                if (field_idx < 0 || field_idx >= inst->klass->num_fields)
                {
                    RT_ERROR("GETFIELD_IDX out of bounds: receiver=R%d class=%s field_index=%d field_count=%d", ZEN_B(i), inst->klass->name->chars, field_idx, inst->klass->num_fields);
                }
                R[ZEN_A(i)] = inst->fields[field_idx];
            }
            else if (is_struct(obj))
            {
                ObjStruct *st = as_struct(obj);
                if (field_idx < 0 || field_idx >= st->def->num_fields)
                {
                    RT_ERROR("GETFIELD_IDX out of bounds: receiver=R%d struct=%s field_index=%d field_count=%d", ZEN_B(i), st->def->name->chars, field_idx, st->def->num_fields);
                }
                R[ZEN_A(i)] = st->fields[field_idx];
            }
            else
            {
                RT_ERROR("GETFIELD_IDX expected instance/struct: dst=R%d receiver=R%d receiver_type=%s field_index=%d", ZEN_A(i), ZEN_B(i), value_debug_type(obj), field_idx);
            }
            NEXT();
        }
        CASE(OP_SETFIELD_IDX)
        {
            /* R[A].fields[B] = R[C] — O(1) direct index */
            uint32_t i = *ip;
            Value obj = R[ZEN_A(i)];
            const int field_idx = ZEN_B(i);
            if (is_instance(obj))
            {
                ObjInstance *inst = as_instance(obj);
                if (field_idx < 0 || field_idx >= inst->klass->num_fields)
                {
                    RT_ERROR("SETFIELD_IDX out of bounds: receiver=R%d class=%s field_index=%d field_count=%d value=R%d", ZEN_A(i), inst->klass->name->chars, field_idx, inst->klass->num_fields, ZEN_C(i));
                }
                inst->fields[field_idx] = R[ZEN_C(i)];
            }
            else if (is_struct(obj))
            {
                ObjStruct *st = as_struct(obj);
                if (field_idx < 0 || field_idx >= st->def->num_fields)
                {
                    RT_ERROR("SETFIELD_IDX out of bounds: receiver=R%d struct=%s field_index=%d field_count=%d value=R%d", ZEN_A(i), st->def->name->chars, field_idx, st->def->num_fields, ZEN_C(i));
                }
                st->fields[field_idx] = R[ZEN_C(i)];
            }
            else
            {
                RT_ERROR("SETFIELD_IDX expected instance/struct: receiver=R%d receiver_type=%s field_index=%d value=R%d", ZEN_A(i), value_debug_type(obj), field_idx, ZEN_C(i));
            }
            NEXT();
        }
        CASE(OP_GETINDEX)
        {
            uint32_t i = *ip;
            Value container = R[ZEN_B(i)];
            Value key = R[ZEN_C(i)];
            if (is_array(container)) {
                if (!is_int(key)) { RT_ERROR("array index must be integer"); }
                R[ZEN_A(i)] = array_get(as_array(container), key.as.integer);
            } else if (is_map(container)) {
                bool found;
                R[ZEN_A(i)] = map_get(as_map(container), key, &found);
                if (!found) R[ZEN_A(i)] = val_nil();
            } else if (is_buffer(container)) {
                if (!is_int(key)) { RT_ERROR("buffer index must be integer"); }
                ObjBuffer *buf = as_buffer(container);
                int32_t idx = (int32_t)key.as.integer;
                if ((uint32_t)idx >= (uint32_t)buf->count) { RT_ERROR("buffer index out of bounds"); }
                double v = buffer_get(buf, idx);
                /* Float types return float, integer types return int64 */
                if (buf->btype >= BUF_FLOAT32)
                    R[ZEN_A(i)] = val_float(v);
                else
                    R[ZEN_A(i)] = val_int((int64_t)v);
            } else if (is_string(container)) {
                if (!is_int(key)) { RT_ERROR("string index must be integer"); }
                ObjString *s = as_string(container);
                int32_t idx = (int32_t)key.as.integer;
                if ((uint32_t)idx >= (uint32_t)s->length) { RT_ERROR("string index out of bounds"); }
                R[ZEN_A(i)] = val_obj((Obj *)new_string(&gc_, &s->chars[idx], 1));
            } else {
                RT_ERROR("cannot index value");
            }
            NEXT();
        }
        CASE(OP_ITER_ELEM)
        {
            uint32_t i = *ip;
            Value container = R[ZEN_B(i)];
            Value vidx = R[ZEN_C(i)];
            int32_t idx = (int32_t)vidx.as.integer;
            if (is_array(container)) {
                R[ZEN_A(i)] = array_get(as_array(container), idx);
            } else if (is_buffer(container)) {
                ObjBuffer *buf = as_buffer(container);
                if ((uint32_t)idx >= (uint32_t)buf->count) { RT_ERROR("buffer index out of bounds"); }
                double v = buffer_get(buf, idx);
                if (buf->btype >= BUF_FLOAT32)
                    R[ZEN_A(i)] = val_float(v);
                else
                    R[ZEN_A(i)] = val_int((int64_t)v);
            } else if (is_string(container)) {
                ObjString *s = as_string(container);
                if ((uint32_t)idx >= (uint32_t)s->length) { RT_ERROR("string index out of bounds"); }
                R[ZEN_A(i)] = val_obj((Obj *)new_string(&gc_, &s->chars[idx], 1));
            } else if (is_map(container)) {
                ObjMap *map = as_map(container);
                int32_t ord = 0;
                bool found = false;
                for (int32_t n = 0; n < map->capacity; n++) {
                    if (map->nodes[n].hash != 0xFFFFFFFFu) {
                        if (ord == idx) { R[ZEN_A(i)] = map->nodes[n].key; found = true; break; }
                        ord++;
                    }
                }
                if (!found) { RT_ERROR("map iteration index out of bounds"); }
            } else if (is_set(container)) {
                ObjSet *set = as_set(container);
                int32_t ord = 0;
                bool found = false;
                for (int32_t n = 0; n < set->capacity; n++) {
                    if (set->nodes[n].hash != 0xFFFFFFFFu) {
                        if (ord == idx) { R[ZEN_A(i)] = set->nodes[n].key; found = true; break; }
                        ord++;
                    }
                }
                if (!found) { RT_ERROR("set iteration index out of bounds"); }
            } else {
                RT_ERROR("cannot iterate value");
            }
            NEXT();
        }
        CASE(OP_SETINDEX)
        {
            uint32_t i = *ip;
            Value container = R[ZEN_A(i)];
            Value key = R[ZEN_B(i)];
            Value val = R[ZEN_C(i)];
            if (is_array(container)) {
                if (!is_int(key)) { RT_ERROR("array index must be integer"); }
                int32_t idx = key.as.integer;
                ObjArray *arr = as_array(container);
                if ((uint32_t)idx < (uint32_t)arr_count(arr)) {
                    arr->data[idx] = val;
                } else {
                    RT_ERROR("array index out of bounds");
                }
            } else if (is_map(container)) {
                map_set(&gc_, as_map(container), key, val);
            } else if (is_buffer(container)) {
                if (!is_int(key)) { RT_ERROR("buffer index must be integer"); }
                ObjBuffer *buf = as_buffer(container);
                int32_t idx = key.as.integer;
                if ((uint32_t)idx >= (uint32_t)buf->count) { RT_ERROR("buffer index out of bounds"); }
                double v = 0;
                if (is_int(val)) v = (double)val.as.integer;
                else if (is_float(val)) v = val.as.number;
                else { RT_ERROR("buffer only accepts numbers"); }
                buffer_set(buf, idx, v);
            } else {
                RT_ERROR("cannot index value");
            }
            NEXT();
        }

        /* --- OP_INVOKE: method dispatch by receiver type --- */
        /* 2-word instruction: word1=[OP_INVOKE|A|B|C], word2=name_ki */
        /* A=base (receiver at R[A], args at R[A+1]..R[A+B]), result → R[A] */
        CASE(OP_INVOKE)
        {
            uint32_t i = *ip;
            uint8_t base = ZEN_A(i);
            uint8_t arg_count = ZEN_B(i);
            uint32_t word2 = *(++ip); /* packed: (selector_slot << 16) | name_ki */
            uint16_t sel_slot = (uint16_t)(word2 >> 16);
            uint16_t name_ki = (uint16_t)(word2 & 0xFFFF);
            Value receiver = R[base];
            ObjString *method = as_string(K[name_ki]);
            const char *mname = method->chars;
            Value *args = &R[base + 1];

            if (is_array(receiver))
            {
                #include "invoke_array.inl"
            }
            else if (is_string(receiver))
            {
                #include "invoke_string.inl"
            }
            else if (is_map(receiver))
            {
                #include "invoke_map.inl"
            }
            else if (is_set(receiver))
            {
                #include "invoke_set.inl"
            }
            else if (is_buffer(receiver))
            {
                #include "invoke_buffer.inl"
            }
            else if (is_instance(receiver))
            {
                /* Vtable dispatch using compile-time selector slot */
                ObjInstance *inst = as_instance(receiver);
                ObjClass *klass = inst->klass;

                /* Direct vtable lookup — O(1), no hash map walk */
                Value mval = val_nil();
                if (sel_slot < klass->vtable_size)
                    mval = klass->vtable[sel_slot];

                if (is_nil(mval))
                {
                    RT_ERROR("'%s' has no method '%s'", klass->name->chars, mname);
                }

                if (is_closure(mval))
                {
                    ObjClosure *cl = as_closure(mval);
                    ObjFunc *fn = cl->func;
                    /* Same reasoning as OP_CALL: a generic method invoked
                    ** through plain OP_INVOKE (no <...>) must not silently
                    ** bind a value argument into a type-parameter register. */
                    if (fn->generic_arity > 0)
                    {
                        RT_ERROR("'%s.%s' is generic and must be called with <...> type arguments",
                                 klass->name->chars, mname);
                    }
                    if (fn->arity >= 0 && arg_count != fn->arity)
                    {
                        RT_ERROR("%s.%s() expects %d args but got %d", klass->name->chars, mname, fn->arity, arg_count);
                    }
                    if (fiber->frame_count >= kMaxFrames ||
                        &R[base] + fn->num_regs > fiber->stack + fiber->stack_capacity)
                    {
                        RT_ERROR("stack overflow");
                    }
                    ++ip;
                    SAVE_IP();
                    CallFrame *new_frame = &fiber->frames[fiber->frame_count++];
                    new_frame->closure = cl;
                    new_frame->func = fn;
                    new_frame->ip = fn->code;
                    new_frame->base = &R[base]; /* base[0]=self, base[1..]=args */
                    new_frame->ret_reg = base;
                    new_frame->ret_count = 1;
                    fiber->stack_top = new_frame->base + fn->num_regs;
                    LOAD_STATE();
                    DISPATCH();
                }
                else if (is_native(mval))
                {
                    ObjNative *nat = as_native(mval);
                    if (nat->generic_arity > 0)
                    {
                        RT_ERROR("'%s.%s' is a generic native method and must be called with <...> type arguments",
                                 klass->name->chars, mname);
                    }
                    int nret = nat->fn(this, &R[base], arg_count + 1); /* +1 for self */
                    if (nret > 0)
                        R[base] = R[base];
                    else
                        R[base] = val_nil();
                }
                else
                {
                    RT_ERROR("'%s.%s' is not callable", klass->name->chars, mname);
                }
            }
            else if (is_class(receiver))
            {
                /* Static method call: Class.method(args) — no self. */
                ObjClass *cls = as_class(receiver);
                bool found = false;
                Value mval = cls->statics ? map_get(cls->statics, val_obj((Obj *)method), &found)
                                          : val_nil();
                if (!found || is_nil(mval))
                {
                    RT_ERROR("class '%s' has no static method '%s'", cls->name->chars, mname);
                }
                if (is_closure(mval))
                {
                    ObjClosure *cl = as_closure(mval);
                    ObjFunc *fn = cl->func;
                    if (fn->generic_arity > 0)
                    {
                        RT_ERROR("'%s.%s' is generic and must be called with <...> type arguments",
                                 cls->name->chars, mname);
                    }
                    if (fn->arity >= 0 && arg_count != fn->arity)
                    {
                        RT_ERROR("%s.%s() expects %d args but got %d", cls->name->chars, mname, fn->arity, arg_count);
                    }
                    if (fiber->frame_count >= kMaxFrames ||
                        &R[base + 1] + fn->num_regs > fiber->stack + fiber->stack_capacity)
                    {
                        RT_ERROR("stack overflow");
                    }
                    ++ip;
                    SAVE_IP();
                    CallFrame *new_frame = &fiber->frames[fiber->frame_count++];
                    new_frame->closure = cl;
                    new_frame->func = fn;
                    new_frame->ip = fn->code;
                    new_frame->base = &R[base + 1]; /* no self: params start at base+1 */
                    new_frame->ret_reg = base;      /* result overwrites the class reg */
                    new_frame->ret_count = 1;
                    fiber->stack_top = new_frame->base + fn->num_regs;
                    LOAD_STATE();
                    DISPATCH();
                }
                else if (is_native(mval))
                {
                    ObjNative *nat = as_native(mval);
                    if (nat->generic_arity > 0)
                    {
                        RT_ERROR("'%s.%s' is a generic native method and must be called with <...> type arguments",
                                 cls->name->chars, mname);
                    }
                    int nret = nat->fn(this, &R[base + 1], arg_count);
                    R[base] = (nret > 0) ? R[base + 1] : val_nil();
                }
                else
                {
                    RT_ERROR("'%s.%s' is not callable", cls->name->chars, mname);
                }
            }
            else
            {
                RT_ERROR("cannot invoke method '%s' on this type", mname);
            }
            NEXT();
        }

        CASE(OP_INVOKE_VT)
        {
            /* Single-word vtable dispatch: A=base, B=arg_count, C=slot_idx */
            uint32_t i = *ip;
            uint8_t base = ZEN_A(i);
            uint8_t arg_count = ZEN_B(i);
            uint8_t slot = ZEN_C(i);

            Value recv = R[base];
            if (!is_instance(recv))
            {
                RT_ERROR("INVOKE_VT expected instance: receiver=R%d receiver_type=%s slot=%d", base, value_debug_type(recv), slot);
            }
            ObjInstance *inst = as_instance(recv);
            ObjClass *klass = inst->klass;
            if (slot < 0 || slot >= klass->vtable_size)
            {
                RT_ERROR("INVOKE_VT slot out of range: class=%s slot=%d vtable_size=%d", klass->name->chars, slot, klass->vtable_size);
            }
            Value mval = klass->vtable[slot];

            if (is_closure(mval))
            {
                ObjClosure *cl = as_closure(mval);
                ObjFunc *fn = cl->func;
                /* The compiler only emits OP_INVOKE_VT for a plain `obj.m(...)`
                ** call; a generic method reached this way carries no type
                ** arguments at all. (fn->name is already qualified with the
                ** declaring class, so don't prefix it again.) */
                if (fn->generic_arity > 0)
                {
                    RT_ERROR("'%s' is generic and must be called with <...> type arguments",
                             fn->name ? fn->name->chars : "?");
                }
                if (fiber->frame_count >= kMaxFrames)
                {
                    RT_ERROR("stack overflow");
                }
                ++ip;
                SAVE_IP();
                CallFrame *new_frame = &fiber->frames[fiber->frame_count++];
                new_frame->closure = cl;
                new_frame->func = fn;
                new_frame->ip = fn->code;
                new_frame->base = &R[base]; /* base[0]=self, base[1..]=args */
                new_frame->ret_reg = base;
                new_frame->ret_count = 1;
                fiber->stack_top = new_frame->base + fn->num_regs;
                LOAD_STATE();
                DISPATCH();
            }
            else if (is_native(mval))
            {
                ObjNative *nat = as_native(mval);
                if (nat->generic_arity > 0)
                {
                    RT_ERROR("'%s.%s' is a generic native method and must be called with <...> type arguments",
                             klass->name->chars, nat->name ? nat->name->chars : "?");
                }
                int nret = nat->fn(this, &R[base], arg_count + 1);
                if (nret > 0)
                    R[base] = R[base];
                else
                    R[base] = val_nil();
            }
            else
            {
                RT_ERROR("vtable slot %d is nil (method not found)", slot);
            }
            NEXT();
        }

        CASE(OP_INVOKE_VT_FAST)
        {
            /* The compiler proved the receiver is an annotated instance, the
            ** slot holds a script closure in its sealed vtable, and the
            ** signature takes exactly arg_count values — so OP_INVOKE_VT's
            ** type test, slot range check and closure/native branch are all
            ** already decided. Only the frame push remains.
            **
            ** The guarantee is the compiler's: emit this ONLY where all
            ** three hold, or the as_instance()/as_closure() below are the
            ** same unchecked casts that made the fused opcodes segfault.
            **
            ** A fourth clause since reified generics: the method must have
            ** generic_arity == 0. There is no room here for the ngeneric
            ** operand and no split between type and value registers, so a
            ** generic method must go through OP_INVOKE_GENERIC instead. */
            uint32_t i = *ip;
            uint8_t base = ZEN_A(i);
            uint8_t arg_count = ZEN_B(i);
            uint8_t slot = ZEN_C(i);
            (void)arg_count;
            ObjInstance *inst = as_instance(R[base]);
            ObjClosure *cl = as_closure(inst->klass->vtable[slot]);
            ObjFunc *fn = cl->func;
            if (fiber->frame_count >= kMaxFrames)
            {
                RT_ERROR("stack overflow");
            }
            ++ip;
            SAVE_IP();
            CallFrame *new_frame = &fiber->frames[fiber->frame_count++];
            new_frame->closure = cl;
            new_frame->func = fn;
            new_frame->ip = fn->code;
            new_frame->base = &R[base]; /* base[0]=self, base[1..]=args */
            new_frame->ret_reg = base;
            new_frame->ret_count = 1;
            fiber->stack_top = new_frame->base + fn->num_regs;
            LOAD_STATE();
            DISPATCH();
        }

        CASE(OP_SUPER_INVOKE)
        {
            /* 3-word: word1=[OP|base|argc|0], word2=(sel<<16|name_ki), word3=parent_ki */
            uint32_t i = *ip;
            uint8_t base = ZEN_A(i);
            uint8_t arg_count = ZEN_B(i);
            uint32_t word2 = ip[1];
            int sel_slot = (int)(word2 >> 16);
            int name_ki = (int)(word2 & 0xFFFF);
            uint32_t parent_ki = ip[2];

            /* Resolve static parent class from constant pool */
            ObjClass *parent = as_class(frame->func->constants[parent_ki]);

            /* Vtable lookup on static parent */
            Value mval = val_nil();
            if (sel_slot < parent->vtable_size)
                mval = parent->vtable[sel_slot];

            if (is_nil(mval))
            {
                const char *mname = as_string(frame->func->constants[name_ki])->chars;
                RT_ERROR("parent class '%s' has no method '%s'", parent->name->chars, mname);
            }

            if (is_closure(mval))
            {
                ObjClosure *cl = as_closure(mval);
                ObjFunc *fn = cl->func;
                /* There is no super().method<T>(...) syntax yet, so a generic
                ** parent method reached through plain super() is rejected
                ** rather than silently miscalled. */
                if (fn->generic_arity > 0)
                {
                    const char *mname = as_string(frame->func->constants[name_ki])->chars;
                    RT_ERROR("'%s' is generic and cannot be called via super() yet", mname);
                }
                if (fn->arity >= 0 && arg_count != fn->arity)
                {
                    const char *mname = as_string(frame->func->constants[name_ki])->chars;
                    RT_ERROR("super.%s() expects %d args but got %d", mname, fn->arity, arg_count);
                }
                if (fiber->frame_count >= kMaxFrames ||
                    &R[base] + fn->num_regs > fiber->stack + fiber->stack_capacity)
                {
                    RT_ERROR("stack overflow");
                }
                ip += 3; /* skip all 3 words */
                SAVE_IP();
                CallFrame *new_frame = &fiber->frames[fiber->frame_count++];
                new_frame->closure = cl;
                new_frame->func = fn;
                new_frame->ip = fn->code;
                new_frame->base = &R[base]; /* base[0]=self, base[1..]=args */
                new_frame->ret_reg = base;
                new_frame->ret_count = 1;
                fiber->stack_top = new_frame->base + fn->num_regs;
                LOAD_STATE();
                DISPATCH();
            }
            else if (is_native(mval))
            {
                ObjNative *nat = as_native(mval);
                if (nat->generic_arity > 0)
                {
                    const char *mname = as_string(frame->func->constants[name_ki])->chars;
                    RT_ERROR("'%s' is a generic native method and cannot be called via super()", mname);
                }
                int nret = nat->fn(this, &R[base], arg_count + 1);
                if (nret > 0)
                    R[base] = R[base];
                else
                    R[base] = val_nil();
            }
            else
            {
                const char *mname = as_string(frame->func->constants[name_ki])->chars;
                RT_ERROR("super.%s is not callable", mname);
            }
            ip += 2; /* skip word2+word3 */
            NEXT();
        }

        CASE(OP_INVOKE_GENERIC)
        {
            /* 3-word: word1=[OP|base|nargs|nresults] (nargs = ngeneric+nvalue);
            ** word2=(sel_slot<<16|name_ki) like OP_INVOKE; word3=ngeneric.
            **
            ** `ip` deliberately still points at word1 here: word2/word3 are
            ** read through non-advancing peeks, and each branch below advances
            ** by exactly 3 words in total, matched to how it leaves the
            ** dispatch loop. The native branch falls through to NEXT() (which
            ** adds 1 itself), so it advances by 2 first; the closure branch
            ** exits via DISPATCH() into the callee and never reaches NEXT(),
            ** so it advances by all 3 before SAVE_IP(). Advancing by the wrong
            ** amount leaves the caller's saved resume ip mid-instruction, and
            ** on return the VM decodes a raw operand word as an opcode,
            ** silently corrupting the caller's registers. OP_SUPER_INVOKE
            ** above uses this same split for its own 3-word shape. */
            uint32_t i = *ip;
            uint8_t base = ZEN_A(i);
            uint8_t nargs = ZEN_B(i);
            uint8_t nresults = ZEN_C(i);
            if (nresults == 0)
                nresults = 1;
            uint32_t word2 = ip[1];
            int sel_slot = (int)(word2 >> 16);
            int name_ki = (int)(word2 & 0xFFFF);
            int ngeneric = (int)ip[2];

            /* ip still points at word1 here — SAVE_IP() now so frame->ip is
            ** correct for a traceback/GC walk from any error path below,
            ** including the native branch's bare `if (had_error_) return;`,
            ** which (like OP_CALL's equivalent) relies on this having already
            ** run rather than saving again itself. */
            SAVE_IP();

            Value receiver = R[base];
            ObjString *method = as_string(K[name_ki]);
            const char *mname = method->chars;

            if (!is_instance(receiver))
            {
                RT_ERROR("generic methods are not supported on this type (got %s)", val_type_str(receiver));
            }
            ObjInstance *inst = as_instance(receiver);
            ObjClass *klass = inst->klass;

            /* Vtables are flattened at class-build time, so a single slot read
            ** already covers inherited methods — no parent walk needed. */
            Value mval = val_nil();
            if (sel_slot < klass->vtable_size)
                mval = klass->vtable[sel_slot];
            if (is_nil(mval))
            {
                RT_ERROR("'%s' has no method '%s'", klass->name->chars, mname);
            }

            int nvalue = (int)nargs - ngeneric;
            for (int gi = 0; gi < ngeneric; gi++)
            {
                if (!is_class(R[base + 1 + gi]))
                {
                    RT_ERROR("'%s.%s': type argument %d is not a type",
                             klass->name->chars, mname, gi + 1);
                }
            }

            if (is_native(mval))
            {
                /* Native generic method, e.g. entity.get_component<Transform>(),
                ** registered via ClassBuilder::generic_method(). Type args and
                ** value args reach C++ as two separate arrays — here that is
                ** just two pointers into the same contiguous register block. */
                ObjNative *nat = as_native(mval);
                if (nat->generic_arity == 0)
                {
                    RT_ERROR("'%s.%s' is not generic — called with <...> but takes no type arguments",
                             klass->name->chars, mname);
                }
                if (ngeneric != nat->generic_arity)
                {
                    RT_ERROR("'%s.%s' expects %d type argument%s but got %d",
                             klass->name->chars, mname, nat->generic_arity,
                             nat->generic_arity == 1 ? "" : "s", ngeneric);
                }
                if (nat->arity >= 0 && nvalue != nat->arity)
                {
                    RT_ERROR("%s.%s() expects %d args but got %d",
                             klass->name->chars, mname, nat->arity, nvalue);
                }

                Value *type_args = &R[base + 1];
                Value *value_args = &R[base + 1 + ngeneric];
                int nret = nat->generic_fn(this, receiver, type_args, ngeneric, value_args, nvalue);
                if (had_error_)
                    return;
                if (nret < 0)
                {
                    RT_ERROR("generic native method '%s' returned error", mname);
                }
                copy_native_results(&R[base], value_args, nret, nresults);
                /* Past word2+word3 — NEXT() adds the third. */
                ip += 2;
                NEXT();
            }

            if (!is_closure(mval))
            {
                RT_ERROR("cannot invoke generic method '%s' on this type", mname);
            }

            {
                ObjClosure *cl = as_closure(mval);
                ObjFunc *fn = cl->func;
                if (fn->generic_arity == 0)
                {
                    RT_ERROR("'%s.%s' is not generic — called with <...> but takes no type arguments",
                             klass->name->chars, mname);
                }
                if (ngeneric != fn->generic_arity)
                {
                    RT_ERROR("'%s.%s' expects %d type argument%s but got %d",
                             klass->name->chars, mname, fn->generic_arity,
                             fn->generic_arity == 1 ? "" : "s", ngeneric);
                }
                if (fn->arity >= 0 && nvalue != fn->arity)
                {
                    RT_ERROR("%s.%s() expects %d args but got %d",
                             klass->name->chars, mname, fn->arity, nvalue);
                }
                if (fiber->frame_count >= kMaxFrames ||
                    &R[base] + fn->num_regs > fiber->stack + fiber->stack_capacity)
                {
                    RT_ERROR("stack overflow");
                }
                /* This branch never reaches NEXT(), so it must account for all
                ** 3 words itself before saving ip for the eventual return. */
                ip += 3;
                SAVE_IP();
                CallFrame *new_frame = &fiber->frames[fiber->frame_count++];
                new_frame->closure = cl;
                new_frame->func = fn;
                new_frame->ip = fn->code;
                /* base[0]=self, base[1..ngeneric]=types, base[1+ngeneric..]=values */
                new_frame->base = &R[base];
                new_frame->ret_reg = base;
                new_frame->ret_count = nresults;
                fiber->stack_top = new_frame->base + fn->num_regs;
                LOAD_STATE();
                DISPATCH();
            }
        }

        /* --- Misc --- */
        CASE(OP_CONCAT)
        {
            uint32_t i = *ip;
            Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];

            /* Fast path: both are strings already */
            if (is_string(vb) && is_string(vc))
            {
                R[ZEN_A(i)] = val_obj((Obj *)string_append_inplace(&gc_, as_string(vb), as_string(vc)));
                NEXT();
            }

            /* Slow path: convert to strings then concat */
            char buf_b[64], buf_c[64];
            const char *sb; int lb;
            const char *sc; int lc;

            if (is_string(vb)) { sb = as_cstring(vb); lb = as_string(vb)->length; }
            else if (is_int(vb)) { lb = int_to_cstr(vb.as.integer, buf_b); sb = buf_b; }
            else if (is_float(vb)) { lb = snprintf(buf_b, sizeof(buf_b), "%g", vb.as.number); sb = buf_b; }
            else if (is_bool(vb)) { sb = vb.as.boolean ? "true" : "false"; lb = vb.as.boolean ? 4 : 5; }
            else if (is_nil(vb)) { sb = "nil"; lb = 3; }
            else { sb = "<obj>"; lb = 5; }

            if (is_string(vc)) { sc = as_cstring(vc); lc = as_string(vc)->length; }
            else if (is_int(vc)) { lc = int_to_cstr(vc.as.integer, buf_c); sc = buf_c; }
            else if (is_float(vc)) { lc = snprintf(buf_c, sizeof(buf_c), "%g", vc.as.number); sc = buf_c; }
            else if (is_bool(vc)) { sc = vc.as.boolean ? "true" : "false"; lc = vc.as.boolean ? 4 : 5; }
            else if (is_nil(vc)) { sc = "nil"; lc = 3; }
            else { sc = "<obj>"; lc = 5; }

            /* Stack buffer — no malloc for results ≤ 256 */
            int len = lb + lc;
            char buf[256];
            char *tmp = (len <= 256) ? buf : (char *)malloc(len);
            memcpy(tmp, sb, lb);
            memcpy(tmp + lb, sc, lc);
            R[ZEN_A(i)] = val_obj((Obj *)new_string(&gc_, tmp, len));
            if (tmp != buf) free(tmp);
            NEXT();
        }

        CASE(OP_STRADD)
        {
            uint32_t i = *ip;
            uint8_t a = ZEN_A(i);
            Value va = R[a], vb = R[ZEN_B(i)];
            if (is_string(va) && is_string(vb))
            {
                ObjString *result = string_append_inplace(&gc_, as_string(va), as_string(vb));
                R[a] = val_obj((Obj *)result);
            }
            else if (is_instance(va) || is_instance(vb))
            {
                /* `t += u` on a local always compiles to OP_STRADD (see
                   compiler_expressions.cpp) regardless of what va/vb turn
                   out to be at runtime, so this — like OP_ADD above — must
                   not assume "not a string means numeric": reading an
                   instance's object pointer as a float silently produced
                   garbage here before this fix. Same object-operator +
                   __str__ fallback as OP_ADD. */
                Value result;
                SAVE_IP();
                if (try_binary_operator(this, va, vb, SLOT_ADD, SLOT_RADD, &result))
                {
                    if (had_error_) return;
                    LOAD_STATE();
                    R[a] = result;
                }
                else
                {
                    LOAD_STATE();
                    /* Fix: pause the GC across this two-sided __str__
                       coercion — no allocation in this window can trigger a
                       collection, so neither intermediate result (held only
                       in a C++ local, not a GC root) can be swept as a
                       silent use-after-free. Deliberately not "parking"
                       results into registers instead: OP_ADD's equivalent
                       fallback learned the hard way that ZEN_C(i)-shaped
                       operand registers aren't reliably scratch (they can be
                       a bare local/parameter's own register — see OP_ADD
                       above), and while OP_STRADD's B register genuinely is
                       always a compiler temp (rhs_reg in
                       compiler_expressions.cpp, freed right after this
                       instruction), pausing the GC needs no such proof. */
                    Value sv = va, sc = vb;
                    gc_pause(&gc_);
                    if (!is_string(sv))
                    {
                        Value str_result;
                        if (try_string_operator(this, sv, &str_result))
                            sv = str_result;
                        else
                            sv = default_to_string(&gc_, sv);
                        if (had_error_) { gc_resume(&gc_); return; }
                        LOAD_STATE();
                    }
                    if (!is_string(sc))
                    {
                        Value str_result;
                        if (try_string_operator(this, sc, &str_result))
                            sc = str_result;
                        else
                            sc = default_to_string(&gc_, sc);
                        if (had_error_) { gc_resume(&gc_); return; }
                        LOAD_STATE();
                    }
                    R[a] = val_obj((Obj *)string_append_inplace(&gc_, as_string(sv), as_string(sc)));
                    gc_resume(&gc_);
                }
            }
            else if (is_string(va) || is_string(vb))
            {
                /* At least one is string — coerce the other and concat */
                char buf[64], buf2[64];
                const char *sa; int la;
                const char *sb; int lb;
                if (is_string(va)) { sa = as_cstring(va); la = as_string(va)->length; }
                else if (is_int(va)) { la = int_to_cstr(va.as.integer, buf); sa = buf; }
                else if (is_float(va)) { la = snprintf(buf, sizeof(buf), "%g", va.as.number); sa = buf; }
                else { sa = "nil"; la = 3; }

                if (is_string(vb)) { sb = as_cstring(vb); lb = as_string(vb)->length; }
                else if (is_int(vb)) { lb = int_to_cstr(vb.as.integer, buf2); sb = buf2; }
                else if (is_float(vb)) { lb = snprintf(buf2, sizeof(buf2), "%g", vb.as.number); sb = buf2; }
                else { sb = "nil"; lb = 3; }

                int len = la + lb;
                char tmp[256];
                char *p = (len <= 256) ? tmp : (char *)malloc(len);
                memcpy(p, sa, la);
                memcpy(p + la, sb, lb);
                R[a] = val_obj((Obj *)new_string(&gc_, p, len));
                if (p != tmp) free(p);
            }
            else
            {
                /* Both numeric — regular addition */
                if (is_int(va) && is_int(vb))
                    R[a] = val_int(va.as.integer + vb.as.integer);
                else
                {
                    double da = is_int(va) ? (double)va.as.integer : va.as.number;
                    double db = is_int(vb) ? (double)vb.as.integer : vb.as.number;
                    R[a] = val_float(da + db);
                }
            }
            NEXT();
        }

        CASE(OP_TOSTRING)
        {
            uint32_t i = *ip;
            uint8_t dst = ZEN_A(i);
            Value v = R[ZEN_B(i)];
            if (is_instance(v) && instance_has_method_slot(v, SLOT_STR))
            {
                Value result;
                SAVE_IP();
                if (!try_string_operator(this, v, &result))
                {
                    LOAD_STATE();
                    R[dst] = default_to_string(&gc_, v);
                    NEXT();
                }
                if (had_error_) return;
                LOAD_STATE();
                if (!is_string(result))
                {
                    RT_ERROR("__str__ must return a string");
                }
                R[dst] = result;
            }
            else
            {
                R[dst] = default_to_string(&gc_, v);
            }
            NEXT();
        }

        CASE(OP_TOSTRING_OBJ)
        {
            uint32_t i = *ip;
            uint8_t dst = ZEN_A(i);
            Value result;
            SAVE_IP();
            if (!try_string_operator(this, R[ZEN_B(i)], &result))
            {
                LOAD_STATE();
                RT_ERROR("object does not implement __str__");
            }
            if (had_error_) return;
            LOAD_STATE();
            if (!is_string(result))
            {
                RT_ERROR("__str__ must return a string");
            }
            R[dst] = result;
            NEXT();
        }

        CASE(OP_LEN)
        {
            uint32_t i = *ip;
            Value v = R[ZEN_B(i)];
            if (is_string(v))
                R[ZEN_A(i)] = val_int(as_string(v)->length);
            else if (is_array(v))
                R[ZEN_A(i)] = val_int(arr_count(as_array(v)));
            else if (is_map(v))
                R[ZEN_A(i)] = val_int(as_map(v)->count);
            else if (is_set(v))
                R[ZEN_A(i)] = val_int(as_set(v)->count);
            else if (is_buffer(v))
                R[ZEN_A(i)] = val_int(as_buffer(v)->count);
            else
                R[ZEN_A(i)] = val_int(0);
            NEXT();
        }

        CASE(OP_PRINT)
        {
            uint32_t i = *ip;
            void *_ud = callbacks_.userdata;
#define ZP(s, l) callbacks_.print((s), (int)(l), _ud)
#define ZPS(s)   callbacks_.print((s), (int)sizeof(s) - 1, _ud)
            if (!ZEN_C(i))
            { /* C=0: normal print value */
                Value v = R[ZEN_A(i)];
                switch (v.type)
                {
                case VAL_NIL:
                    ZPS("nil");
                    break;
                case VAL_BOOL:
                    if (v.as.boolean) { ZPS("true"); } else { ZPS("false"); }
                    break;
                case VAL_INT:
                {
                    char ibuf[21];
                    int ilen = int_to_cstr(v.as.integer, ibuf);
                    ZP(ibuf, ilen);
                    break;
                }
                case VAL_FLOAT:
                {
                    char fbuf[32];
                    int flen = snprintf(fbuf, sizeof(fbuf), "%g", v.as.number);
                    ZP(fbuf, flen);
                    break;
                }
                case VAL_OBJ:
                    if (v.as.obj->type == OBJ_STRING)
                    {
                        ZP(((ObjString *)v.as.obj)->chars, ((ObjString *)v.as.obj)->length);
                    }
                    else if (is_instance(v) && instance_has_method_slot(v, SLOT_STR))
                    {
                        Value result;
                        SAVE_IP();
                        if (!try_string_operator(this, v, &result))
                        {
                            LOAD_STATE();
                            print_value(v);
                            break;
                        }
                        if (had_error_) return;
                        LOAD_STATE();
                        if (!is_string(result))
                        {
                            RT_ERROR("__str__ must return a string");
                        }
                        ObjString *s = as_string(result);
                        ZP(s->chars, s->length);
                    }
                    else
                    {
                        print_value(v);
                    }
                    break;
                case VAL_PTR:
                {
                    char pbuf[32];
                    int plen = snprintf(pbuf, sizeof(pbuf), "<ptr %p>", v.as.pointer);
                    ZP(pbuf, plen);
                    break;
                }
                }
            }
            if (ZEN_B(i))
                ZP("\n", 1);
            else
                ZPS(" ");
#undef ZP
#undef ZPS
            NEXT();
        }

        /* --- Math builtins --- */
        CASE(OP_SIN)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_float(sin(to_number(R[ZEN_B(i)])));
            NEXT();
        }
        CASE(OP_COS)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_float(cos(to_number(R[ZEN_B(i)])));
            NEXT();
        }
        CASE(OP_TAN)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_float(tan(to_number(R[ZEN_B(i)])));
            NEXT();
        }
        CASE(OP_ASIN)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_float(asin(to_number(R[ZEN_B(i)])));
            NEXT();
        }
        CASE(OP_ACOS)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_float(acos(to_number(R[ZEN_B(i)])));
            NEXT();
        }
        CASE(OP_ATAN)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_float(atan(to_number(R[ZEN_B(i)])));
            NEXT();
        }
        CASE(OP_ATAN2)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_float(atan2(to_number(R[ZEN_B(i)]), to_number(R[ZEN_C(i)])));
            NEXT();
        }
        CASE(OP_SQRT)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_float(sqrt(to_number(R[ZEN_B(i)])));
            NEXT();
        }
        CASE(OP_POW)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_float(pow(to_number(R[ZEN_B(i)]), to_number(R[ZEN_C(i)])));
            NEXT();
        }
        CASE(OP_LOG)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_float(log(to_number(R[ZEN_B(i)])));
            NEXT();
        }
        CASE(OP_ABS)
        {
            uint32_t i = *ip;
            Value v = R[ZEN_B(i)];
            if (v.type == VAL_INT)
                R[ZEN_A(i)] = val_int(v.as.integer < 0
                    ? (int64_t)(-(uint64_t)v.as.integer)
                    : v.as.integer);
            else
                R[ZEN_A(i)] = val_float(fabs(to_number(v)));
            NEXT();
        }
        CASE(OP_FLOOR)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_float(floor(to_number(R[ZEN_B(i)])));
            NEXT();
        }
        CASE(OP_CEIL)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_float(ceil(to_number(R[ZEN_B(i)])));
            NEXT();
        }
        CASE(OP_DEG)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_float(to_number(R[ZEN_B(i)]) * (180.0 / 3.14159265358979323846));
            NEXT();
        }
        CASE(OP_RAD)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_float(to_number(R[ZEN_B(i)]) * (3.14159265358979323846 / 180.0));
            NEXT();
        }
        CASE(OP_EXP)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_float(exp(to_number(R[ZEN_B(i)])));
            NEXT();
        }
        CASE(OP_CLOCK)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_float((double)clock() / CLOCKS_PER_SEC);
            NEXT();
        }

        /* --- Fused comparison + jump superinstructions (2-word) --- */
        CASE(OP_LTJMPIFNOT)
        {
            uint32_t i = *ip;
            Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];
            bool less;
            if (vb.type == VAL_INT && vc.type == VAL_INT)
                less = vb.as.integer < vc.as.integer;
            else
                less = to_number(vb) < to_number(vc);
            ++ip; /* advance to the sBx word */
            if (!less)
                ip += ZEN_SBX(*ip);
            NEXT();
        }

        CASE(OP_LEJMPIFNOT)
        {
            uint32_t i = *ip;
            Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];
            bool le;
            if (vb.type == VAL_INT && vc.type == VAL_INT)
                le = vb.as.integer <= vc.as.integer;
            else
                le = to_number(vb) <= to_number(vc);
            ++ip; /* advance to the sBx word */
            if (!le)
                ip += ZEN_SBX(*ip);
            NEXT();
        }

        /* --- Ported from zenpy (PLANO.md item 1) ---
        ** Same shape as OP_LTJMPIFNOT/OP_LEJMPIFNOT above: compare, step to
        ** the sBx word, jump when the condition does not hold. */

        CASE(OP_EQJMPIFNOT)
        {
            uint32_t i = *ip;
            Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];
            bool eq;
            if (__builtin_expect(is_instance(vb) || is_instance(vc), 0))
            {
                ++ip; /* operator overload may re-enter; be at the sBx word */
                Value result;
                SAVE_IP();
                if (try_binary_operator(this, vb, vc, SLOT_EQ, SLOT_EQ, &result))
                {
                    if (had_error_) return;
                    LOAD_STATE();
                    eq = is_truthy(result);
                }
                else
                {
                    LOAD_STATE();
                    eq = values_equal(vb, vc);
                }
                if (!eq)
                    ip += ZEN_SBX(*ip);
                NEXT();
            }
            eq = values_equal(vb, vc);
            ++ip;
            if (!eq)
                ip += ZEN_SBX(*ip);
            NEXT();
        }

        CASE(OP_NEJMPIFNOT)
        {
            uint32_t i = *ip;
            Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];
            bool ne;
            if (__builtin_expect(is_instance(vb) || is_instance(vc), 0))
            {
                ++ip;
                Value result;
                SAVE_IP();
                if (try_binary_operator(this, vb, vc, SLOT_EQ, SLOT_EQ, &result))
                {
                    if (had_error_) return;
                    LOAD_STATE();
                    ne = !is_truthy(result);
                }
                else
                {
                    LOAD_STATE();
                    ne = !values_equal(vb, vc);
                }
                if (!ne)
                    ip += ZEN_SBX(*ip);
                NEXT();
            }
            ne = !values_equal(vb, vc);
            ++ip;
            if (!ne)
                ip += ZEN_SBX(*ip);
            NEXT();
        }

        /* C is a signed 8-bit immediate, so `i < 10` needs no register for
        ** the literal and no constant load before the compare. */
        CASE(OP_LTIJMPIFNOT)
        {
            uint32_t i = *ip;
            Value vx = R[ZEN_B(i)];
            int64_t imm = (int8_t)ZEN_C(i);
            bool less;
            if (__builtin_expect(vx.type == VAL_INT, 1))
                less = vx.as.integer < imm;
            else
                less = to_number(vx) < (double)imm;
            ++ip;
            if (!less)
                ip += ZEN_SBX(*ip);
            NEXT();
        }

        CASE(OP_GTIJMPIFNOT)
        {
            uint32_t i = *ip;
            Value vx = R[ZEN_B(i)];
            int64_t imm = (int8_t)ZEN_C(i);
            bool greater;
            if (__builtin_expect(vx.type == VAL_INT, 1))
                greater = vx.as.integer > imm;
            else
                greater = to_number(vx) > (double)imm;
            ++ip;
            if (!greater)
                ip += ZEN_SBX(*ip);
            NEXT();
        }

        CASE(OP_JMPIFNIL)
        {
            uint32_t i = *ip;
            bool isnil = is_nil(R[ZEN_A(i)]);
            ++ip;
            if (isnil)
                ip += ZEN_SBX(*ip);
            NEXT();
        }

        CASE(OP_FORPREP)
        {
            /* R[A]=counter, R[A+1]=limit, R[A+2]=step
               Subtract step so first FORLOOP increments to start value.
               Then jump to FORLOOP for initial test. */
            uint32_t i = *ip;
            int a = ZEN_A(i);
            R[a].as.integer -= R[a + 2].as.integer;
            ip += ZEN_SBX(i); /* jump to FORLOOP */
            NEXT();
        }

        CASE(OP_FORLOOP)
        {
            /* R[A] += R[A+2]; if still in range: pc += sBx (back to body) */
            uint32_t i = *ip;
            int a = ZEN_A(i);
            int32_t counter = R[a].as.integer + R[a + 2].as.integer;
            int32_t limit = R[a + 1].as.integer;
            R[a].as.integer = counter;
            if (counter < limit)
                ip += ZEN_SBX(i); /* loop back */
            NEXT();
        }

        /* --- Fused field+arith superinstructions (2-word) --- */
        CASE(OP_GETFIELD_MUL)
        {
            /* word1: GETFIELD_IDX  R[A] = R[B].fields[C]
               word2: MUL           R[A] = R[B] * R[C]    */
            uint32_t i1 = *ip;
            Value fobj = R[ZEN_B(i1)];
            const int fidx = ZEN_C(i1);
            if (is_instance(fobj))
            {
                ObjInstance *inst = as_instance(fobj);
                if (fidx < 0 || fidx >= inst->klass->num_fields)
                {
                    RT_ERROR("GETFIELD_MUL out of bounds: receiver=R%d class=%s field_index=%d field_count=%d", ZEN_B(i1), inst->klass->name->chars, fidx, inst->klass->num_fields);
                }
                R[ZEN_A(i1)] = inst->fields[fidx];
            }
            else if (is_struct(fobj))
            {
                ObjStruct *st = as_struct(fobj);
                if (fidx < 0 || fidx >= st->def->num_fields)
                {
                    RT_ERROR("GETFIELD_MUL out of bounds: receiver=R%d struct=%s field_index=%d field_count=%d", ZEN_B(i1), st->def->name->chars, fidx, st->def->num_fields);
                }
                R[ZEN_A(i1)] = st->fields[fidx];
            }
            else
            {
                RT_ERROR("GETFIELD_MUL expected instance/struct: dst=R%d receiver=R%d receiver_type=%s field_index=%d", ZEN_A(i1), ZEN_B(i1), value_debug_type(fobj), fidx);
            }
            ++ip;
            uint32_t i2 = *ip;
            Value vb = R[ZEN_B(i2)], vc = R[ZEN_C(i2)];
            if (__builtin_expect(!is_obj(vb) && !is_obj(vc), 1))
            {
                if (vb.type == VAL_INT && vc.type == VAL_INT)
                    R[ZEN_A(i2)] = val_int((int64_t)((uint64_t)vb.as.integer * (uint64_t)vc.as.integer));
                else
                    R[ZEN_A(i2)] = val_float(to_number(vb) * to_number(vc));
            }
            else if (is_instance(vb) || is_instance(vc))
            {
                Value result;
                SAVE_IP();
                if (try_binary_operator(this, vb, vc, SLOT_MUL, SLOT_RMUL, &result))
                {
                    if (had_error_) return;
                    LOAD_STATE();
                    R[ZEN_A(i2)] = result;
                }
                else { LOAD_STATE(); R[ZEN_A(i2)] = val_float(to_number(vb) * to_number(vc)); }
            }
            else
            {
                R[ZEN_A(i2)] = val_float(to_number(vb) * to_number(vc));
            }
            NEXT();
        }

        CASE(OP_GETFIELD_SUB)
        {
            /* word1: GETFIELD_IDX  R[A] = R[B].fields[C]
               word2: SUB           R[A] = R[B] - R[C]    */
            uint32_t i1 = *ip;
            Value fobj = R[ZEN_B(i1)];
            const int fidx = ZEN_C(i1);
            if (is_instance(fobj))
            {
                ObjInstance *inst = as_instance(fobj);
                if (fidx < 0 || fidx >= inst->klass->num_fields)
                {
                    RT_ERROR("GETFIELD_SUB out of bounds: receiver=R%d class=%s field_index=%d field_count=%d", ZEN_B(i1), inst->klass->name->chars, fidx, inst->klass->num_fields);
                }
                R[ZEN_A(i1)] = inst->fields[fidx];
            }
            else if (is_struct(fobj))
            {
                ObjStruct *st = as_struct(fobj);
                if (fidx < 0 || fidx >= st->def->num_fields)
                {
                    RT_ERROR("GETFIELD_SUB out of bounds: receiver=R%d struct=%s field_index=%d field_count=%d", ZEN_B(i1), st->def->name->chars, fidx, st->def->num_fields);
                }
                R[ZEN_A(i1)] = st->fields[fidx];
            }
            else
            {
                RT_ERROR("GETFIELD_SUB expected instance/struct: dst=R%d receiver=R%d receiver_type=%s field_index=%d", ZEN_A(i1), ZEN_B(i1), value_debug_type(fobj), fidx);
            }
            ++ip;
            uint32_t i2 = *ip;
            Value vb = R[ZEN_B(i2)], vc = R[ZEN_C(i2)];
            if (__builtin_expect(!is_obj(vb) && !is_obj(vc), 1))
            {
                if (vb.type == VAL_INT && vc.type == VAL_INT)
                    R[ZEN_A(i2)] = val_int(vb.as.integer - vc.as.integer);
                else
                    R[ZEN_A(i2)] = val_float(to_number(vb) - to_number(vc));
            }
            else if (is_instance(vb) || is_instance(vc))
            {
                Value result;
                SAVE_IP();
                if (try_binary_operator(this, vb, vc, SLOT_SUB, SLOT_RSUB, &result))
                {
                    if (had_error_) return;
                    LOAD_STATE();
                    R[ZEN_A(i2)] = result;
                }
                else { LOAD_STATE(); R[ZEN_A(i2)] = val_float(to_number(vb) - to_number(vc)); }
            }
            else
            {
                R[ZEN_A(i2)] = val_float(to_number(vb) - to_number(vc));
            }
            NEXT();
        }

        CASE(OP_CONTAINS)
        {
            uint32_t i = *ip;
            Value needle    = R[ZEN_B(i)];
            Value haystack  = R[ZEN_C(i)];
            bool found = false;
            if (is_array(haystack)) {
                ObjArray *arr = as_array(haystack);
                int32_t cnt = arr_count(arr);
                for (int32_t k = 0; k < cnt; k++)
                    if (values_equal(arr->data[k], needle)) { found = true; break; }
            } else if (is_map(haystack)) {
                map_get(as_map(haystack), needle, &found);
            } else if (is_set(haystack)) {
                found = set_contains(as_set(haystack), needle);
            } else if (is_string(haystack) && is_string(needle)) {
                found = strstr(as_cstring(haystack), as_cstring(needle)) != nullptr;
            } else {
                RT_ERROR("'in' operator not supported for this type");
            }
            R[ZEN_A(i)] = val_bool(found);
            NEXT();
        }

        CASE(OP_DELINDEX)
        {
            uint32_t i = *ip;
            Value container = R[ZEN_A(i)];
            Value key       = R[ZEN_B(i)];
            if (is_array(container)) {
                ObjArray *arr = as_array(container);
                int32_t idx = (int32_t)key.as.integer;
                if (idx < 0) idx += arr_count(arr);
                if (idx < 0 || idx >= arr_count(arr)) RT_ERROR("del index out of range");
                array_remove(arr, idx);
            } else if (is_map(container)) {
                map_delete(as_map(container), key);
            } else {
                RT_ERROR("'del' not supported for this type");
            }
            NEXT();
        }

        CASE(OP_GETSLICE)
        {
            uint32_t i  = *ip;
            Value obj   = R[ZEN_B(i)];
            int   base  = ZEN_C(i);          /* R[base]=start, R[base+1]=stop, R[base+2]=step */
            Value vstart = R[base];
            Value vstop  = R[base + 1];
            Value vstep  = R[base + 2];

            if (is_array(obj)) {
                ObjArray *src = as_array(obj);
                int32_t  len  = arr_count(src);
                int32_t  step  = is_nil(vstep)  ? 1 : (int32_t)vstep.as.integer;
                if (step == 0) RT_ERROR("slice step cannot be zero");
                int32_t  start = is_nil(vstart) ? (step > 0 ? 0 : len - 1) : (int32_t)vstart.as.integer;
                int32_t  stop  = is_nil(vstop)  ? (step > 0 ? len : -len - 1) : (int32_t)vstop.as.integer;
                if (start < 0) start += len;
                if (stop  < 0) stop  += len;
                ObjArray *dst = new_array(&gc_);
                if (step > 0)
                    for (int32_t k = start; k < stop && k < len; k += step) array_push(&gc_, dst, src->data[k]);
                else
                    for (int32_t k = start; k > stop && k >= 0; k += step) array_push(&gc_, dst, src->data[k]);
                R[ZEN_A(i)] = val_obj((Obj *)dst);
            } else if (is_string(obj)) {
                ObjString *src = as_string(obj);
                int32_t   len  = src->length;
                int32_t   step  = is_nil(vstep)  ? 1 : (int32_t)vstep.as.integer;
                if (step == 0) RT_ERROR("slice step cannot be zero");
                int32_t   start = is_nil(vstart) ? (step > 0 ? 0 : len - 1) : (int32_t)vstart.as.integer;
                int32_t   stop  = is_nil(vstop)  ? (step > 0 ? len : -len - 1) : (int32_t)vstop.as.integer;
                if (start < 0) start += len;
                if (stop  < 0) stop  += len;
                /* Build result string */
                int32_t cap = len + 1;
                char *buf = (char *)malloc(cap);
                if (!buf) RT_ERROR("out of memory for slice");
                int32_t n = 0;
                if (step > 0)
                    for (int32_t k = start; k < stop && k < len; k += step) buf[n++] = src->chars[k];
                else
                    for (int32_t k = start; k > stop && k >= 0; k += step) buf[n++] = src->chars[k];
                R[ZEN_A(i)] = val_obj((Obj *)new_string(&gc_, buf, n));
                free(buf);
            } else {
                RT_ERROR("slice not supported for this type");
            }
            NEXT();
        }

        CASE(OP_IS)
        {
            uint32_t i = *ip;
            Value v = R[ZEN_B(i)];
            Value cls = R[ZEN_C(i)];
            bool result = false;
            if (is_instance(v) && is_class(cls))
            {
                ObjClass *k = ((ObjInstance *)v.as.obj)->klass;
                ObjClass *target = as_class(cls);
                while (k)
                {
                    if (k == target) { result = true; break; }
                    k = k->parent;
                }
            }
            R[ZEN_A(i)] = val_bool(result);
            NEXT();
        }

        CASE(OP_HALT)
        {
            SAVE_IP();
            fiber->state = FIBER_DONE;
            return;
        }

        /* =================================================================
        **  DISPATCH CLEANUP
        ** ================================================================= */

#ifdef ZEN_COMPUTED_GOTO
        /* Computed goto não precisa de close */
#else
        } /* switch */
    } /* for */
#endif

#undef DISPATCH
#undef CASE
#undef NEXT
#undef LOAD_STATE
#undef SAVE_IP
#undef NUM_BINOP
    }

} /* namespace zen */
