/*
** bb_codegen.cpp — Zen bytecode generation for the Blitz AST.
**
** Register model:
**   R[0..nparams-1]   parameters
**   R[nparams..nlocals-1]  locals (Blitz auto-declared locals included)
**   R[nlocals..]      expression temporaries (stack discipline)
**
** Globals, functions, arrays and Type lists live in Zen globals:
**   _v<name>  Global variables        _f<name>  functions (user + native)
**   _a<name>  Dim arrays (+ _a<name>_s<k> per-dimension sizes)
**   _t<name>  Type classes, _t<name>_first/_last list heads
*/
#include "bb_codegen.h"
#include "object.h"
#include "opcodes.h"

using namespace zen;

namespace bb
{
    static const int MAX_REGS = 240;

    static bool smallIntConst(ExprNode *e, int &v)
    {
        ConstNode *c = e->constNode();
        if (!c || e->sem_type != Type::int_type) return false;
        long long n = c->intValue();
        if (n < -128 || n > 127) return false;
        v = (int)n;
        return true;
    }

    BBGen::BBGen(VM *vm, BBRuntime *rt, bool debug)
        : f(0), line(0), vm(vm), rt(rt), debug(debug) {}

    BBGen::~BBGen() {}

    /* ================= registers ================= */
    int BBGen::allocTemp()
    {
        int r = f->top++;
        if (f->top > f->maxreg) f->maxreg = f->top;
        if (f->top > MAX_REGS) Node::ex("Expression too complex (out of registers)");
        return r;
    }

    /* Place `reg` (result of an expression evaluated with temps >= save)
       according to `dest`, releasing temporaries. */
    int BBGen::finish(int reg, int dest, int save)
    {
        if (dest >= 0)
        {
            if (reg != dest) move(dest, reg);
            freeTo(save);
            return dest;
        }
        if (reg < save)
        {
            /* a variable's own register — nothing to keep */
            freeTo(save);
            return reg;
        }
        if (reg != save) move(save, reg);
        freeTo(save + 1);
        if (save + 1 > f->maxreg) f->maxreg = save + 1;
        return save;
    }

    /* ================= raw emission ================= */
    int BBGen::emitABC(OpCode op, int a, int b, int c)
    {
        return f->em.emit_abc(op, a, b, c, line);
    }

    int BBGen::emitABx(OpCode op, int a, int bx)
    {
        if (bx < 0 || bx > 0xFFFF) Node::ex("Too many constants/globals");
        return f->em.emit_abx(op, a, bx, line);
    }

    int BBGen::emitJump(OpCode op, int a)
    {
        return f->em.emit_jump(op, a, line);
    }

    int BBGen::emitFused(OpCode op, int b, int c)
    {
        return f->em.emit_cmp_jmpifnot(op, b, c, line);
    }

    int BBGen::emitJumpIfNil(int a)
    {
        f->em.emit(ZEN_ENCODE(OP_JMPIFNIL, a, 0, 0), line);
        return f->em.emit(ZEN_ENCODE_SBX(OP_JMP, 0, 0), line);
    }

    void BBGen::patchTo(int hole, int target)
    {
        int jump = target - (hole + 1);
        if (jump > 32767 || jump < -32768) Node::ex("Code block too large (jump out of range)");
        f->em.patch_jump_to(hole, target);
    }

    /* ================= constants ================= */
    void BBGen::loadInt(int reg, long long v)
    {
        if (v >= -32768 && v <= 32767) f->em.emit_asbx(OP_LOADI, reg, (int)v, line);
        else emitABx(OP_LOADK, reg, f->em.add_constant(val_int(v)));
    }

    void BBGen::loadFloat(int reg, double v)
    {
        emitABx(OP_LOADK, reg, f->em.add_constant(val_float(v)));
    }

    void BBGen::loadStr(int reg, const string &s)
    {
        emitABx(OP_LOADK, reg, f->em.add_string_constant(s.data(), (int)s.size()));
    }

    void BBGen::loadNil(int reg)
    {
        emitABC(OP_LOADNIL, reg, 0, 0);
    }

    void BBGen::loadDefault(int reg, Type *t)
    {
        if (t == Type::float_type) loadFloat(reg, 0.0);
        else if (t == Type::string_type) loadStr(reg, "");
        else if (t->structType() || t->vectorType()) loadNil(reg);
        else loadInt(reg, 0);
    }

    void BBGen::move(int dst, int src)
    {
        if (dst != src) emitABC(OP_MOVE, dst, src, 0);
    }

    /* ================= calls ================= */
    void BBGen::callGlobal(int base, int nargs, int gidx, int nresults)
    {
        if (gidx < 0 || gidx > 0xFFFF) Node::ex("bad global index");
        f->em.emit_callglobal(base, nargs, nresults, gidx, line);
    }

    int BBGen::callHelper1(int gidx, ExprNode *arg, int dest)
    {
        int save = top();
        int base = allocTemp();
        int a = allocTemp();
        exprInto(arg, a);
        callGlobal(base, 1, gidx);
        return finish(base, dest, save);
    }

    int BBGen::callHelper1r(int gidx, int argReg, int dest)
    {
        int save = top();
        int base = allocTemp();
        int a = allocTemp();
        move(a, argReg);
        callGlobal(base, 1, gidx);
        return finish(base, dest, save);
    }

    int BBGen::exprInto(ExprNode *e, int dest)
    {
        int r = e->emit(*this, dest);
        if (r != dest) move(dest, r);
        return dest;
    }

    /* ================= labels ================= */
    void BBGen::defineLabel(const string &l)
    {
        f->labels[l] = here();
    }

    void BBGen::jumpToLabel(const string &l)
    {
        int hole = emitJump(OP_JMP, 0);
        f->pending.push_back(std::make_pair(hole, l));
    }

    void BBGen::resolveLabels()
    {
        for (size_t k = 0; k < f->pending.size(); ++k)
        {
            map<string, int>::iterator it = f->labels.find(f->pending[k].second);
            if (it == f->labels.end()) Node::ex("Undefined label '" + f->pending[k].second + "'");
            patchTo(f->pending[k].first, it->second);
        }
        f->pending.clear();
    }

    /* ================= globals / arrays / types ================= */
    int BBGen::globalOf(Decl *d)
    {
        if (d->offset < 0) Node::ex("INTERNAL: global '" + d->name + "' has no slot");
        return d->offset;
    }

    ArrayInfo &BBGen::arrayOf(Decl *d)
    {
        map<Decl *, ArrayInfo>::iterator it = arrays.find(d);
        if (it == arrays.end()) Node::ex("INTERNAL: array '" + d->name + "' not prepared");
        return it->second;
    }

