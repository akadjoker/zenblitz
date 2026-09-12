#ifndef ZEN_DEBUG_H
#define ZEN_DEBUG_H

#include "object.h"
#include "opcodes.h"

namespace zen
{
    /* Disassembler and diagnostics. */
    void disassemble_func(ObjFunc *func, const char *label = nullptr);
    int disassemble_instruction(ObjFunc *func, int offset);
    void print_value(Value val);
    void println_value(Value val);
    void dump_stack(Fiber *fiber);
    void dump_constants(ObjFunc *func);
    const char *opcode_name(OpCode op);
}

#endif /* ZEN_DEBUG_H */
