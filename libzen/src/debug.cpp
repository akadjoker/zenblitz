#include "debug.h"
#include "opcodes.h"

namespace zen
{
    static const char *s_opnames[] = {
        "LOADNIL",
        "LOADBOOL",
        "LOADK",
        "LOADI",
        "MOVE",
        "GETGLOBAL",
        "SETGLOBAL",
        "ADD",
        "SUB",
        "MUL",
        "DIV",
        "MOD",
        "IDIV",
        "NEG",
        "ADDI",
        "SUBI",
        "BAND",
        "BOR",
        "BXOR",
        "BNOT",
        "SHL",
        "SHR",
        "EQ",
        "LT",
        "LE",
        "NOT",
        "JMP",
        "JMPIF",
        "JMPIFNOT",
        "CALL",
        "CALLGLOBAL",
        "RETURN",
        "RETURNNIL",
        "NEWARRAY",
        "NEWBUFFER",
        "APPEND",
        "GETFIELD_IDX",
        "SETFIELD_IDX",
        "GETINDEX",
        "SETINDEX",
        "CONCAT",
        "TOSTRING",
        "LEN",
        "SIN",
        "COS",
        "TAN",
        "ASIN",
        "ACOS",
        "ATAN",
        "ATAN2",
        "SQRT",
        "POW",
        "LOG",
        "ABS",
        "FLOOR",
        "CEIL",
        "DEG",
        "RAD",
        "EXP",
        "CLOCK",
        "LTJMPIFNOT",
        "LEJMPIFNOT",
        "EQJMPIFNOT",
        "NEJMPIFNOT",
        "LTIJMPIFNOT",
        "GTIJMPIFNOT",
        "JMPIFNIL",
        "HALT",
    };

    static_assert(sizeof(s_opnames) / sizeof(s_opnames[0]) == (size_t)OP_HALT + 1,
                  "s_opnames is out of sync with the OpCode enum");

    const char *opcode_name(OpCode op)
    {
        int idx = (int)op;
        int count = (int)(sizeof(s_opnames) / sizeof(s_opnames[0]));
        if (idx >= 0 && idx < count)
            return s_opnames[idx];
        return "???";
    }

    void print_value(Value val)
    {
        switch (val.type)
        {
        case VAL_NIL: printf("nil"); break;
        case VAL_BOOL: printf(val.as.boolean ? "true" : "false"); break;
        case VAL_INT: printf("%lld", (long long)val.as.integer); break;
        case VAL_FLOAT: printf("%g", val.as.number); break;
        case VAL_OBJ:
        {
            Obj *obj = val.as.obj;
            switch (obj->type)
            {
            case OBJ_STRING: printf("\"%s\"", ((ObjString *)obj)->chars); break;
            case OBJ_FUNC:
            {
                ObjFunc *fn = (ObjFunc *)obj;
                printf("<fn %s>", fn->name ? fn->name->chars : "script");
                break;
            }
            case OBJ_NATIVE: printf("<native %s>", ((ObjNative *)obj)->name->chars); break;
            case OBJ_ARRAY: printf("<array[%d]>", arr_count((ObjArray *)obj)); break;
            case OBJ_BUFFER:
            {
                ObjBuffer *b = (ObjBuffer *)obj;
                static const char *bnames[] = {"Int8", "Int16", "Int32", "Uint8", "Uint16", "Uint32", "Float32", "Float64"};
                printf("<buffer %s[%d]>", bnames[b->btype], b->count);
                break;
            }
            case OBJ_STRUCT_DEF: printf("<type %s>", ((ObjStructDef *)obj)->name->chars); break;
            case OBJ_STRUCT: printf("<%s>", ((ObjStruct *)obj)->def->name->chars); break;
            }
            break;
        }
        case VAL_PTR: printf("<ptr %p>", val.as.pointer); break;
        }
    }

    void println_value(Value val)
    {
        print_value(val);
        printf("\n");
    }

    enum InstrFormat { FMT_ABC, FMT_ABX, FMT_ASBX };