    BBTypeInfo *BBGen::typeOf(Type *t)
    {
        StructType *s = t->structType();
        if (!s || s->rt_index < 0) Node::ex("INTERNAL: type not registered");
        return rt->types[s->rt_index];
    }

    int BBGen::gosubReg()
    {
        return f->gosubReg;
    }

    /* ================= conditions ================= */
    /* is this expression the literal Null (possibly wrapped in casts)? */
    static bool isNullExpr(ExprNode *e)
    {
        while (CastNode *c = dynamic_cast<CastNode *>(e)) e = c->expr;
        return dynamic_cast<NullNode *>(e) != 0 || e->sem_type == Type::null_type;
    }

    static OpCode fusedFor(int op, bool &swap)
    {
        swap = false;
        switch (op)
        {
        case '<': return OP_LTJMPIFNOT;
        case '>': swap = true; return OP_LTJMPIFNOT;
        case LE: return OP_LEJMPIFNOT;
        case GE: swap = true; return OP_LEJMPIFNOT;
        case '=': return OP_EQJMPIFNOT;
        case NE: return OP_NEJMPIFNOT;
        }
        return OP_HALT;
    }

    /* inverse relation (for jump-if-true) */
    static int invRel(int op)
    {
        switch (op)
        {
        case '<': return GE;
        case '>': return LE;
        case LE: return '>';
        case GE: return '<';
        case '=': return NE;
        case NE: return '=';
        }
        return op;
    }

    static void emitRelJump(BBGen &g, int op, Type *opType, int l, int r, bool jumpIfTrue, vector<int> &holes)
    {
        /* jump-if-false of `op` == jump-if-true of inverse; fused ops are jump-if-NOT */
        int eff = jumpIfTrue ? invRel(op) : op;
        if (opType == Type::string_type && eff != '=' && eff != NE)
        {
            /* strings: LT/LE produce a bool, then JMPIF/JMPIFNOT */
            int t = g.allocTemp();
            switch (op)
            {
            case '<': g.emitABC(OP_LT, t, l, r); break;
            case '>': g.emitABC(OP_LT, t, r, l); break;
            case LE: g.emitABC(OP_LE, t, l, r); break;
            case GE: g.emitABC(OP_LE, t, r, l); break;
            }
            holes.push_back(g.emitJump(jumpIfTrue ? OP_JMPIF : OP_JMPIFNOT, t));
            return;
        }
        bool swap;
        OpCode fop = fusedFor(eff, swap);
        holes.push_back(swap ? g.emitFused(fop, r, l) : g.emitFused(fop, l, r));
    }

    /* `x < c` / `x > c` (and <=, >= rewritten as < c+1 / > c-1) with a small
       int constant on the right: one LTIJMPIFNOT/GTIJMPIFNOT, no constant load.
       Returns false when the shape does not match. */
    static bool emitRelJumpImm(BBGen &g, RelExprNode *rel, bool jumpIfTrue, vector<int> &holes)
    {
        if (rel->opType != Type::int_type) return false;
        int c;
        if (!smallIntConst(rel->rhs, c)) return false;
        int op = jumpIfTrue ? invRel(rel->op) : rel->op;
        /* fused ops jump when the relation does NOT hold */
        OpCode fop;
        int imm;
        switch (op)
        {
        case '<': fop = OP_LTIJMPIFNOT; imm = c; break;
        case '>': fop = OP_GTIJMPIFNOT; imm = c; break;
        case LE: if (c >= 127) return false; fop = OP_LTIJMPIFNOT; imm = c + 1; break;
        case GE: if (c <= -128) return false; fop = OP_GTIJMPIFNOT; imm = c - 1; break;
        default: return false;
        }
        int l = g.expr(rel->lhs);
        holes.push_back(g.emitFused(fop, l, imm & 0xFF));
        return true;
    }

    static void emitNullTest(BBGen &g, RelExprNode *rel, bool jumpIfTrue, vector<int> &holes)
    {
        /* one side is Null: test object for nil-or-deleted */
        ExprNode *objE = isNullExpr(rel->lhs) ? rel->rhs : rel->lhs;
        bool wantNull = rel->op == '=';             /* condition true when object IS null */
        if (jumpIfTrue) wantNull = !wantNull;       /* we jump on the opposite of "fall through" */
        /* Now: jump when (obj is null) == !wantNull ... work it out directly: */
        bool jumpWhenNull = jumpIfTrue ? (rel->op == '=') : (rel->op != '=');
        int save = g.top();
        int o = g.expr(objE);
        BBTypeInfo *ti = g.typeOf(objE->sem_type);
        int aliveIdx = ti->nfields + HF_ALIVE;
        if (jumpWhenNull)
        {
            holes.push_back(g.emitJumpIfNil(o));
            int t = g.allocTemp();
            g.emitABC(OP_GETFIELD_IDX, t, o, aliveIdx);
            holes.push_back(g.emitJump(OP_JMPIFNOT, t));
        }
        else
        {
            int skip = g.emitJumpIfNil(o);
            int t = g.allocTemp();
            g.emitABC(OP_GETFIELD_IDX, t, o, aliveIdx);
            holes.push_back(g.emitJump(OP_JMPIF, t));
            g.patch(skip);
        }
        g.freeTo(save);
    }

    void BBGen::condFalse(ExprNode *e, vector<int> &holes)
    {
        if (ConstNode *c = e->constNode())
        {
            if (!c->intValue()) holes.push_back(emitJump(OP_JMP, 0));
            return;
        }
        if (RelExprNode *rel = dynamic_cast<RelExprNode *>(e))
        {
            if (rel->opType->structType())
            {
                if (isNullExpr(rel->lhs) || isNullExpr(rel->rhs)) { emitNullTest(*this, rel, false, holes); return; }
            }
            int save = top();
            if (emitRelJumpImm(*this, rel, false, holes)) { freeTo(save); return; }
            int l = expr(rel->lhs);
            int r = expr(rel->rhs);
            emitRelJump(*this, rel->op, rel->opType, l, r, false, holes);
            freeTo(save);
            return;
        }
        if (BinExprNode *bin = dynamic_cast<BinExprNode *>(e))
        {
            if (bin->op == AND && !bin->rhs->impure())
            {
                condFalse(bin->lhs, holes);
                condFalse(bin->rhs, holes);
                return;
            }
            if (bin->op == OR && !bin->rhs->impure())
            {
                vector<int> t;
                condTrue(bin->lhs, t);
                condFalse(bin->rhs, holes);
                patchAll(t);
                return;
            }
        }
        int save = top();
        int r = expr(e);
        int z = allocTemp();
        loadInt(z, 0);
        holes.push_back(emitFused(OP_NEJMPIFNOT, r, z));
        freeTo(save);
    }

