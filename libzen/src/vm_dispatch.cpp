/*
** vm_dispatch.cpp — the interpreter loop.
**
** Computed-goto dispatch on GCC/Clang, switch elsewhere. Register machine:
** R = frame base, K = constant pool of the running function.
*/
#include "vm.h"
#include "debug.h"
#include "zenconf.h"
#include <cmath>
#include <ctime>

namespace zen
{
    static inline void copy_native_results(Value *dst, Value *src, int nret, int nresults)
    {
        int wanted = nresults <= 0 ? 0 : nresults;
        int copy_count = nret > 0 ? (nret < wanted ? nret : wanted) : 0;
        for (int j = 0; j < copy_count; j++)
            dst[j] = src[j];
        for (int j = copy_count; j < wanted; j++)
            dst[j] = val_nil();
    }

    static const char *value_type_name(Value v)
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
            case OBJ_ARRAY: return "array";
            case OBJ_BUFFER: return "buffer";
            case OBJ_STRUCT_DEF: return "type";
            case OBJ_STRUCT: return "object";
            }
            return "obj(unknown)";
        }
        return "unknown";
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

    /* Append `n` bytes of the textual form of v to (p, len); buf is scratch. */
    static inline const char *value_text(Value v, char *buf, int *len)
    {
        if (is_string(v)) { *len = as_string(v)->length; return as_cstring(v); }
        if (is_int(v)) { *len = int_to_cstr(v.as.integer, buf); return buf; }
        if (is_float(v)) { *len = snprintf(buf, 64, "%g", v.as.number); return buf; }
        if (is_bool(v)) { *len = v.as.boolean ? 4 : 5; return v.as.boolean ? "true" : "false"; }
        if (is_nil(v)) { *len = 3; return "nil"; }
        *len = 5;
        return "<obj>";
    }

    void VM::execute(Fiber *fiber)
    {
        CallFrame *frame = &fiber->frames[fiber->frame_count - 1];
        Instruction *ip = frame->ip;
        Value *R = frame->base;
        Value *K = frame->func->constants;

#define LOAD_STATE()                                \
    frame = &fiber->frames[fiber->frame_count - 1]; \
    ip = frame->ip;                                 \
    R = frame->base;                                \
    K = frame->func->constants

#define SAVE_IP() frame->ip = ip

#define RT_ERROR(...)               \
    do                              \
    {                               \
        SAVE_IP();                  \
        runtime_error(__VA_ARGS__); \
        return;                     \
    } while (0)

/* int64 wrapping via unsigned casts (no UB) */
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

/* After a native call: honour a suspend request from the host. */
#define CHECK_SUSPEND()                        \
    do                                         \
    {                                          \
        if (ZEN_UNLIKELY(suspend_requested_)) \
        {                                      \
            suspend_requested_ = false;        \
            SAVE_IP();                         \
            fiber->state = FIBER_SUSPENDED;    \
            return;                            \
        }                                      \
    } while (0)

#ifdef ZEN_COMPUTED_GOTO
        static const void *const dispatch_table[] = {
            &&lbl_OP_LOADNIL, &&lbl_OP_LOADBOOL, &&lbl_OP_LOADK, &&lbl_OP_LOADI, &&lbl_OP_MOVE,
            &&lbl_OP_GETGLOBAL, &&lbl_OP_SETGLOBAL,
            &&lbl_OP_ADD, &&lbl_OP_SUB, &&lbl_OP_MUL, &&lbl_OP_DIV, &&lbl_OP_MOD, &&lbl_OP_IDIV, &&lbl_OP_NEG,
            &&lbl_OP_ADDI, &&lbl_OP_SUBI, &&lbl_OP_MULI,
            &&lbl_OP_BAND, &&lbl_OP_BOR, &&lbl_OP_BXOR, &&lbl_OP_BNOT, &&lbl_OP_SHL, &&lbl_OP_SHR,
            &&lbl_OP_EQ, &&lbl_OP_LT, &&lbl_OP_LE, &&lbl_OP_NOT,
            &&lbl_OP_JMP, &&lbl_OP_JMPIF, &&lbl_OP_JMPIFNOT,
            &&lbl_OP_CALL, &&lbl_OP_CALLGLOBAL, &&lbl_OP_RETURN, &&lbl_OP_RETURNNIL,
            &&lbl_OP_NEWARRAY, &&lbl_OP_NEWBUFFER, &&lbl_OP_APPEND,
            &&lbl_OP_GETFIELD_IDX, &&lbl_OP_SETFIELD_IDX, &&lbl_OP_GETINDEX, &&lbl_OP_SETINDEX,
            &&lbl_OP_CONCAT, &&lbl_OP_TOSTRING, &&lbl_OP_LEN,
            &&lbl_OP_SIN, &&lbl_OP_COS, &&lbl_OP_TAN, &&lbl_OP_ASIN, &&lbl_OP_ACOS, &&lbl_OP_ATAN, &&lbl_OP_ATAN2,
            &&lbl_OP_SQRT, &&lbl_OP_POW, &&lbl_OP_LOG, &&lbl_OP_ABS, &&lbl_OP_FLOOR, &&lbl_OP_CEIL, &&lbl_OP_DEG, &&lbl_OP_RAD, &&lbl_OP_EXP,
            &&lbl_OP_CLOCK,
            &&lbl_OP_LTJMPIFNOT, &&lbl_OP_LEJMPIFNOT, &&lbl_OP_EQJMPIFNOT, &&lbl_OP_NEJMPIFNOT,
            &&lbl_OP_LTIJMPIFNOT, &&lbl_OP_GTIJMPIFNOT, &&lbl_OP_JMPIFNIL,
            &&lbl_OP_HALT,
        };
        static_assert(sizeof(dispatch_table) / sizeof(dispatch_table[0]) == (size_t)OP_HALT + 1,
                      "dispatch_table is out of sync with the OpCode enum");

#define DISPATCH() goto *dispatch_table[ZEN_OP(*ip)]
#define CASE(op) lbl_##op:
#define NEXT()      \
    do              \
    {               \
        ++ip;       \
        DISPATCH(); \
    } while (0)

        DISPATCH();
#else
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
            switch (ZEN_OP(*ip))
            {
#endif

        /* ---------------- loads ---------------- */
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
            if (ZEN_C(i)) ++ip;
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

        /* ---------------- arithmetic ---------------- */
        CASE(OP_ADD)
        {
            uint32_t i = *ip;
            Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];
            if (ZEN_LIKELY(!is_obj(vb) && !is_obj(vc)))
            {
                NUM_BINOP(+);
            }
            else if (is_string(vb) && is_string(vc))
            {
                R[ZEN_A(i)] = val_obj((Obj *)new_string_concat(&gc_, as_string(vb), as_string(vc)));
            }
            else if (is_string(vb) || is_string(vc))
            {
                char bb[64], bc[64];
                int lb, lc;
                const char *sb = value_text(vb, bb, &lb);
                const char *sc = value_text(vc, bc, &lc);
                int len = lb + lc;
                char stackbuf[256];
                char *tmp = (len <= 256) ? stackbuf : (char *)malloc((size_t)len);
                memcpy(tmp, sb, (size_t)lb);
                memcpy(tmp + lb, sc, (size_t)lc);
                R[ZEN_A(i)] = val_obj((Obj *)new_string(&gc_, tmp, len));
                if (tmp != stackbuf) free(tmp);
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
            NUM_BINOP(-);
            NEXT();
        }
        CASE(OP_MUL)
        {
            uint32_t i = *ip;
            NUM_BINOP(*);
            NEXT();
        }
        CASE(OP_DIV)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_float(to_number(R[ZEN_B(i)]) / to_number(R[ZEN_C(i)]));
            NEXT();
        }
        CASE(OP_MOD)
        {
            uint32_t i = *ip;
            Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];
            if (vb.type == VAL_INT && vc.type == VAL_INT)
            {
                int64_t divisor = vc.as.integer;
                if (divisor == 0)
                    RT_ERROR("Division by zero");
                R[ZEN_A(i)] = val_int(vb.as.integer % divisor);
            }
            else
            {
                double a = to_number(vb), b = to_number(vc);
                R[ZEN_A(i)] = val_float(b == 0.0 ? NAN : fmod(a, b));
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
                    RT_ERROR("Division by zero");
                R[ZEN_A(i)] = val_int(vb.as.integer / divisor);
            }
            else
            {
                double a = to_number(vb), b = to_number(vc);
                if (b == 0.0)
                    RT_ERROR("Division by zero");
                R[ZEN_A(i)] = val_int((int64_t)(a / b));
            }
            NEXT();
        }
        CASE(OP_NEG)
        {
            uint32_t i = *ip;
            Value v = R[ZEN_B(i)];
            if (v.type == VAL_INT)
                R[ZEN_A(i)] = val_int((int64_t)(-(uint64_t)v.as.integer));
            else
                R[ZEN_A(i)] = val_float(-to_number(v));
            NEXT();
        }
        CASE(OP_ADDI)
        {
            uint32_t i = *ip;
            Value vb = R[ZEN_B(i)];
            int8_t imm = (int8_t)ZEN_C(i);
            if (vb.type == VAL_INT)
                R[ZEN_A(i)] = val_int((int64_t)((uint64_t)vb.as.integer + (int64_t)imm));
            else
                R[ZEN_A(i)] = val_float(to_number(vb) + imm);
            NEXT();
        }
        CASE(OP_MULI)
        {
            uint32_t i = *ip;
            Value vb = R[ZEN_B(i)];
            int8_t imm = (int8_t)ZEN_C(i);
            if (vb.type == VAL_INT)
                R[ZEN_A(i)] = val_int((int64_t)((uint64_t)vb.as.integer * (uint64_t)(int64_t)imm));
            else
                R[ZEN_A(i)] = val_float(to_number(vb) * imm);
            NEXT();
        }
        CASE(OP_SUBI)
        {
            uint32_t i = *ip;
            Value vb = R[ZEN_B(i)];
            int8_t imm = (int8_t)ZEN_C(i);
            if (vb.type == VAL_INT)
                R[ZEN_A(i)] = val_int((int64_t)((uint64_t)vb.as.integer - (int64_t)imm));
            else
                R[ZEN_A(i)] = val_float(to_number(vb) - imm);
            NEXT();
        }

        /* ---------------- bitwise ---------------- */
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
            R[ZEN_A(i)] = val_int((int64_t)(
                ((uint64_t)val >> shift) | (val < 0 ? ~(~(uint64_t)0 >> shift) : 0)));
            NEXT();
        }

        /* ---------------- comparison ---------------- */
        CASE(OP_EQ)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_bool(values_equal(R[ZEN_B(i)], R[ZEN_C(i)]));
            NEXT();
        }
        CASE(OP_LT)
        {
            uint32_t i = *ip;
            Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];
            if (is_string(vb) && is_string(vc))
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
            if (is_string(vb) && is_string(vc))
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

        /* ---------------- jumps ---------------- */
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

        /* ---------------- calls ---------------- */
        CASE(OP_CALL)
        {
            uint32_t i = *ip;
            int a = ZEN_A(i);
            int nargs = ZEN_B(i);
            int nresults = ZEN_C(i);
            ++ip;
            SAVE_IP();
            Value callee = R[a];
            if (is_func(callee))
            {
                ObjFunc *fn = as_func(callee);
                if (fiber->frame_count >= fiber->frame_capacity ||
                    &R[a + 1] + fn->num_regs > fiber->stack + fiber->stack_capacity)
                {
                    RT_ERROR("stack overflow");
                }
                CallFrame *new_frame = &fiber->frames[fiber->frame_count++];
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
                int nret = nat->fn(this, &R[a + 1], nargs);
                if (had_error_) return;
                copy_native_results(&R[a], &R[a + 1], nret, nresults);
                CHECK_SUSPEND();
                DISPATCH();
            }
            RT_ERROR("attempt to call a %s value", value_type_name(callee));
        }
        CASE(OP_CALLGLOBAL)
        {
            uint32_t i = *ip;
            int a = ZEN_A(i);
            int nargs = ZEN_B(i);
            int nresults = ZEN_C(i);
            ++ip;
            int gidx = ZEN_BX(*ip);
            ++ip;
            SAVE_IP();
            Value callee = globals_[gidx];
            if (is_func(callee))
            {
                ObjFunc *fn = as_func(callee);
                if (fiber->frame_count >= fiber->frame_capacity ||
                    &R[a + 1] + fn->num_regs > fiber->stack + fiber->stack_capacity)
                {
                    RT_ERROR("stack overflow");
                }
                CallFrame *new_frame = &fiber->frames[fiber->frame_count++];
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
                int nret = nat->fn(this, &R[a + 1], nargs);
                if (had_error_) return;
                copy_native_results(&R[a], &R[a + 1], nret, nresults);
                CHECK_SUSPEND();
                DISPATCH();
            }
            {
                /* An empty "_f" global is a command the compiler knew and
                   this runtime does not: the program was built with a
                   userlib that is not installed beside this executable.
                   Worth naming, because the generic message sends people
                   looking for a bug in their own program. */
                const char *gname = global_names_[gidx] ? global_names_[gidx]->chars : "?";
                if (is_nil(callee) && gname[0] == '_' && gname[1] == 'f')
                    RT_ERROR("command '%s' is not available: this program was built with a userlib "
                             "that is not installed next to the runtime", gname + 2);
                RT_ERROR("global '%s' is not a function", gname);
            }
        }
        CASE(OP_RETURNNIL)
        {
            R[0] = val_nil();
            /* fall into the general return path with A=0, B=1 */
            {
                int ret_reg = frame->ret_reg;
                int ret_count = frame->ret_count;
                fiber->frame_count--;
                if (fiber->frame_count == 0)
                {
                    fiber->state = FIBER_DONE;
                    return;
                }
                CallFrame *caller_frame = &fiber->frames[fiber->frame_count - 1];
                Value *caller_base = caller_frame->base;
                for (int j = 0; j < ret_count; j++)
                    caller_base[ret_reg + j] = val_nil();
                fiber->stack_top = caller_base + caller_frame->func->num_regs;
                LOAD_STATE();
                DISPATCH();
            }
        }
        CASE(OP_RETURN)
        {
            uint32_t i = *ip;
            int a = ZEN_A(i);
            int nresults = ZEN_B(i);
            int ret_reg = frame->ret_reg;
            int ret_count = frame->ret_count;
            fiber->frame_count--;
            if (fiber->frame_count == 0)
            {
                fiber->state = FIBER_DONE;
                return;
            }
            CallFrame *caller_frame = &fiber->frames[fiber->frame_count - 1];
            Value *caller_base = caller_frame->base;
            int copy_count = ret_count < 0 ? nresults : ret_count;
            if (copy_count > nresults) copy_count = nresults;
            for (int j = 0; j < copy_count; j++)
                caller_base[ret_reg + j] = R[a + j];
            for (int j = copy_count; j < ret_count; j++)
                caller_base[ret_reg + j] = val_nil();
            fiber->stack_top = caller_base + caller_frame->func->num_regs;
            LOAD_STATE();
            DISPATCH();
        }

        /* ---------------- objects ---------------- */
        CASE(OP_NEWARRAY)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_obj((Obj *)new_array(&gc_));
            NEXT();
        }
        CASE(OP_NEWBUFFER)
        {
            uint32_t i = *ip;
            int a = ZEN_A(i);
            BufferType btype = (BufferType)ZEN_C(i);
            Value arg = R[ZEN_B(i)];
            if (is_int(arg))
            {
                int32_t count = (int32_t)arg.as.integer;
                if (count < 0) RT_ERROR("buffer size must be non-negative");
                R[a] = val_obj((Obj *)new_buffer(&gc_, btype, count));
            }
            else if (is_array(arg))
            {
                ObjArray *src = as_array(arg);
                int32_t count = arr_count(src);
                ObjBuffer *buf = new_buffer(&gc_, btype, count);
                for (int32_t idx = 0; idx < count; idx++)
                    buffer_set(buf, idx, to_number(src->data[idx]));
                R[a] = val_obj((Obj *)buf);
            }
            else RT_ERROR("buffer constructor expects int or array");
            NEXT();
        }
        CASE(OP_APPEND)
        {
            uint32_t i = *ip;
            Value aval = R[ZEN_A(i)];
            if (!is_array(aval)) RT_ERROR("APPEND expected array, got %s", value_type_name(aval));
            array_push(&gc_, as_array(aval), R[ZEN_B(i)]);
            NEXT();
        }
        CASE(OP_GETFIELD_IDX)
        {
            uint32_t i = *ip;
            Value obj = R[ZEN_B(i)];
            const int field_idx = ZEN_C(i);
            if (ZEN_LIKELY(is_struct(obj)))
            {
                ObjStruct *st = as_struct(obj);
                if (field_idx >= st->def->num_fields)
                    RT_ERROR("field index %d out of range for %s", field_idx, st->def->name->chars);
                R[ZEN_A(i)] = st->fields[field_idx];
            }
            else RT_ERROR("Object does not exist");
            NEXT();
        }
        CASE(OP_SETFIELD_IDX)
        {
            uint32_t i = *ip;
            Value obj = R[ZEN_A(i)];
            const int field_idx = ZEN_B(i);
            if (ZEN_LIKELY(is_struct(obj)))
            {
                ObjStruct *st = as_struct(obj);
                if (field_idx >= st->def->num_fields)
                    RT_ERROR("field index %d out of range for %s", field_idx, st->def->name->chars);
                Value v = R[ZEN_C(i)];
                st->fields[field_idx] = v;
                if (is_obj(v)) gc_write_barrier(&gc_, (Obj *)st, v.as.obj);
            }
            else RT_ERROR("Object does not exist");
            NEXT();
        }
        CASE(OP_GETINDEX)
        {
            uint32_t i = *ip;
            Value container = R[ZEN_B(i)];
            Value key = R[ZEN_C(i)];
            if (is_array(container))
            {
                if (!is_int(key)) RT_ERROR("array index must be integer");
                ObjArray *arr = as_array(container);
                int32_t idx = (int32_t)key.as.integer;
                if ((uint32_t)idx >= (uint32_t)arr_count(arr)) RT_ERROR("Array index out of bounds");
                R[ZEN_A(i)] = arr->data[idx];
            }
            else if (is_buffer(container))
            {
                if (!is_int(key)) RT_ERROR("buffer index must be integer");
                ObjBuffer *buf = as_buffer(container);
                int32_t idx = (int32_t)key.as.integer;
                if ((uint32_t)idx >= (uint32_t)buf->count) RT_ERROR("buffer index out of bounds");
                double v = buffer_get(buf, idx);
                R[ZEN_A(i)] = buf->btype >= BUF_FLOAT32 ? val_float(v) : val_int((int64_t)v);
            }
            else if (is_string(container))
            {
                if (!is_int(key)) RT_ERROR("string index must be integer");
                ObjString *s = as_string(container);
                int32_t idx = (int32_t)key.as.integer;
                if ((uint32_t)idx >= (uint32_t)s->length) RT_ERROR("string index out of bounds");
                R[ZEN_A(i)] = val_obj((Obj *)new_string(&gc_, &s->chars[idx], 1));
            }
            else RT_ERROR("cannot index a %s value", value_type_name(container));
            NEXT();
        }
        CASE(OP_SETINDEX)
        {
            uint32_t i = *ip;
            Value container = R[ZEN_A(i)];
            Value key = R[ZEN_B(i)];
            Value val = R[ZEN_C(i)];
            if (is_array(container))
            {
                if (!is_int(key)) RT_ERROR("array index must be integer");
                ObjArray *arr = as_array(container);
                int32_t idx = (int32_t)key.as.integer;
                if ((uint32_t)idx >= (uint32_t)arr_count(arr)) RT_ERROR("Array index out of bounds");
                arr->data[idx] = val;
                if (is_obj(val)) gc_write_barrier(&gc_, (Obj *)arr, val.as.obj);
            }
            else if (is_buffer(container))
            {
                if (!is_int(key)) RT_ERROR("buffer index must be integer");
                ObjBuffer *buf = as_buffer(container);
                int32_t idx = (int32_t)key.as.integer;
                if ((uint32_t)idx >= (uint32_t)buf->count) RT_ERROR("buffer index out of bounds");
                if (!is_int(val) && !is_float(val)) RT_ERROR("buffer only accepts numbers");
                buffer_set(buf, idx, to_number(val));
            }
            else RT_ERROR("cannot index a %s value", value_type_name(container));
            NEXT();
        }

        /* ---------------- strings ---------------- */
        CASE(OP_CONCAT)
        {
            uint32_t i = *ip;
            Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];
            if (is_string(vb) && is_string(vc))
            {
                /* value semantics: never append in place, R[B] may be aliased */
                R[ZEN_A(i)] = val_obj((Obj *)new_string_concat(&gc_, as_string(vb), as_string(vc)));
                NEXT();
            }
            char bb[64], bc[64];
            int lb, lc;
            const char *sb = value_text(vb, bb, &lb);
            const char *sc = value_text(vc, bc, &lc);
            int len = lb + lc;
            char stackbuf[256];
            char *tmp = (len <= 256) ? stackbuf : (char *)malloc((size_t)len);
            memcpy(tmp, sb, (size_t)lb);
            memcpy(tmp + lb, sc, (size_t)lc);
            R[ZEN_A(i)] = val_obj((Obj *)new_string(&gc_, tmp, len));
            if (tmp != stackbuf) free(tmp);
            NEXT();
        }
        CASE(OP_TOSTRING)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = default_to_string(&gc_, R[ZEN_B(i)]);
            NEXT();
        }
        CASE(OP_LEN)
        {
            uint32_t i = *ip;
            Value v = R[ZEN_B(i)];
            if (is_string(v)) R[ZEN_A(i)] = val_int(as_string(v)->length);
            else if (is_array(v)) R[ZEN_A(i)] = val_int(arr_count(as_array(v)));
            else if (is_buffer(v)) R[ZEN_A(i)] = val_int(as_buffer(v)->count);
            else R[ZEN_A(i)] = val_int(0);
            NEXT();
        }

        /* ---------------- maths ---------------- */