    static InstrFormat instr_format(OpCode op)
    {
        switch (op)
        {
        case OP_LOADK:
        case OP_GETGLOBAL:
        case OP_SETGLOBAL:
            return FMT_ABX;
        case OP_JMP:
        case OP_JMPIF:
        case OP_JMPIFNOT:
        case OP_LOADI:
            return FMT_ASBX;
        default:
            return FMT_ABC;
        }
    }

    static bool is_two_word(OpCode op)
    {
        switch (op)
        {
        case OP_CALLGLOBAL:
        case OP_LTJMPIFNOT:
        case OP_LEJMPIFNOT:
        case OP_EQJMPIFNOT:
        case OP_NEJMPIFNOT:
        case OP_LTIJMPIFNOT:
        case OP_GTIJMPIFNOT:
        case OP_JMPIFNIL:
            return true;
        default:
            return false;
        }
    }

    int disassemble_instruction(ObjFunc *func, int offset)
    {
        uint32_t instr = func->code[offset];
        OpCode op = (OpCode)ZEN_OP(instr);
        int a = ZEN_A(instr);

        if (offset > 0 && func->lines[offset] == func->lines[offset - 1])
            printf("   | ");
        else
            printf("%4d ", func->lines[offset]);

        printf("%04d  %-12s", offset, opcode_name(op));

        switch (instr_format(op))
        {
        case FMT_ABC:
        {
            int b = ZEN_B(instr);
            int c = ZEN_C(instr);
            printf("  A=%-3d B=%-3d C=%-3d", a, b, c);
            if (op == OP_CALL)
                printf("  ; R[%d](%d args) -> %d results", a, b, c);
            else if (op == OP_RETURN)
                printf("  ; return R[%d]..R[%d]", a, a + b - 1);
            else if (op == OP_ADDI || op == OP_SUBI || op == OP_LTIJMPIFNOT || op == OP_GTIJMPIFNOT)
                printf("  ; imm=%d", (int)(int8_t)c);
            break;
        }
        case FMT_ABX:
        {
            int bx = ZEN_BX(instr);
            printf("  A=%-3d Bx=%-5d", a, bx);
            if (op == OP_LOADK && bx < func->const_count)
            {
                printf("  ; R[%d] = ", a);
                print_value(func->constants[bx]);
            }
            break;
        }
        case FMT_ASBX:
        {
            int sbx = ZEN_SBX(instr);
            printf("  A=%-3d sBx=%-5d", a, sbx);
            if (op == OP_JMP || op == OP_JMPIF || op == OP_JMPIFNOT)
                printf("  ; -> %04d", offset + 1 + sbx);
            else if (op == OP_LOADI)
                printf("  ; R[%d] = %d", a, sbx);
            break;
        }
        }
        printf("\n");

        if (is_two_word(op))
        {
            uint32_t word2 = func->code[offset + 1];
            if (op == OP_CALLGLOBAL)
                printf("      %04d  (global=%d)\n", offset + 1, (int)ZEN_BX(word2));
            else
                printf("      %04d  (-> %04d)\n", offset + 1, offset + 2 + ZEN_SBX(word2));
            return offset + 2;
        }
        return offset + 1;
    }

    void disassemble_func(ObjFunc *func, const char *label)
    {
        const char *name = label ? label : (func->name ? func->name->chars : "<script>");
        printf("=== %s (arity=%d, regs=%d, code=%d) ===\n",
               name, func->arity, func->num_regs, func->code_count);
        int offset = 0;
        while (offset < func->code_count)
            offset = disassemble_instruction(func, offset);
        printf("\n");
    }

    void dump_constants(ObjFunc *func)
    {
        printf("  constants (%d):\n", func->const_count);
        for (int i = 0; i < func->const_count; i++)
        {
            printf("    [%3d] ", i);
            println_value(func->constants[i]);
        }
    }

    void dump_stack(Fiber *fiber)
    {
        printf("  stack: [ ");
        for (Value *slot = fiber->stack; slot < fiber->stack_top; slot++)
        {
            print_value(*slot);
            printf(" | ");
        }
        printf("]\n");
    }

} /* namespace zen */