    void BBGen::condTrue(ExprNode *e, vector<int> &holes)
    {
        if (ConstNode *c = e->constNode())
        {
            if (c->intValue()) holes.push_back(emitJump(OP_JMP, 0));
            return;
        }
        if (RelExprNode *rel = dynamic_cast<RelExprNode *>(e))
        {
            if (rel->opType->structType())
            {
                if (isNullExpr(rel->lhs) || isNullExpr(rel->rhs)) { emitNullTest(*this, rel, true, holes); return; }
            }
            int save = top();
            if (emitRelJumpImm(*this, rel, true, holes)) { freeTo(save); return; }
            int l = expr(rel->lhs);
            int r = expr(rel->rhs);
            emitRelJump(*this, rel->op, rel->opType, l, r, true, holes);
            freeTo(save);
            return;
        }
        if (BinExprNode *bin = dynamic_cast<BinExprNode *>(e))
        {
            if (bin->op == OR && !bin->rhs->impure())
            {
                condTrue(bin->lhs, holes);
                condTrue(bin->rhs, holes);
                return;
            }
            if (bin->op == AND && !bin->rhs->impure())
            {
                vector<int> t;
                condFalse(bin->lhs, t);
                condTrue(bin->rhs, holes);
                patchAll(t);
                return;
            }
        }
        int save = top();
        int r = expr(e);
        int z = allocTemp();
        loadInt(z, 0);
        holes.push_back(emitFused(OP_EQJMPIFNOT, r, z));
        freeTo(save);
    }

    /* ================= expression nodes ================= */
    int CastNode::emit(BBGen &g, int dest)
    {
        Type *from = expr->sem_type;
        if (from == Type::int_type && sem_type == Type::float_type) return expr->emit(g, dest);
        if (from == Type::float_type && sem_type == Type::int_type) return g.callHelper1(g.rt->g_ftoi, expr, dest);
        if (from == Type::string_type && sem_type == Type::int_type) return g.callHelper1(g.rt->g_stoi, expr, dest);
        if (from == Type::string_type && sem_type == Type::float_type) return g.callHelper1(g.rt->g_stof, expr, dest);
        if (from == Type::float_type && sem_type == Type::string_type) return g.callHelper1(g.rt->g_ftostr, expr, dest);
        if (from->structType() && sem_type == Type::string_type) return g.callHelper1(g.rt->g_objtostr, expr, dest);
        if (from == Type::int_type && sem_type == Type::string_type)
        {
            int save = g.top();
            int r = g.expr(expr);
            int d = dest >= 0 ? dest : save;
            g.emitABC(OP_TOSTRING, d, r, 0);
            return g.finish(d, dest, save);
        }
        return expr->emit(g, dest);
    }

    /* Blitz maths commands that map onto VM opcodes (degrees <-> radians handled inline) */
    struct Intrinsic { const char *name; OpCode op; int mode; }; /* mode: 0 plain, 1 rad->op, 2 op->deg */
    static const Intrinsic intrinsics[] = {
        {"sqr", OP_SQRT, 0}, {"floor", OP_FLOOR, 0}, {"ceil", OP_CEIL, 0}, {"exp", OP_EXP, 0}, {"log", OP_LOG, 0},
        {"sin", OP_SIN, 1}, {"cos", OP_COS, 1}, {"tan", OP_TAN, 1},
        {"asin", OP_ASIN, 2}, {"acos", OP_ACOS, 2}, {"atan", OP_ATAN, 2},
    };

    int CallNode::emit(BBGen &g, int dest)
    {
        int gidx = g.globalOf(sem_decl);
        if (gidx == g.rt->g_end)
        {
            g.emitABC(OP_HALT, 0, 0, 0);
            if (dest >= 0) return dest;
            int t = g.allocTemp();
            return t;
        }
        if (g.rt->isRuntimeDecl(sem_decl) && exprs->size() == 1)
        {
            for (size_t k = 0; k < sizeof(intrinsics) / sizeof(intrinsics[0]); ++k)
            {
                if (ident != intrinsics[k].name) continue;
                int save = g.top();
                int r = g.expr(exprs->exprs[0]);
                int d = dest >= 0 ? dest : save;
                if (intrinsics[k].mode == 1) { g.emitABC(OP_RAD, d, r, 0); g.emitABC(intrinsics[k].op, d, d, 0); }
                else if (intrinsics[k].mode == 2) { g.emitABC(intrinsics[k].op, d, r, 0); g.emitABC(OP_DEG, d, d, 0); }
                else g.emitABC(intrinsics[k].op, d, r, 0);
                return g.finish(d, dest, save);
            }
        }
        int save = g.top();
        int base;
        if (dest >= 0 && dest == save - 1 && dest >= g.f->nlocals) base = dest;
        else base = g.allocTemp();
        int n = exprs->size();
        for (int k = 0; k < n; ++k)
        {
            int r = g.allocTemp();
            g.exprInto(exprs->exprs[k], r);
        }
        g.callGlobal(base, n, gidx, 1);
        if (base == dest)
        {
            g.freeTo(save);
            return dest;
        }
        return g.finish(base, dest, save);
    }

    int VarExprNode::emit(BBGen &g, int dest)
    {
        return var->load(g, dest);
    }

    int IntConstNode::emit(BBGen &g, int dest)
    {
        int d = dest >= 0 ? dest : g.allocTemp();
        g.loadInt(d, value);
        return d;
    }

    int FloatConstNode::emit(BBGen &g, int dest)
    {
        int d = dest >= 0 ? dest : g.allocTemp();
        g.loadFloat(d, value);
        return d;
    }

    int StringConstNode::emit(BBGen &g, int dest)
    {
        int d = dest >= 0 ? dest : g.allocTemp();
        g.loadStr(d, value);
        return d;
    }

    int UniExprNode::emit(BBGen &g, int dest)
    {
        if (op == '+') return expr->emit(g, dest);
        if (op == SGN) return g.callHelper1(g.rt->g_sgn, expr, dest);
        int save = g.top();
        int r = g.expr(expr);
        int d = dest >= 0 ? dest : save;
        g.emitABC(op == '-' ? OP_NEG : OP_ABS, d, r, 0);
        return g.finish(d, dest, save);
    }