#define MATH1(OPNAME, EXPR)                                  \
        CASE(OPNAME)                                         \
        {                                                    \
            uint32_t i = *ip;                                \
            double n = to_number(R[ZEN_B(i)]);               \
            R[ZEN_A(i)] = val_float(EXPR);                   \
            NEXT();                                          \
        }
        MATH1(OP_SIN, sin(n))
        MATH1(OP_COS, cos(n))
        MATH1(OP_TAN, tan(n))
        MATH1(OP_ASIN, asin(n))
        MATH1(OP_ACOS, acos(n))
        MATH1(OP_ATAN, atan(n))
        CASE(OP_ATAN2)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_float(atan2(to_number(R[ZEN_B(i)]), to_number(R[ZEN_C(i)])));
            NEXT();
        }
        MATH1(OP_SQRT, sqrt(n))
        CASE(OP_POW)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_float(pow(to_number(R[ZEN_B(i)]), to_number(R[ZEN_C(i)])));
            NEXT();
        }
        MATH1(OP_LOG, log(n))
        CASE(OP_ABS)
        {
            uint32_t i = *ip;
            Value v = R[ZEN_B(i)];
            if (v.type == VAL_INT)
                R[ZEN_A(i)] = val_int(v.as.integer < 0 ? (int64_t)(-(uint64_t)v.as.integer) : v.as.integer);
            else
                R[ZEN_A(i)] = val_float(fabs(to_number(v)));
            NEXT();
        }
        MATH1(OP_FLOOR, floor(n))
        MATH1(OP_CEIL, ceil(n))
        MATH1(OP_DEG, n * (180.0 / 3.14159265358979323846))
        MATH1(OP_RAD, n * (3.14159265358979323846 / 180.0))
        MATH1(OP_EXP, exp(n))
