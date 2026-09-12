#include "debug.h"
#include "opcodes.h"

namespace zen
{

    /* --- Nomes dos opcodes --- */
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
        "ADD_OBJ",
        "SUB_OBJ",
        "MUL_OBJ",
        "DIV_OBJ",
        "MOD_OBJ",
        "NEG_OBJ",
        "EQ_OBJ",
        "LT_OBJ",
        "LE_OBJ",
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
        "CLOSURE",
        "GETUPVAL",
        "SETUPVAL",
        "CLOSE",
        "NEWFIBER",
        "RESUME",
        "YIELD",
        "FRAME",
        "FRAME_N",
        "SPAWN",
        "PROC_GET",
        "PROC_SET",
        "NEWARRAY",
        "NEWMAP",
        "NEWSET",
        "NEWBUFFER",
        "APPEND",
        "SETADD",
        "GETFIELD",
        "SETFIELD",
        "GETFIELD_IDX",
        "SETFIELD_IDX",
        "GETINDEX",
        "SETINDEX",
        "INVOKE",
        "INVOKE_VT",
        "SUPER_INVOKE",
        "CONCAT",
        "STRADD",
        "TOSTRING",
        "TOSTRING_OBJ",
        "LEN",
        "PRINT",
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
        "FORPREP",
        "FORLOOP",
        "GETFIELD_MUL",
        "GETFIELD_SUB",
        "ITER_ELEM",
        "CONTAINS",
        "DELINDEX",
        "GETSLICE",
        "IS",
        "TAILCALL",
        "RETURNNIL",
        "JMPIFNIL",
        "LTIJMPIFNOT",
        "GTIJMPIFNOT",
        "EQJMPIFNOT",
        "NEJMPIFNOT",
        "INVOKE_VT_FAST",
        "CALL_GENERIC",
        "INVOKE_GENERIC",
        "HALT",
    };

    /* The table is indexed by OpCode: keep it in lockstep with the enum.
    ** OP_HALT is the last opcode, so its index + 1 is the expected size. */
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

    /* --- Print value --- */
    void print_value(Value val)
    {
        switch (val.type)
        {
        case VAL_NIL:
            printf("nil");
            break;
        case VAL_BOOL:
            printf(val.as.boolean ? "true" : "false");
            break;
        case VAL_INT:
            printf("%lld", (long long)val.as.integer);
            break;
        case VAL_FLOAT:
            printf("%g", val.as.number);
            break;
        case VAL_OBJ:
        {
            Obj *obj = val.as.obj;
            switch (obj->type)
            {
            case OBJ_STRING:
                printf("\"%s\"", ((ObjString *)obj)->chars);
                break;
            case OBJ_FUNC:
            {
                ObjFunc *fn = (ObjFunc *)obj;
                if (fn->name)
                    printf("<fn %s>", fn->name->chars);
                else
                    printf("<fn script>");
                break;
            }
            case OBJ_NATIVE:
                printf("<native %s>", ((ObjNative *)obj)->name->chars);
                break;
            case OBJ_CLOSURE:
            {
                ObjClosure *cl = (ObjClosure *)obj;
                if (cl->func->name)
                    printf("<closure %s>", cl->func->name->chars);
                else
                    printf("<closure>");
                break;
            }
            case OBJ_FIBER:
                printf("<fiber %p>", (void *)obj);
                break;
            case OBJ_UPVALUE:
                printf("<upvalue>");
                break;
            case OBJ_ARRAY:
                printf("<array[%d]>", arr_count((ObjArray *)obj));
                break;
            case OBJ_MAP:
                printf("<map[%d]>", ((ObjMap *)obj)->count);
                break;
            case OBJ_SET:
                printf("<set[%d]>", ((ObjSet *)obj)->count);
                break;
            case OBJ_BUFFER:
            {
                ObjBuffer *b = (ObjBuffer *)obj;
                static const char *bnames[] = {"Int8Array","Int16Array","Int32Array","Uint8Array","Uint16Array","Uint32Array","Float32Array","Float64Array"};
                printf("<%s[%d]>", bnames[b->btype], b->count);
                break;
            }
            case OBJ_STRUCT_DEF:
                printf("<struct_def %s>", ((ObjStructDef *)obj)->name->chars);
                break;
            case OBJ_STRUCT:
                printf("<struct %s>", ((ObjStruct *)obj)->def->name->chars);
                break;
            case OBJ_CLASS:
                printf("<class %s>", ((ObjClass *)obj)->name->chars);
                break;
            case OBJ_INSTANCE:
                printf("<instance %s>", ((ObjInstance *)obj)->klass->name->chars);
                break;
            case OBJ_PROCESS:
                printf("<process>");
                break;
            case OBJ_NATIVE_STRUCT_DEF:
                printf("<native_struct_def>");
                break;
            case OBJ_NATIVE_STRUCT:
                printf("<native_struct>");
                break;
            }
            break;
        }
        case VAL_PTR:
            printf("<ptr %p>", val.as.pointer);
            break;
        }
    } 

    void println_value(Value val)
    {
        print_value(val);
        printf("\n");
    }

    /* --- Instruction format detection --- */
    enum InstrFormat
    {
        FMT_ABC,
        FMT_ABX,
        FMT_ASBX
    };

    static InstrFormat instr_format(OpCode op)
    {
        switch (op)
        {
        case OP_LOADK:
        case OP_GETGLOBAL:
        case OP_SETGLOBAL:
        case OP_CLOSURE:
            return FMT_ABX;
        case OP_JMP:
        case OP_JMPIF:
        case OP_JMPIFNOT:
        case OP_LOADI:
        case OP_FORPREP:
        case OP_FORLOOP:
            return FMT_ASBX;
        default:
            return FMT_ABC;
        }
    }

    /* --- Disassemble one instruction --- */
    int disassemble_instruction(ObjFunc *func, int offset)
    {
        uint32_t instr = func->code[offset];
        OpCode op = (OpCode)ZEN_OP(instr);
        int a = ZEN_A(instr);

        /* Line number */
        if (offset > 0 && func->lines[offset] == func->lines[offset - 1])
        {
            printf("   | ");
        }
        else
        {
            printf("%4d ", func->lines[offset]);
        }

        /* Offset */
        printf("%04d  %-12s", offset, opcode_name(op));

        /* Operands */
        InstrFormat fmt = instr_format(op);
        switch (fmt)
        {
        case FMT_ABC:
        {
            int b = ZEN_B(instr);
            int c = ZEN_C(instr);
            printf("  A=%-3d B=%-3d C=%-3d", a, b, c);

            /* Extra info para LOADBOOL */
            if (op == OP_LOADBOOL)
            {
                printf("  ; R[%d] = %s%s", a, b ? "true" : "false",
                       c ? " (skip next)" : "");
            }
            /* Extra info para ADD/SUB etc */
            else if (op >= OP_ADD && op <= OP_MOD)
            {
                printf("  ; R[%d] = R[%d] op R[%d]", a, b, c);
            }
            /* CALL */
            else if (op == OP_CALL)
            {
                printf("  ; R[%d](%d args) -> %d results", a, b, c);
            }
            /* RETURN */
            else if (op == OP_RETURN)
            {
                printf("  ; return R[%d]..R[%d] (%d vals)", a, a + b - 1, b);
            }
            break;
        }
        case FMT_ABX:
        {
            int bx = ZEN_BX(instr);
            printf("  A=%-3d Bx=%-5d", a, bx);

            /* Extra: show constant value */
            if (op == OP_LOADK && bx < func->const_count)
            {
                printf("  ; R[%d] = ", a);
                print_value(func->constants[bx]);
            }
            else if (op == OP_CLOSURE && bx < func->const_count)
            {
                printf("  ; R[%d] = closure(K[%d])", a, bx);
            }
            break;
        }
        case FMT_ASBX:
        {
            int sbx = ZEN_SBX(instr);
            printf("  A=%-3d sBx=%-5d", a, sbx);

            /* Extra: show jump target */
            if (op == OP_JMP || op == OP_JMPIF || op == OP_JMPIFNOT ||
                op == OP_FORPREP || op == OP_FORLOOP)
            {
                printf("  ; -> %04d", offset + 1 + sbx);
            }
            else if (op == OP_LOADI)
            {
                printf("  ; R[%d] = %d", a, sbx);
            }
            break;
        }
        }

        printf("\n");

        /* 3-word OP_SUPER_INVOKE */
        if (op == OP_SUPER_INVOKE)
        {
            uint32_t word2 = func->code[offset + 1];
            uint32_t word3 = func->code[offset + 2];
            int sel = (int)(word2 >> 16);
            int nki = (int)(word2 & 0xFFFF);
            printf("      %04d  (sel=%d, name_ki=%d", offset + 1, sel, nki);
            if (nki < func->const_count)
            {
                printf(" = ");
                print_value(func->constants[nki]);
            }
            printf(", parent_ki=%d)\n", word3);
            return offset + 3;
        }

        /* 3-word OP_INVOKE_GENERIC: OP_INVOKE's two words plus ngeneric */
        if (op == OP_INVOKE_GENERIC)
        {
            uint32_t word2 = func->code[offset + 1];
            uint32_t word3 = func->code[offset + 2];
            int sel = (int)(word2 >> 16);
            int nki = (int)(word2 & 0xFFFF);
            printf("      %04d  (sel=%d, name_ki=%d", offset + 1, sel, nki);
            if (nki < func->const_count)
            {
                printf(" = ");
                print_value(func->constants[nki]);
            }
            printf(")\n");
            printf("      %04d  (ngeneric=%d)\n", offset + 2, (int)word3);
            return offset + 3;
        }

        /* 2-word superinstructions: skip the operand word */
        if (op == OP_LTJMPIFNOT || op == OP_LEJMPIFNOT || op == OP_CALLGLOBAL ||
            op == OP_INVOKE || op == OP_GETFIELD_MUL || op == OP_GETFIELD_SUB ||
            op == OP_CALL_GENERIC)
        {
            uint32_t word2 = func->code[offset + 1];
            if (op == OP_INVOKE)
            {
                printf("      %04d  (name_ki=%d", offset + 1, word2);
                if ((int)word2 < func->const_count)
                {
                    printf(" = ");
                    print_value(func->constants[word2]);
                }
                printf(")\n");
            }
            else if (op == OP_CALL_GENERIC)
            {
                printf("      %04d  (ngeneric=%d)\n", offset + 1, (int)(word2 & 0xFFFF));
            }
            return offset + 2;
        }

        return offset + 1;
    }

    /* --- Disassemble full function --- */
    void disassemble_func(ObjFunc *func, const char *label)
    {
        const char *name = label ? label : (func->name ? func->name->chars : "<script>");
        printf("=== %s (arity=%d, regs=%d, code=%d) ===\n",
               name, func->arity, func->num_regs, func->code_count);

        int offset = 0;
        while (offset < func->code_count)
        {
            offset = disassemble_instruction(func, offset);
        }
        printf("\n");
    }

    /* --- Dump constants --- */
    void dump_constants(ObjFunc *func)
    {
        printf("  constants (%d):\n", func->const_count);
        for (int i = 0; i < func->const_count; i++)
        {
            printf("    [%3d] ", i);
            println_value(func->constants[i]);
        }
    }

    /* --- Dump fiber stack --- */
    void dump_stack(ObjFiber *fiber)
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