    int BinExprNode::emit(BBGen &g, int dest)
    {
        int save = g.top();
        int l = g.expr(lhs);
        int r = g.expr(rhs);
        int d = dest >= 0 ? dest : save;
        switch (op)
        {
        case AND: g.emitABC(OP_BAND, d, l, r); break;
        case OR: g.emitABC(OP_BOR, d, l, r); break;
        case XOR: g.emitABC(OP_BXOR, d, l, r); break;
        case SHL: g.emitABC(OP_SHL, d, l, r); break;
        case SAR: g.emitABC(OP_SHR, d, l, r); break;
        case SHR:
        {
            /* logical shift of the low 32 bits (Blitz ints are 32-bit) */
            int m = g.allocTemp();
            g.emitABx(OP_LOADK, m, g.f->em.add_constant(val_int(0xFFFFFFFFLL)));
            g.emitABC(OP_BAND, m, l, m);
            g.emitABC(OP_SHR, d, m, r);
            break;
        }
        }
        return g.finish(d, dest, save);
    }

    int ArithExprNode::emit(BBGen &g, int dest)
    {
        int save = g.top();
        int imm;
        /* int +/- small constant → ADDI/SUBI (one instruction, no constant load) */
        if (sem_type == Type::int_type && (op == '+' || op == '-') && smallIntConst(rhs, imm))
        {
            int l = g.expr(lhs);
            int d = dest >= 0 ? dest : save;
            g.emitABC(op == '+' ? OP_ADDI : OP_SUBI, d, l, imm & 0xFF);
            return g.finish(d, dest, save);
        }
        /* the same for multiplication, which is commutative, so a constant
           on either side folds into the instruction */
        if (sem_type == Type::int_type && op == '*' && smallIntConst(rhs, imm))
        {
            int l = g.expr(lhs);
            int d = dest >= 0 ? dest : save;
            g.emitABC(OP_MULI, d, l, imm & 0xFF);
            return g.finish(d, dest, save);
        }
        if (sem_type == Type::int_type && (op == '+' || op == '*') && smallIntConst(lhs, imm))
        {
            int r = g.expr(rhs);
            int d = dest >= 0 ? dest : save;
            g.emitABC(op == '+' ? OP_ADDI : OP_MULI, d, r, imm & 0xFF);
            return g.finish(d, dest, save);
        }
        int l = g.expr(lhs);
        int r = g.expr(rhs);
        int d = dest >= 0 ? dest : save;
        if (sem_type == Type::string_type)
        {
            g.emitABC(OP_CONCAT, d, l, r);
        }
        else if (sem_type == Type::int_type)
        {
            switch (op)
            {
            case '+': g.emitABC(OP_ADD, d, l, r); break;
            case '-': g.emitABC(OP_SUB, d, l, r); break;
            case '*': g.emitABC(OP_MUL, d, l, r); break;
            case '/': g.emitABC(OP_IDIV, d, l, r); break;
            case MOD: g.emitABC(OP_MOD, d, l, r); break;
            }
        }
        else
        {
            switch (op)
            {
            case '+': g.emitABC(OP_ADD, d, l, r); break;
            case '-': g.emitABC(OP_SUB, d, l, r); break;
            case '*': g.emitABC(OP_MUL, d, l, r); break;
            case '/': g.emitABC(OP_DIV, d, l, r); break;
            case MOD: g.emitABC(OP_MOD, d, l, r); break;
            case '^': g.emitABC(OP_POW, d, l, r); break;
            }
        }
        return g.finish(d, dest, save);
    }

    int RelExprNode::emit(BBGen &g, int dest)
    {
        int save = g.top();
        vector<int> holes;
        g.condFalse(this, holes);
        int d = dest >= 0 ? dest : g.allocTemp();
        g.loadInt(d, 1);
        int j = g.emitJump(OP_JMP, 0);
        g.patchAll(holes);
        g.loadInt(d, 0);
        g.patch(j);
        if (dest >= 0) g.freeTo(save);
        return d;
    }

    int NewNode::emit(BBGen &g, int dest)
    {
        BBTypeInfo *ti = g.typeOf(sem_type);
        int save = g.top();
        int base = g.allocTemp();
        int a = g.allocTemp();
        g.emitABx(OP_GETGLOBAL, a, ti->gDef);
        g.callGlobal(base, 1, g.rt->g_new);
        return g.finish(base, dest, save);
    }

    int FirstNode::emit(BBGen &g, int dest)
    {
        BBTypeInfo *ti = g.typeOf(sem_type);
        int d = dest >= 0 ? dest : g.allocTemp();
        g.emitABx(OP_GETGLOBAL, d, ti->gFirst);
        return d;
    }

    int LastNode::emit(BBGen &g, int dest)
    {
        BBTypeInfo *ti = g.typeOf(sem_type);
        int d = dest >= 0 ? dest : g.allocTemp();
        g.emitABx(OP_GETGLOBAL, d, ti->gLast);
        return d;
    }

    int AfterNode::emit(BBGen &g, int dest)
    {
        return g.callHelper1(g.rt->g_after, expr, dest);
    }

    int BeforeNode::emit(BBGen &g, int dest)
    {
        return g.callHelper1(g.rt->g_before, expr, dest);
    }

    int NullNode::emit(BBGen &g, int dest)
    {
        int d = dest >= 0 ? dest : g.allocTemp();
        g.loadNil(d);
        return d;
    }

    int ObjectCastNode::emit(BBGen &g, int dest)
    {
        int save = g.top();
        int base = g.allocTemp();
        int a = g.allocTemp();
        g.exprInto(expr, a);
        int b = g.allocTemp();
        g.emitABx(OP_GETGLOBAL, b, g.typeOf(sem_type)->gDef);
        g.callGlobal(base, 2, g.rt->g_object);
        return g.finish(base, dest, save);
    }

    int ObjectHandleNode::emit(BBGen &g, int dest)
    {
        return g.callHelper1(g.rt->g_handle, expr, dest);
    }

    /* ================= variables ================= */
    int DeclVarNode::directReg(BBGen &g)
    {
        (void)g;
        if (sem_decl->kind & (DECL_LOCAL | DECL_PARAM)) return sem_decl->offset;
        return -1;
    }

    int DeclVarNode::load(BBGen &g, int dest)
    {
        if (sem_decl->kind & (DECL_LOCAL | DECL_PARAM))
        {
            int r = sem_decl->offset;
            if (dest >= 0 && dest != r) { g.move(dest, r); return dest; }
            return r;
        }
        int d = dest >= 0 ? dest : g.allocTemp();
        g.emitABx(OP_GETGLOBAL, d, g.globalOf(sem_decl));
        return d;
    }

