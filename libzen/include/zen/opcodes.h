#ifndef ZEN_OPCODES_H
#define ZEN_OPCODES_H

/*
** opcodes.h — VM instruction set (reduced for Blitz Basic).
**
** Format: 32 bits  [ opcode(8) | A(8) | B(8) | C(8) ]
** or:     32 bits  [ opcode(8) | A(8) | Bx(16) ]       constants / globals
** or:     32 bits  [ opcode(8) | A(8) | sBx(16) ]      jumps
**
** A  = destination register
** B  = source register or short index
** C  = second source register
** Bx = 16-bit index (constants, globals)
** sBx = signed 16-bit offset (jumps)
*/

#include <cstdint>

namespace zen
{
    enum OpCode : uint8_t
    {
        /* --- Load/Store --- */
        OP_LOADNIL,  /* R[A] = nil                              */
        OP_LOADBOOL, /* R[A] = (bool)B; if C: pc++              */
        OP_LOADK,    /* R[A] = constants[Bx]                    */
        OP_LOADI,    /* R[A] = (int)sBx                         */
        OP_MOVE,     /* R[A] = R[B]                             */

        /* --- Globals --- */
        OP_GETGLOBAL, /* R[A] = globals[Bx]                     */
        OP_SETGLOBAL, /* globals[Bx] = R[A]                     */

        /* --- Arithmetic --- */
        OP_ADD,  /* R[A] = R[B] + R[C]  (int/float, string concat)   */
        OP_SUB,  /* R[A] = R[B] - R[C]                                */
        OP_MUL,  /* R[A] = R[B] * R[C]                                */
        OP_DIV,  /* R[A] = R[B] / R[C]  (float)                       */
        OP_MOD,  /* R[A] = R[B] % R[C]                                */
        OP_IDIV, /* R[A] = R[B] / R[C]  (integer)                     */
        OP_NEG,  /* R[A] = -R[B]                                      */
        OP_ADDI, /* R[A] = R[B] + (signed)C                           */
        OP_SUBI, /* R[A] = R[B] - (signed)C                           */
        OP_MULI, /* R[A] = R[B] * (signed)C                           */

        /* --- Bitwise --- */
        OP_BAND, /* R[A] = R[B] & R[C]                      */
        OP_BOR,  /* R[A] = R[B] | R[C]                      */
        OP_BXOR, /* R[A] = R[B] ^ R[C]                      */
        OP_BNOT, /* R[A] = ~R[B]                            */
        OP_SHL,  /* R[A] = R[B] << R[C]                     */
        OP_SHR,  /* R[A] = R[B] >> R[C]  (arithmetic)       */

        /* --- Comparison (bool result in R[A]) --- */
        OP_EQ,  /* R[A] = (R[B] == R[C])                   */
        OP_LT,  /* R[A] = (R[B] <  R[C])                   */
        OP_LE,  /* R[A] = (R[B] <= R[C])                   */
        OP_NOT, /* R[A] = !R[B]                            */

        /* --- Jumps --- */
        OP_JMP,      /* pc += sBx                          */
        OP_JMPIF,    /* if truthy(R[A]): pc += sBx         */
        OP_JMPIFNOT, /* if !truthy(R[A]): pc += sBx        */

        /* --- Calls --- */
        OP_CALL,       /* R[A](R[A+1]..R[A+B]) -> R[A]..R[A+C-1]           */
        OP_CALLGLOBAL, /* globals[Bx(word2)](R[A+1]..+B) -> R[A]..+C, 2 words */
        OP_RETURN,     /* return R[A]..R[A+B-1] (B = nresults)             */
        OP_RETURNNIL,  /* return nil                                       */

        /* --- Objects --- */
        OP_NEWARRAY,     /* R[A] = []                                  */
        OP_NEWBUFFER,    /* R[A] = TypedArray(R[B]); C = BufferType    */
        OP_APPEND,       /* R[A].push(R[B])                            */
        OP_GETFIELD_IDX, /* R[A] = R[B].fields[C]                      */
        OP_SETFIELD_IDX, /* R[A].fields[B] = R[C]                      */
        OP_GETINDEX,     /* R[A] = R[B][R[C]]                          */
        OP_SETINDEX,     /* R[A][R[B]] = R[C]                          */

        /* --- Strings --- */
        OP_CONCAT,   /* R[A] = R[B] .. R[C]  (always a fresh string)  */
        OP_TOSTRING, /* R[A] = tostring(R[B])                          */
        OP_LEN,      /* R[A] = #R[B]                                   */

        /* --- Maths (keyword-level, no call overhead) --- */
        OP_SIN, OP_COS, OP_TAN, OP_ASIN, OP_ACOS, OP_ATAN, OP_ATAN2,
        OP_SQRT, OP_POW, OP_LOG, OP_ABS, OP_FLOOR, OP_CEIL, OP_DEG, OP_RAD, OP_EXP,
        OP_CLOCK, /* R[A] = high-res clock (seconds)         */

        /* --- Fused compare + jump (2 words: word2 = sBx) --- */
        OP_LTJMPIFNOT,  /* if !(R[B] <  R[C]): pc += sBx       */
        OP_LEJMPIFNOT,  /* if !(R[B] <= R[C]): pc += sBx       */
        OP_EQJMPIFNOT,  /* if !(R[B] == R[C]): pc += sBx       */
        OP_NEJMPIFNOT,  /* if !(R[B] != R[C]): pc += sBx       */
        OP_LTIJMPIFNOT, /* if !(R[B] <  C): pc += sBx  (imm C) */
        OP_GTIJMPIFNOT, /* if !(R[B] >  C): pc += sBx  (imm C) */
        OP_JMPIFNIL,    /* if R[A] is nil: pc += sBx           */

        /* OP_HALT must stay last: every program ends with it and the dispatch
           table and the disassembler name table are sized from it. */
        OP_HALT,
    };

/* Encode/Decode — ABC format */
#define ZEN_ENCODE(op, a, b, c) ((uint32_t)((op) << 24) | ((a) << 16) | ((b) << 8) | (c))
#define ZEN_OP(i) ((uint8_t)(((i) >> 24) & 0xFF))
#define ZEN_A(i) ((uint8_t)(((i) >> 16) & 0xFF))
#define ZEN_B(i) ((uint8_t)(((i) >> 8) & 0xFF))
#define ZEN_C(i) ((uint8_t)((i) & 0xFF))

/* Encode/Decode — ABx format (16-bit unsigned operand) */
#define ZEN_ENCODE_BX(op, a, bx) ((uint32_t)((op) << 24) | ((a) << 16) | ((bx) & 0xFFFF))
#define ZEN_BX(i) ((uint16_t)((i) & 0xFFFF))

/* Encode/Decode — AsBx format (16-bit signed offset) */
#define ZEN_ENCODE_SBX(op, a, sbx) ((uint32_t)((op) << 24) | ((a) << 16) | (((sbx) + 32768) & 0xFFFF))
#define ZEN_SBX(i) ((int)((int)((i) & 0xFFFF) - 32768))

} /* namespace zen */

#endif /* ZEN_OPCODES_H */
