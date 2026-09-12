#ifndef ZEN_EMITTER_H
#define ZEN_EMITTER_H

#include "memory.h"
#include "opcodes.h"

namespace zen
{

    /*
    ** Emitter — Gera bytecode para um ObjFunc.
    **
    ** Responsável por:
    **   1. Crescer code[] e lines[] em paralelo (realloc seguro)
    **   2. Crescer constants[] (pool de constantes)
    **   3. Backpatching de jumps
    **   4. Tracking de linhas para debug
    **
    ** SEGURANÇA DE MEMÓRIA:
    **   - code[] pode ser realocado durante emit (muda de endereço)
    **   - Nunca guardes ponteiro directo para code[] durante compilação
    **   - Usa sempre índices (int offset), não Instruction*
    **   - Após compilação: ObjFunc.code[] NUNCA mais muda
    **   - O IP da VM só aponta para code[] DEPOIS de compilar → seguro
    **
    ** Uso:
    **   Emitter e(&gc);
    **   e.begin("my_func", 2);          // nome, arity
    **   e.emit(ZEN_ENCODE(...), line);   // emite instrução
    **   int hole = e.emit_jump(OP_JMPIFNOT, reg, line);
    **   // ... código do then ...
    **   e.patch_jump(hole);              // backpatch
    **   ObjFunc* fn = e.end(num_regs);   // finaliza, devolve func
    */
    class Emitter
    {
    public:
        Emitter() : gc_(nullptr) {}
        explicit Emitter(GC *gc);

        /* Inicia nova função */
        void begin(const char *name, int arity, const char *source = nullptr);

        /* --- Emitir instruções --- */
        int emit(Instruction instr, int line);
        int emit_abc(OpCode op, int a, int b, int c, int line);
        int emit_abx(OpCode op, int a, int bx, int line);
        int emit_asbx(OpCode op, int a, int sbx, int line);

        /* --- Constantes --- */
        int add_constant(Value val); /* retorna índice na pool */
        int add_string_constant(const char *str, int len = -1);
        int add_escaped_string_constant(const char *str, int len);
        int add_verbatim_string_constant(const char *str, int len);

        /* --- Jumps (backpatching) --- */
        int emit_jump(OpCode op, int a, int line);      /* retorna offset do hole */
        void patch_jump(int offset);                    /* preenche com distância actual */

        /* Highest offset any backpatched jump now targets.
        **
        ** A fused compare-and-branch deletes the comparison and emits a
        ** 2-word instruction in its place, which moves the branch one word
        ** later. That is invisible to a jump already patched to land on it —
        ** `a() || b < c` patches the short-circuit JMPIF to the branch, and
        ** after fusing that target is the *second* word of the fused
        ** instruction, so the jump lands mid-instruction. Fusion checks this
        ** and declines when anything already points at the current offset. */
        int last_patched_target() const { return last_patched_target_; }
        void patch_jump_to(int offset, int target);     /* preenche com destino explícito */
        int emit_loop(int loop_start, int a, int line); /* jump para trás */

        /* --- Fused compare+jump (2-word superinstructions) --- */
        int emit_lt_jmpifnot(int b, int c, int line); /* retorna offset do sBx word */
        int emit_le_jmpifnot(int b, int c, int line);
        int emit_cmp_jmpifnot(OpCode op, int b, int c, int line);
        void patch_fused_jump(int sbx_offset); /* patch the sBx word */
    private:
        int last_patched_target_ = -1;
    public:

        /* --- Fused global call (2-word: CALLGLOBAL + global_idx) --- */
        void emit_callglobal(int a, int nargs, int nresults, int global_idx, int line);

        /* --- Finalizar --- */
        ObjFunc *end(int num_regs);

        /* --- Accessors --- */
        int current_offset() const { return func_->code_count; }
        /* Discard instructions emitted after `offset` (used to roll back a
           speculative fast-path like the numeric-for optimizer). Line/constant
           side tables are append-only, so any orphaned entries are simply
           unused — harmless. */
        void rewind_to(int offset)
        {
            if (offset >= 0 && offset <= func_->code_count)
                func_->code_count = offset;
        }
        int last_line() const { return last_line_; }
        Instruction instruction_at(int offset) const { return func_->code[offset]; }
        void rewrite_opcode_at(int offset, OpCode new_op)
        {
            func_->code[offset] = (func_->code[offset] & 0x00FFFFFF) | ((uint32_t)new_op << 24);
        }
        /* Retarget the destination register of an ABC instruction. */
        void rewrite_a_at(int offset, int a)
        {
            func_->code[offset] = (func_->code[offset] & 0xFF00FFFF) | ((uint32_t)(a & 0xFF) << 16);
        }
        int line_at(int offset) const { return func_->lines ? func_->lines[offset] : last_line_; }

        /* Error from escape processing */
        bool has_escape_error() const { return escape_error_[0] != '\0'; }
        const char *escape_error() const { return escape_error_; }
        void clear_escape_error() { escape_error_[0] = '\0'; }

    private:
        void grow_code();
        void grow_constants();

        GC *gc_;
        ObjFunc *func_; /* func em construção */
        int last_line_;
        char escape_error_[128] = {};
    };

} /* namespace zen */

#endif /* ZEN_EMITTER_H */