    void DeclVarNode::storeReg(BBGen &g, int reg)
    {
        if (sem_decl->kind & (DECL_LOCAL | DECL_PARAM))
        {
            g.move(sem_decl->offset, reg);
            return;
        }
        g.emitABx(OP_SETGLOBAL, reg, g.globalOf(sem_decl));
    }

    void DeclVarNode::store(BBGen &g, ExprNode *expr)
    {
        if (sem_decl->kind & (DECL_LOCAL | DECL_PARAM))
        {
            g.exprInto(expr, sem_decl->offset);
            return;
        }
        int save = g.top();
        int r = g.expr(expr);
        g.emitABx(OP_SETGLOBAL, r, g.globalOf(sem_decl));
        g.freeTo(save);
    }

    int ArrayVarNode::emitIndex(BBGen &g, int &arrReg)
    {
        ArrayInfo &ai = g.arrayOf(sem_decl);
        arrReg = g.allocTemp();
        g.emitABx(OP_GETGLOBAL, arrReg, ai.gArr);
        int idx = g.allocTemp();
        g.exprInto(exprs->exprs[0], idx);
        if (g.debug) g.boundsCheck(idx, ai.gSizes[0]);
        for (int k = 1; k < exprs->size(); ++k)
        {
            int s = g.allocTemp();
            g.emitABx(OP_GETGLOBAL, s, ai.gSizes[k]);
            g.emitABC(OP_MUL, idx, idx, s);
            g.exprInto(exprs->exprs[k], s);
            if (g.debug) g.boundsCheck(s, ai.gSizes[k]);
            g.emitABC(OP_ADD, idx, idx, s);
            g.freeTo(s);
        }
        return idx;
    }

    /* --debug: 0 <= R[idx] < globals[gSize], else "Array index out of bounds" */
    void BBGen::boundsCheck(int idx, int gSize)
    {
        int save = top();
        int s = allocTemp();
        emitABx(OP_GETGLOBAL, s, gSize);
        int bad1 = emitFused(OP_LTJMPIFNOT, idx, s);
        int bad2 = emitFused(OP_GTIJMPIFNOT, idx, (-1) & 0xFF);
        int ok = emitJump(OP_JMP, 0);
        patch(bad1);
        patch(bad2);
        int base = allocTemp();
        int a = allocTemp();
        loadStr(a, "Array index out of bounds");
        callGlobal(base, 1, rt->g_rterror, 0);
        patch(ok);
        freeTo(save);
    }

    int ArrayVarNode::load(BBGen &g, int dest)
    {
        int save = g.top();
        int arr;
        int idx = emitIndex(g, arr);
        int d = dest >= 0 ? dest : save;
        g.emitABC(OP_GETINDEX, d, arr, idx);
        return g.finish(d, dest, save);
    }

    void ArrayVarNode::storeReg(BBGen &g, int reg)
    {
        int save = g.top();
        int arr;
        int idx = emitIndex(g, arr);
        g.emitABC(OP_SETINDEX, arr, idx, reg);
        g.freeTo(save);
    }

    void ArrayVarNode::store(BBGen &g, ExprNode *expr)
    {
        int save = g.top();
        int arr;
        int idx = emitIndex(g, arr);
        int v = g.expr(expr);
        g.emitABC(OP_SETINDEX, arr, idx, v);
        g.freeTo(save);
    }

    int FieldVarNode::load(BBGen &g, int dest)
    {
        int save = g.top();
        int o = g.expr(expr);
        int d = dest >= 0 ? dest : save;
        g.emitABC(OP_GETFIELD_IDX, d, o, sem_field->offset);
        return g.finish(d, dest, save);
    }

    void FieldVarNode::storeReg(BBGen &g, int reg)
    {
        int save = g.top();
        int o = g.expr(expr);
        g.emitABC(OP_SETFIELD_IDX, o, sem_field->offset, reg);
        g.freeTo(save);
    }

    void FieldVarNode::store(BBGen &g, ExprNode *e)
    {
        int save = g.top();
        int o = g.expr(expr);
        int v = g.expr(e);
        g.emitABC(OP_SETFIELD_IDX, o, sem_field->offset, v);
        g.freeTo(save);
    }

    int VectorVarNode::emitIndex(BBGen &g, int &vecReg)
    {
        vecReg = g.expr(expr);
        int idx = g.allocTemp();
        g.exprInto(exprs->exprs[0], idx);
        for (int k = 1; k < exprs->size(); ++k)
        {
            int s = g.allocTemp();
            g.loadInt(s, vec_type->sizes[k]);
            g.emitABC(OP_MUL, idx, idx, s);
            g.exprInto(exprs->exprs[k], s);
            g.emitABC(OP_ADD, idx, idx, s);
            g.freeTo(s);
        }
        return idx;
    }

    int VectorVarNode::load(BBGen &g, int dest)
    {
        int save = g.top();
        int vec;
        int idx = emitIndex(g, vec);
        int d = dest >= 0 ? dest : save;
        g.emitABC(OP_GETINDEX, d, vec, idx);
        return g.finish(d, dest, save);
    }

    void VectorVarNode::storeReg(BBGen &g, int reg)
    {
        int save = g.top();
        int vec;
        int idx = emitIndex(g, vec);
        g.emitABC(OP_SETINDEX, vec, idx, reg);
        g.freeTo(save);
    }

    void VectorVarNode::store(BBGen &g, ExprNode *e)
    {
        int save = g.top();
        int vec;
        int idx = emitIndex(g, vec);
        int v = g.expr(e);
        g.emitABC(OP_SETINDEX, vec, idx, v);
        g.freeTo(save);
    }

    /* ================= declarations ================= */
    void DeclSeqNode::emit(BBGen &g)
    {
        for (size_t k = 0; k < decls.size(); ++k)
        {
            try { decls[k]->emit(g); }
            catch (Ex &x)
            {
                if (x.pos < 0) x.pos = decls[k]->pos;
                if (!x.file.size()) x.file = decls[k]->file;
                throw;
            }
        }
    }

    void VarDeclNode::emit(BBGen &g)
    {
        if (expr) sem_var->store(g, expr);
    }

    void VectorDeclNode::emit(BBGen &g)
    {
        /* allocate the Blitz array: __bbVec(kind, size) */
        int save = g.top();
        int base = g.allocTemp();
        int a = g.allocTemp();
        Type *et = sem_type->elementType;
        int kind = et == Type::float_type ? KIND_FLOAT : et == Type::string_type ? KIND_STR : et->structType() ? KIND_OBJ : KIND_INT;
        g.loadInt(a, kind);
        int b = g.allocTemp();
        g.loadInt(b, sem_type->totalSize());
        g.callGlobal(base, 2, g.rt->g_vec);
        if (sem_decl->kind & (DECL_LOCAL | DECL_PARAM)) g.move(sem_decl->offset, base);
        else g.emitABx(OP_SETGLOBAL, base, g.globalOf(sem_decl));
        g.freeTo(save);
    }