#undef MATH1
        CASE(OP_CLOCK)
        {
            uint32_t i = *ip;
            R[ZEN_A(i)] = val_float((double)clock() / CLOCKS_PER_SEC);
            NEXT();
        }

        /* ---------------- fused compare + jump ---------------- */
        CASE(OP_LTJMPIFNOT)
        {
            uint32_t i = *ip;
            Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];
            bool less = (vb.type == VAL_INT && vc.type == VAL_INT) ? vb.as.integer < vc.as.integer
                                                                   : to_number(vb) < to_number(vc);
            ++ip;
            if (!less) ip += ZEN_SBX(*ip);
            NEXT();
        }
        CASE(OP_LEJMPIFNOT)
        {
            uint32_t i = *ip;
            Value vb = R[ZEN_B(i)], vc = R[ZEN_C(i)];
            bool le = (vb.type == VAL_INT && vc.type == VAL_INT) ? vb.as.integer <= vc.as.integer
                                                                 : to_number(vb) <= to_number(vc);
            ++ip;
            if (!le) ip += ZEN_SBX(*ip);
            NEXT();
        }
        CASE(OP_EQJMPIFNOT)
        {
            uint32_t i = *ip;
            bool eq = values_equal(R[ZEN_B(i)], R[ZEN_C(i)]);
            ++ip;
            if (!eq) ip += ZEN_SBX(*ip);
            NEXT();
        }
        CASE(OP_NEJMPIFNOT)
        {
            uint32_t i = *ip;
            bool ne = !values_equal(R[ZEN_B(i)], R[ZEN_C(i)]);
            ++ip;
            if (!ne) ip += ZEN_SBX(*ip);
            NEXT();
        }
        CASE(OP_LTIJMPIFNOT)
        {
            uint32_t i = *ip;
            Value vx = R[ZEN_B(i)];
            int64_t imm = (int8_t)ZEN_C(i);
            bool less = (vx.type == VAL_INT) ? vx.as.integer < imm : to_number(vx) < (double)imm;
            ++ip;
            if (!less) ip += ZEN_SBX(*ip);
            NEXT();
        }
        CASE(OP_GTIJMPIFNOT)
        {
            uint32_t i = *ip;
            Value vx = R[ZEN_B(i)];
            int64_t imm = (int8_t)ZEN_C(i);
            bool greater = (vx.type == VAL_INT) ? vx.as.integer > imm : to_number(vx) > (double)imm;
            ++ip;
            if (!greater) ip += ZEN_SBX(*ip);
            NEXT();
        }
        CASE(OP_JMPIFNIL)
        {
            uint32_t i = *ip;
            bool isnil = is_nil(R[ZEN_A(i)]);
            ++ip;
            if (isnil) ip += ZEN_SBX(*ip);
            NEXT();
        }

        CASE(OP_HALT)
        {
            SAVE_IP();
            fiber->state = FIBER_DONE;
            return;
        }

#ifndef ZEN_COMPUTED_GOTO
            } /* switch */
        } /* for */
#endif

#undef DISPATCH
#undef CASE
#undef NEXT
#undef LOAD_STATE
#undef SAVE_IP
#undef RT_ERROR
#undef NUM_BINOP
#undef CHECK_SUSPEND
    }

} /* namespace zen */