    /* ================= statements ================= */
    void StmtSeqNode::emit(BBGen &g)
    {
        for (size_t k = 0; k < stmts.size(); ++k)
        {
            StmtNode *stmt = stmts[k];
            if (stmt->pos >= 0) g.line = (stmt->pos >> 16) + 1;
            try
            {
                stmt->emit(g);
                g.freeTo(g.f->nlocals);
            }
            catch (Ex &x)
            {
                if (x.pos < 0) x.pos = stmt->pos;
                if (!x.file.size()) x.file = file;
                throw;
            }
        }
    }

    void IncludeNode::emit(BBGen &g) { stmts->emit(g); }

    void DeclStmtNode::emit(BBGen &g) { decl->emit(g); }

    void DimNode::emit(BBGen &g)
    {
        ArrayInfo &ai = g.arrayOf(sem_array);
        int save = g.top();
        int n = exprs->size();
        for (int k = 0; k < n; ++k)
        {
            int t = g.allocTemp();
            g.exprInto(exprs->exprs[k], t);
            g.emitABC(OP_ADDI, t, t, 1);
            g.emitABx(OP_SETGLOBAL, t, ai.gSizes[k]);
            g.freeTo(t);
        }
        int base = g.allocTemp();
        int a = g.allocTemp();
        g.loadInt(a, ai.kind);
        for (int k = 0; k < n; ++k)
        {
            int s = g.allocTemp();
            g.emitABx(OP_GETGLOBAL, s, ai.gSizes[k]);
        }
        g.callGlobal(base, 1 + n, g.rt->g_dim);
        g.emitABx(OP_SETGLOBAL, base, ai.gArr);
        g.freeTo(save);
    }

    void AssNode::emit(BBGen &g) { var->store(g, expr); }

    void ExprStmtNode::emit(BBGen &g)
    {
        int save = g.top();
        g.expr(expr);
        g.freeTo(save);
    }

    void LabelNode::emit(BBGen &g) { g.defineLabel(ident); }

    void GotoNode::emit(BBGen &g) { g.jumpToLabel(ident); }

    void GosubNode::emit(BBGen &g)
    {
        int id = (int)g.f->gosubSites.size() + 1;
        string ret = "__gosub_ret_" + bb_itoa(id);
        g.f->gosubSites.push_back(std::make_pair(id, ret));
        int save = g.top();
        int t = g.allocTemp();
        g.loadInt(t, id);
        g.emitABC(OP_APPEND, g.gosubReg(), t, 0);
        g.freeTo(save);
        g.jumpToLabel(ident);
        g.defineLabel(ret);
    }

    void IfNode::emit(BBGen &g)
    {
        if (ConstNode *c = expr->constNode())
        {
            if (c->intValue()) stmts->emit(g);
            else if (elseOpt) elseOpt->emit(g);
            return;
        }
        vector<int> holes;
        g.condFalse(expr, holes);
        g.freeTo(g.f->nlocals);
        stmts->emit(g);
        if (elseOpt)
        {
            int j = g.emitJump(OP_JMP, 0);
            g.patchAll(holes);
            elseOpt->emit(g);
            g.patch(j);
        }
        else g.patchAll(holes);
    }

    void ExitNode::emit(BBGen &g) { g.jumpToLabel(sem_brk); }

    void WhileNode::emit(BBGen &g)
    {
        int loop = g.here();
        vector<int> holes;
        if (ConstNode *c = expr->constNode())
        {
            if (!c->intValue()) return;
        }
        else
        {
            g.condFalse(expr, holes);
            g.freeTo(g.f->nlocals);
        }
        stmts->emit(g);
        g.line = (wendPos >> 16) + 1;
        int j = g.emitJump(OP_JMP, 0);
        g.patchTo(j, loop);
        g.patchAll(holes);
        g.defineLabel(sem_brk);
    }

    void ForNode::emit(BBGen &g)
    {
        Type *ty = var->sem_type;
        double stepv = stepExpr->constNode()->floatValue();
        bool up = stepv > 0;

        /* initial assignment */
        var->store(g, fromExpr);
        g.freeTo(g.f->nlocals);

        int cond = g.here();
        /* loop test: continue while var <= to (or var >= to for negative step). `to` is re-evaluated every iteration */
        int save = g.top();
        int v = var->load(g, -1);
        int t = g.expr(toExpr);
        int exitHole = up ? g.emitFused(OP_LEJMPIFNOT, v, t) : g.emitFused(OP_LEJMPIFNOT, t, v);
        g.freeTo(save);

        stmts->emit(g);

        /* step */
        g.line = (nextPos >> 16) + 1;
        save = g.top();
        int direct = var->directReg(g);
        if (direct >= 0 && ty == Type::int_type && stepv >= -128 && stepv <= 127)
        {
            g.emitABC(OP_ADDI, direct, direct, (int)(long long)stepv & 0xFF);
        }
        else
        {
            int cur = var->load(g, -1);
            int s = g.allocTemp();
            if (ty == Type::int_type) g.loadInt(s, stepExpr->constNode()->intValue());
            else g.loadFloat(s, stepv);
            int r = g.allocTemp();
            g.emitABC(OP_ADD, r, cur, s);
            var->storeReg(g, r);
        }
        g.freeTo(save);
        int j = g.emitJump(OP_JMP, 0);
        g.patchTo(j, cond);
        g.patch(exitHole);
        g.defineLabel(sem_brk);
    }

    void ForEachNode::emit(BBGen &g)
    {
        BBTypeInfo *ti = g.typeOf(var->sem_type);
        int nextIdx = ti->nfields + HF_NEXT;
        int aliveIdx = ti->nfields + HF_ALIVE;

        /* var = First */
        int save = g.top();
        int t = g.allocTemp();
        g.emitABx(OP_GETGLOBAL, t, ti->gFirst);
        var->storeReg(g, t);
        g.freeTo(save);

        /* check */
        int check = g.here();
        int cur = var->load(g, -1);
        int exitHole = g.emitJumpIfNil(cur);
        g.freeTo(save);

        stmts->emit(g);

        /* next: skip deleted successors */
        g.line = (nextPos >> 16) + 1;
        cur = var->load(g, -1);
        int nxt = g.allocTemp();
        g.emitABC(OP_GETFIELD_IDX, nxt, cur, nextIdx);
        int skip = g.here();
        int toStore = g.emitJumpIfNil(nxt);
        int alive = g.allocTemp();
        g.emitABC(OP_GETFIELD_IDX, alive, nxt, aliveIdx);
        int toStore2 = g.emitJump(OP_JMPIF, alive);
        g.emitABC(OP_GETFIELD_IDX, nxt, nxt, nextIdx);
        int back = g.emitJump(OP_JMP, 0);
        g.patchTo(back, skip);
        g.patch(toStore);
        g.patch(toStore2);
        var->storeReg(g, nxt);
        g.freeTo(save);
        int j = g.emitJump(OP_JMP, 0);
        g.patchTo(j, check);
        g.patch(exitHole);
        g.defineLabel(sem_brk);
    }

    void ReturnNode::emit(BBGen &g)
    {
        if (sem_level <= 0)
        {
            /* Return from a Gosub: jump to the dispatcher */
            g.f->gosubReturnHoles.push_back(g.emitJump(OP_JMP, 0));
            return;
        }
        int save = g.top();
        int r = g.expr(expr);
        g.emitABC(OP_RETURN, r, 1, 0);
        g.freeTo(save);
    }

    void DeleteNode::emit(BBGen &g)
    {
        int save = g.top();
        g.callHelper1(g.rt->g_delete, expr, -1);
        g.freeTo(save);
    }

    void DeleteEachNode::emit(BBGen &g)
    {
        int save = g.top();
        int base = g.allocTemp();
        int a = g.allocTemp();
        g.emitABx(OP_GETGLOBAL, a, g.typeOf(sem_struct)->gDef);
        g.callGlobal(base, 1, g.rt->g_deleteEach);
        g.freeTo(save);
    }

    void InsertNode::emit(BBGen &g)
    {
        int save = g.top();
        int base = g.allocTemp();
        int a = g.allocTemp();
        g.exprInto(expr1, a);
        int b = g.allocTemp();
        g.exprInto(expr2, b);
        g.callGlobal(base, 2, before ? g.rt->g_insBefore : g.rt->g_insAfter);
        g.freeTo(save);
    }

    void SelectNode::emit(BBGen &g)
    {
        sem_temp->store(g, expr);
        g.freeTo(g.f->nlocals);
        int tmp = sem_temp->directReg(g);

        vector<int> caseJumps;   /* JMP holes to each case body */
        vector<int> endJumps;
        for (size_t k = 0; k < cases.size(); ++k)
        {
            CaseNode *c = cases[k];
            vector<int> hits;
            for (int j = 0; j < c->exprs->size(); ++j)
            {
                int save = g.top();
                int e = g.expr(c->exprs->exprs[j]);
                int miss = g.emitFused(OP_EQJMPIFNOT, tmp, e);
                hits.push_back(g.emitJump(OP_JMP, 0));
                g.patch(miss);
                g.freeTo(save);
            }
            /* remember where this case's hits must land */
            caseJumps.push_back((int)hits.size());
            for (size_t h = 0; h < hits.size(); ++h) caseJumps.push_back(hits[h]);
        }
        if (defStmts) defStmts->emit(g);
        endJumps.push_back(g.emitJump(OP_JMP, 0));
        size_t p = 0;
        for (size_t k = 0; k < cases.size(); ++k)
        {
            int n = caseJumps[p++];
            for (int h = 0; h < n; ++h) g.patch(caseJumps[p++]);
            cases[k]->stmts->emit(g);
            endJumps.push_back(g.emitJump(OP_JMP, 0));
        }
        g.patchAll(endJumps);
    }

    void RepeatNode::emit(BBGen &g)
    {
        int loop = g.here();
        stmts->emit(g);
        g.line = (untilPos >> 16) + 1;
        if (ConstNode *c = expr ? expr->constNode() : 0)
        {
            if (!c->intValue()) g.patchTo(g.emitJump(OP_JMP, 0), loop);
        }
        else if (expr)
        {
            vector<int> holes;
            g.condFalse(expr, holes);
            for (size_t k = 0; k < holes.size(); ++k) g.patchTo(holes[k], loop);
            g.freeTo(g.f->nlocals);
        }
        else g.patchTo(g.emitJump(OP_JMP, 0), loop);
        g.defineLabel(sem_brk);
    }

    void ReadNode::emit(BBGen &g)
    {
        int save = g.top();
        int base = g.allocTemp();
        int gidx = var->sem_type == Type::int_type ? g.rt->g_readInt : var->sem_type == Type::float_type ? g.rt->g_readFloat : g.rt->g_readStr;
        g.callGlobal(base, 0, gidx);
        var->storeReg(g, base);
        g.freeTo(save);
    }

    void RestoreNode::emit(BBGen &g)
    {
        int save = g.top();
        int t = g.allocTemp();
        g.loadInt(t, sem_label ? sem_label->data_sz : 0);
        g.emitABx(OP_SETGLOBAL, t, g.rt->g_dataptr);
        g.freeTo(save);
    }

    /* ================= functions / program ================= */
    static int kindOf(Type *t)
    {
        if (t == Type::float_type) return KIND_FLOAT;
        if (t == Type::string_type) return KIND_STR;
        if (t->structType()) return KIND_OBJ;
        if (t->vectorType()) return KIND_VEC;
        return KIND_INT;
    }

    void BBGen::assignLocals(Environ *env)
    {
        int n = 0;
        for (int k = 0; k < env->decls->size(); ++k)
        {
            Decl *d = env->decls->decls[k];
            if (d->kind & DECL_PARAM) d->offset = n++;
        }
        for (int k = 0; k < env->decls->size(); ++k)
        {
            Decl *d = env->decls->decls[k];
            if (d->kind & DECL_LOCAL) d->offset = n++;
        }
        if (n > 200) Node::ex("Too many local variables in function");
        f->nlocals = n;
        f->top = n;
        f->maxreg = n;
    }

    void BBGen::initLocals(Environ *env, bool isMain)
    {
        (void)isMain;
        for (int k = 0; k < env->decls->size(); ++k)
        {
            Decl *d = env->decls->decls[k];
            if (!(d->kind & DECL_LOCAL)) continue;
            loadDefault(d->offset, d->type);
        }
    }

    void BBGen::prepareGlobals(ProgNode *prog)
    {
        Environ *env = prog->sem_env;

        /* Types */
        for (size_t k = 0; k < prog->structs->decls.size(); ++k)
        {
            StructDeclNode *sd = (StructDeclNode *)prog->structs->decls[k];
            StructType *st = sd->sem_type;
            vector<int> kinds, vsz, vk;
            for (int i = 0; i < st->fields->size(); ++i)
            {
                Type *ft = st->fields->decls[i]->type;
                kinds.push_back(kindOf(ft));
                if (VectorType *v = ft->vectorType())
                {
                    vsz.push_back(v->totalSize());
                    vk.push_back(kindOf(v->elementType));
                }
                else
                {
                    vsz.push_back(0);
                    vk.push_back(0);
                }
            }
            st->rt_index = rt->registerType(st->ident, kinds, vsz, vk);
        }

        /* Globals and arrays */
        for (int k = 0; k < env->decls->size(); ++k)
        {
            Decl *d = env->decls->decls[k];
            if (d->kind & DECL_GLOBAL)
            {
                if (d->type->constType()) continue;
                Value init;
                Type *t = d->type;
                if (t == Type::float_type) init = val_float(0.0);
                else if (t == Type::string_type) init = val_obj((Obj *)vm->make_string(""));
                else if (t->structType() || t->vectorType()) init = val_nil();
                else init = val_int(0);
                d->offset = vm->def_global(("_v" + d->name).c_str(), init);
            }
            else if (d->kind & DECL_ARRAY)
            {
                ArrayType *a = d->type->arrayType();
                ArrayInfo ai;
                ai.kind = kindOf(a->elementType);
                ai.gArr = vm->def_global(("_a" + d->name).c_str(), val_nil());
                for (int i = 0; i < a->dims; ++i)
                    ai.gSizes.push_back(vm->def_global(("_a" + d->name + "_s" + bb_itoa(i)).c_str(), val_int(0)));
                arrays[d] = ai;
                d->offset = ai.gArr;
            }
        }

        /* Functions */
        for (int k = 0; k < env->funcDecls->size(); ++k)
        {
            Decl *d = env->funcDecls->decls[k];
            d->offset = vm->def_global(("_f" + d->name).c_str(), val_nil());
        }

        /* Data */
        ObjArray *data = new_array(&vm->get_gc());
        vm->set_global(rt->g_data, val_obj((Obj *)data));
        for (size_t k = 0; k < prog->datas->decls.size(); ++k)
        {
            DataDeclNode *dd = (DataDeclNode *)prog->datas->decls[k];
            ConstNode *c = dd->expr->constNode();
            Value v;
            if (dd->expr->sem_type == Type::int_type) v = val_int(c->intValue());
            else if (dd->expr->sem_type == Type::float_type) v = val_float(c->floatValue());
            else
            {
                string s = c->stringValue();
                v = val_obj((Obj *)vm->make_string(s.c_str(), (int)s.size()));
            }
            array_push(&vm->get_gc(), data, v);
        }
        vm->set_global(rt->g_dataptr, val_int(0));
    }

    ObjFunc *BBGen::compileFunc(FuncDeclNode *fn)
    {
        Func *saved = f;
        Func state(&vm->get_gc());
        f = &state;
        f->env = fn->sem_env;
        f->name = fn->ident;
        int nparams = fn->sem_type->params->size();
        f->em.begin(fn->ident.c_str(), nparams, filename.c_str());
        assignLocals(fn->sem_env);
        line = (fn->pos >> 16) + 1;
        initLocals(fn->sem_env, false);
        try
        {
            fn->stmts->emit(*this);
            resolveLabels();
        }
        catch (Ex &x)
        {
            f = saved;
            if (x.pos < 0) x.pos = fn->pos;
            if (!x.file.size()) x.file = fn->file;
            throw;
        }
        /* safety net: functions always end with a Return, but keep the VM happy */
        emitABC(OP_RETURNNIL, 0, 1, 0);
        ObjFunc *obj = f->em.end(f->maxreg + 1);
        obj->arity = nparams;
        f = saved;
        return obj;
    }

    ObjFunc *BBGen::compile(ProgNode *prog, const string &fname)
    {
        filename = fname;
        GC *gc = &vm->get_gc();
        gc_pause(gc);
        ObjFunc *result = 0;
        try
        {
            prepareGlobals(prog);

            /* compile functions */
            funcObjs.clear();
            funcDecls.clear();
            for (size_t k = 0; k < prog->funcs->decls.size(); ++k)
            {
                FuncDeclNode *fn = (FuncDeclNode *)prog->funcs->decls[k];
                funcObjs.push_back(compileFunc(fn));
                funcDecls.push_back(fn->sem_decl);
            }

            /* main program */
            Func state(gc);
            f = &state;
            f->env = prog->sem_env;
            f->name = "<main>";
            f->em.begin("<main program>", 0, filename.c_str());
            assignLocals(prog->sem_env);
            line = 1;

            /* gosub stack */
            f->gosubReg = allocTemp();
            f->nlocals = f->top; /* keep it below the temp area */
            emitABC(OP_NEWARRAY, f->gosubReg, 0, 0);

            /* install functions in their globals */
            for (size_t k = 0; k < funcObjs.size(); ++k)
            {
                int t = allocTemp();
                int ki = f->em.add_constant(val_obj((Obj *)funcObjs[k]));
                emitABx(OP_LOADK, t, ki);
                emitABx(OP_SETGLOBAL, t, funcDecls[k]->offset);
                freeTo(t);
            }

            initLocals(prog->sem_env, true);

            prog->stmts->emit(*this);
            emitABC(OP_HALT, 0, 0, 0);

            /* gosub return dispatcher */
            if (f->gosubReturnHoles.size())
            {
                patchAll(f->gosubReturnHoles);
                int base = allocTemp();
                int a = allocTemp();
                move(a, f->gosubReg);
                callGlobal(base, 1, rt->g_gosubPop);
                int c = allocTemp();
                for (size_t k = 0; k < f->gosubSites.size(); ++k)
                {
                    loadInt(c, f->gosubSites[k].first);
                    int miss = emitFused(OP_EQJMPIFNOT, base, c);
                    jumpToLabel(f->gosubSites[k].second);
                    patch(miss);
                }
                emitABC(OP_HALT, 0, 0, 0);
                freeTo(f->nlocals);
            }

            /* undefined labels */
            for (size_t k = 0; k < prog->sem_env->labels.size(); ++k)
                if (prog->sem_env->labels[k]->def < 0)
                    Node::ex("Undefined label '" + prog->sem_env->labels[k]->name + "'", prog->sem_env->labels[k]->ref, prog->stmts->file);
            resolveLabels();

            result = f->em.end(f->maxreg + 1);
            f = 0;
        }
        catch (...)
        {
            f = 0;
            gc_resume(gc);
            throw;
        }
        gc_resume(gc);
        return result;
    }
}

namespace bb
{
    /* functions are compiled by BBGen::compileFunc; nothing to emit inline */
    void FuncDeclNode::emit(BBGen &g) { (void)g; }
}
