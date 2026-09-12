/*
** bb_nodes.h — Abstract syntax tree for Blitz Basic.
** (port of Blitz3D compiler/node.h, exprnode.h, varnode.h, stmtnode.h,
**  declnode.h, prognode.h). Every node keeps the original `semant` pass
**  (type checking, constant folding, implicit declarations) and gains an
**  `emit` method that generates Zen bytecode through BBGen.
*/
#ifndef BB_NODES_H
#define BB_NODES_H

#include "bb_std.h"
#include "bb_toker.h"
#include "bb_type.h"

namespace bb
{
    class BBGen;
    struct VarNode;
    struct ConstNode;
    struct ExprNode;
    struct StmtSeqNode;

    /* ================= Node ================= */
    struct Node
    {
        virtual ~Node() {}

        static void ex();
        static void ex(const string &e);
        static void ex(const string &e, int pos);
        static void ex(const string &e, int pos, const string &f);

        static string genLabel();
        static ConstNode *constValue(Type *ty);
        static Type *tagType(const string &s, Environ *e);
    };

    /* ================= Expressions ================= */
    struct ExprNode : public Node
    {
        Type *sem_type;
        ExprNode() : sem_type(0) {}
        ExprNode(Type *t) : sem_type(t) {}

        ExprNode *castTo(Type *ty, Environ *e);

        virtual ExprNode *semant(Environ *e) = 0;
        /* Emit code; result placed in `dest` if dest>=0, else in a register
           chosen by the generator (may be a variable's own register). Returns
           the register holding the result. */
        virtual int emit(BBGen &g, int dest) = 0;
        virtual ConstNode *constNode() { return 0; }
        /* true if evaluating this expression may have side effects (calls, New...) */
        virtual bool impure() { return false; }
    };

    struct ExprSeqNode : public Node
    {
        vector<ExprNode *> exprs;
        ~ExprSeqNode() { for (; exprs.size(); exprs.pop_back()) delete exprs.back(); }
        void push_back(ExprNode *e) { exprs.push_back(e); }
        int size() { return (int)exprs.size(); }
        void semant(Environ *e);
        void castTo(DeclSeq *ds, Environ *e, bool userlib);
        void castTo(Type *t, Environ *e);
    };

    /* ================= Variables ================= */
    struct VarNode : public Node
    {
        Type *sem_type;
        VarNode() : sem_type(0) {}

        virtual void semant(Environ *e) = 0;
        /* load value into dest (or into a chosen register if dest<0); returns register */
        virtual int load(BBGen &g, int dest) = 0;
        /* store expression into the variable */
        virtual void store(BBGen &g, ExprNode *expr) = 0;
        /* store the value already held in register `reg` */
        virtual void storeReg(BBGen &g, int reg) = 0;
        /* if the variable lives in a plain register, return it (else -1) */
        virtual int directReg(BBGen &g) { (void)g; return -1; }
    };

    struct DeclVarNode : public VarNode
    {
        Decl *sem_decl;
        DeclVarNode(Decl *d = 0) : sem_decl(d) { if (d) sem_type = d->type; }
        void semant(Environ *e);
        int load(BBGen &g, int dest);
        void store(BBGen &g, ExprNode *expr);
        void storeReg(BBGen &g, int reg);
        int directReg(BBGen &g);
    };

    struct IdentVarNode : public DeclVarNode
    {
        string ident, tag;
        IdentVarNode(const string &i, const string &t) : ident(i), tag(t) {}
        void semant(Environ *e);
    };

    struct ArrayVarNode : public VarNode
    {
        string ident, tag;
        ExprSeqNode *exprs;
        Decl *sem_decl;
        ArrayVarNode(const string &i, const string &t, ExprSeqNode *e) : ident(i), tag(t), exprs(e), sem_decl(0) {}
        ~ArrayVarNode() { delete exprs; }
        void semant(Environ *e);
        int load(BBGen &g, int dest);
        void store(BBGen &g, ExprNode *expr);
        void storeReg(BBGen &g, int reg);
        int emitIndex(BBGen &g, int &arrReg); /* returns reg holding flat index */
    };

    struct FieldVarNode : public VarNode
    {
        ExprNode *expr;
        string ident, tag;
        Decl *sem_field;
        FieldVarNode(ExprNode *e, const string &i, const string &t) : expr(e), ident(i), tag(t), sem_field(0) {}
        ~FieldVarNode() { delete expr; }
        void semant(Environ *e);
        int load(BBGen &g, int dest);
        void store(BBGen &g, ExprNode *expr);
        void storeReg(BBGen &g, int reg);
    };

    struct VectorVarNode : public VarNode
    {
        ExprNode *expr;
        ExprSeqNode *exprs;
        VectorType *vec_type;
        VectorVarNode(ExprNode *e, ExprSeqNode *es) : expr(e), exprs(es), vec_type(0) {}
        ~VectorVarNode() { delete expr; delete exprs; }
        void semant(Environ *e);
        int load(BBGen &g, int dest);
        void store(BBGen &g, ExprNode *expr);
        void storeReg(BBGen &g, int reg);
        int emitIndex(BBGen &g, int &vecReg);
    };

    /* ================= Expression nodes ================= */
    struct CastNode : public ExprNode
    {
        ExprNode *expr;
        Type *type;
        CastNode(ExprNode *ex, Type *ty) : expr(ex), type(ty) {}
        ~CastNode() { delete expr; }
        ExprNode *semant(Environ *e);
        int emit(BBGen &g, int dest);
        bool impure() { return expr->impure(); }
    };

    struct CallNode : public ExprNode
    {
        string ident, tag;
        ExprSeqNode *exprs;
        Decl *sem_decl;
        CallNode(const string &i, const string &t, ExprSeqNode *e) : ident(i), tag(t), exprs(e), sem_decl(0) {}
        ~CallNode() { delete exprs; }
        ExprNode *semant(Environ *e);
        int emit(BBGen &g, int dest);
        bool impure() { return true; }
    };

    struct VarExprNode : public ExprNode
    {
        VarNode *var;
        VarExprNode(VarNode *v) : var(v) {}
        ~VarExprNode() { delete var; }
        ExprNode *semant(Environ *e);
        int emit(BBGen &g, int dest);
    };

    struct ConstNode : public ExprNode
    {
        ExprNode *semant(Environ *e) { (void)e; return this; }
        ConstNode *constNode() { return this; }
        virtual long long intValue() = 0;
        virtual double floatValue() = 0;
        virtual string stringValue() = 0;
    };

    struct IntConstNode : public ConstNode
    {
        long long value;
        IntConstNode(long long n);
        int emit(BBGen &g, int dest);
        long long intValue();
        double floatValue();
        string stringValue();
    };

    struct FloatConstNode : public ConstNode
    {
        double value;
        FloatConstNode(double f);
        int emit(BBGen &g, int dest);
        long long intValue();
        double floatValue();
        string stringValue();
    };

    struct StringConstNode : public ConstNode
    {
        string value;
        StringConstNode(const string &s);
        int emit(BBGen &g, int dest);
        long long intValue();
        double floatValue();
        string stringValue();
    };

    struct UniExprNode : public ExprNode
    {
        int op;
        ExprNode *expr;
        UniExprNode(int op, ExprNode *expr) : op(op), expr(expr) {}
        ~UniExprNode() { delete expr; }
        ExprNode *semant(Environ *e);
        int emit(BBGen &g, int dest);
        bool impure() { return expr->impure(); }
    };

    /* and, or, xor, shl, shr, sar */
    struct BinExprNode : public ExprNode
    {
        int op;
        ExprNode *lhs, *rhs;
        BinExprNode(int op, ExprNode *lhs, ExprNode *rhs) : op(op), lhs(lhs), rhs(rhs) {}
        ~BinExprNode() { delete lhs; delete rhs; }
        ExprNode *semant(Environ *e);
        int emit(BBGen &g, int dest);
        bool impure() { return lhs->impure() || rhs->impure(); }
    };

    /* *,/,Mod,+,-,^ */
    struct ArithExprNode : public ExprNode
    {
        int op;
        ExprNode *lhs, *rhs;
        ArithExprNode(int op, ExprNode *lhs, ExprNode *rhs) : op(op), lhs(lhs), rhs(rhs) {}
        ~ArithExprNode() { delete lhs; delete rhs; }
        ExprNode *semant(Environ *e);
        int emit(BBGen &g, int dest);
        bool impure() { return lhs->impure() || rhs->impure(); }
    };

    /* <,=,>,<=,<>,>= */
    struct RelExprNode : public ExprNode
    {
        int op;
        ExprNode *lhs, *rhs;
        Type *opType;
        RelExprNode(int op, ExprNode *lhs, ExprNode *rhs) : op(op), lhs(lhs), rhs(rhs), opType(0) {}
        ~RelExprNode() { delete lhs; delete rhs; }
        ExprNode *semant(Environ *e);
        int emit(BBGen &g, int dest);
        bool impure() { return lhs->impure() || rhs->impure(); }
    };

    struct NewNode : public ExprNode
    {
        string ident;
        NewNode(const string &i) : ident(i) {}
        ExprNode *semant(Environ *e);
        int emit(BBGen &g, int dest);
        bool impure() { return true; }
    };

    struct FirstNode : public ExprNode
    {
        string ident;
        FirstNode(const string &i) : ident(i) {}
        ExprNode *semant(Environ *e);
        int emit(BBGen &g, int dest);
    };

    struct LastNode : public ExprNode
    {
        string ident;
        LastNode(const string &i) : ident(i) {}
        ExprNode *semant(Environ *e);
        int emit(BBGen &g, int dest);
    };

    struct AfterNode : public ExprNode
    {
        ExprNode *expr;
        AfterNode(ExprNode *e) : expr(e) {}
        ~AfterNode() { delete expr; }
        ExprNode *semant(Environ *e);
        int emit(BBGen &g, int dest);
        bool impure() { return expr->impure(); }
    };

    struct BeforeNode : public ExprNode
    {
        ExprNode *expr;
        BeforeNode(ExprNode *e) : expr(e) {}
        ~BeforeNode() { delete expr; }
        ExprNode *semant(Environ *e);
        int emit(BBGen &g, int dest);
        bool impure() { return expr->impure(); }
    };

    struct NullNode : public ExprNode
    {
        ExprNode *semant(Environ *e);
        int emit(BBGen &g, int dest);
    };

    struct ObjectCastNode : public ExprNode
    {
        ExprNode *expr;
        string type_ident;
        ObjectCastNode(ExprNode *e, const string &t) : expr(e), type_ident(t) {}
        ~ObjectCastNode() { delete expr; }
        ExprNode *semant(Environ *e);
        int emit(BBGen &g, int dest);
        bool impure() { return expr->impure(); }
    };

    struct ObjectHandleNode : public ExprNode
    {
        ExprNode *expr;
        ObjectHandleNode(ExprNode *e) : expr(e) {}
        ~ObjectHandleNode() { delete expr; }
        ExprNode *semant(Environ *e);
        int emit(BBGen &g, int dest);
        bool impure() { return true; }
    };

    /* ================= Declarations ================= */
    struct DeclNode : public Node
    {
        int pos;
        string file;
        DeclNode() : pos(-1) {}
        virtual void proto(DeclSeq *d, Environ *e) { (void)d; (void)e; }
        virtual void semant(Environ *e) { (void)e; }
        virtual void emit(BBGen &g) { (void)g; }
    };

    struct DeclSeqNode : public Node
    {
        vector<DeclNode *> decls;
        DeclSeqNode() {}
        ~DeclSeqNode() { for (; decls.size(); decls.pop_back()) delete decls.back(); }
        void proto(DeclSeq *d, Environ *e);
        void semant(Environ *e);
        void emit(BBGen &g);
        void push_back(DeclNode *d) { decls.push_back(d); }
        int size() { return (int)decls.size(); }
    };

    struct VarDeclNode : public DeclNode
    {
        string ident, tag;
        int kind;
        bool constant;
        ExprNode *expr;
        DeclVarNode *sem_var;
        VarDeclNode(const string &i, const string &t, int k, bool c, ExprNode *e) : ident(i), tag(t), kind(k), constant(c), expr(e), sem_var(0) {}
        ~VarDeclNode() { delete expr; delete sem_var; }
        void proto(DeclSeq *d, Environ *e);
        void semant(Environ *e);
        void emit(BBGen &g);
    };

    struct FuncDeclNode : public DeclNode
    {
        string ident, tag;
        DeclSeqNode *params;
        StmtSeqNode *stmts;
        FuncType *sem_type;
        Environ *sem_env;
        Decl *sem_decl;
        FuncDeclNode(const string &i, const string &t, DeclSeqNode *p, StmtSeqNode *ss) : ident(i), tag(t), params(p), stmts(ss), sem_type(0), sem_env(0), sem_decl(0) {}
        ~FuncDeclNode();
        void proto(DeclSeq *d, Environ *e);
        void semant(Environ *e);
        void emit(BBGen &g);
    };

    struct StructDeclNode : public DeclNode
    {
        string ident;
        DeclSeqNode *fields;
        StructType *sem_type;
        StructDeclNode(const string &i, DeclSeqNode *f) : ident(i), fields(f), sem_type(0) {}
        ~StructDeclNode() { delete fields; }
        void proto(DeclSeq *d, Environ *e);
        void semant(Environ *e);
    };

    struct DataDeclNode : public DeclNode
    {
        ExprNode *expr;
        DataDeclNode(ExprNode *e) : expr(e) {}
        ~DataDeclNode() { delete expr; }
        void proto(DeclSeq *d, Environ *e);
    };

    struct VectorDeclNode : public DeclNode
    {
        string ident, tag;
        ExprSeqNode *exprs;
        int kind;
        VectorType *sem_type;
        Decl *sem_decl;
        VectorDeclNode(const string &i, const string &t, ExprSeqNode *e, int k) : ident(i), tag(t), exprs(e), kind(k), sem_type(0), sem_decl(0) {}
        ~VectorDeclNode() { delete exprs; }
        void proto(DeclSeq *d, Environ *e);
        void emit(BBGen &g);
    };

    /* ================= Statements ================= */
    struct StmtNode : public Node
    {
        int pos; /* (row<<16)|col in source */
        StmtNode() : pos(-1) {}
        virtual void semant(Environ *e) { (void)e; }
        virtual void emit(BBGen &g) { (void)g; }
    };

    struct StmtSeqNode : public Node
    {
        string file;
        vector<StmtNode *> stmts;
        StmtSeqNode(const string &f) : file(f) {}
        ~StmtSeqNode() { for (; stmts.size(); stmts.pop_back()) delete stmts.back(); }
        void semant(Environ *e);
        void emit(BBGen &g);
        void push_back(StmtNode *s) { stmts.push_back(s); }
        int size() { return (int)stmts.size(); }
    };

    struct IncludeNode : public StmtNode
    {
        string file;
        StmtSeqNode *stmts;
        IncludeNode(const string &t, StmtSeqNode *ss) : file(t), stmts(ss) {}
        ~IncludeNode() { delete stmts; }
        void semant(Environ *e);
        void emit(BBGen &g);
    };

    struct DeclStmtNode : public StmtNode
    {
        DeclNode *decl;
        DeclStmtNode(DeclNode *d) : decl(d) { pos = d->pos; }
        ~DeclStmtNode() { delete decl; }
        void semant(Environ *e);
        void emit(BBGen &g);
    };

    struct DimNode : public StmtNode
    {
        string ident, tag;
        ExprSeqNode *exprs;
        ArrayType *sem_type;
        Decl *sem_decl;
        Decl *sem_array; /* the array decl (new or existing) */
        DimNode(const string &i, const string &t, ExprSeqNode *e) : ident(i), tag(t), exprs(e), sem_type(0), sem_decl(0), sem_array(0) {}
        ~DimNode() { delete exprs; }
        void semant(Environ *e);
        void emit(BBGen &g);
    };

    struct AssNode : public StmtNode
    {
        VarNode *var;
        ExprNode *expr;
        AssNode(VarNode *var, ExprNode *expr) : var(var), expr(expr) {}
        ~AssNode() { delete var; delete expr; }
        void semant(Environ *e);
        void emit(BBGen &g);
    };

    struct ExprStmtNode : public StmtNode
    {
        ExprNode *expr;
        ExprStmtNode(ExprNode *e) : expr(e) {}
        ~ExprStmtNode() { delete expr; }
        void semant(Environ *e);
        void emit(BBGen &g);
    };

    struct LabelNode : public StmtNode
    {
        string ident;
        int data_sz;
        LabelNode(const string &s, int sz) : ident(s), data_sz(sz) {}
        void semant(Environ *e);
        void emit(BBGen &g);
    };

    struct GotoNode : public StmtNode
    {
        string ident;
        GotoNode(const string &s) : ident(s) {}
        void semant(Environ *e);
        void emit(BBGen &g);
    };

    struct GosubNode : public StmtNode
    {
        string ident;
        GosubNode(const string &s) : ident(s) {}
        void semant(Environ *e);
        void emit(BBGen &g);
    };

    struct IfNode : public StmtNode
    {
        ExprNode *expr;
        StmtSeqNode *stmts, *elseOpt;
        IfNode(ExprNode *e, StmtSeqNode *s, StmtSeqNode *o) : expr(e), stmts(s), elseOpt(o) {}
        ~IfNode() { delete expr; delete stmts; delete elseOpt; }
        void semant(Environ *e);
        void emit(BBGen &g);
    };

    struct ExitNode : public StmtNode
    {
        string sem_brk;
        void semant(Environ *e);
        void emit(BBGen &g);
    };

    struct WhileNode : public StmtNode
    {
        int wendPos;
        ExprNode *expr;
        StmtSeqNode *stmts;
        string sem_brk;
        WhileNode(ExprNode *e, StmtSeqNode *s, int wp) : wendPos(wp), expr(e), stmts(s) {}
        ~WhileNode() { delete expr; delete stmts; }
        void semant(Environ *e);
        void emit(BBGen &g);
    };

    struct ForNode : public StmtNode
    {
        int nextPos;
        VarNode *var;
        ExprNode *fromExpr, *toExpr, *stepExpr;
        StmtSeqNode *stmts;
        string sem_brk;
        ForNode(VarNode *var, ExprNode *from, ExprNode *to, ExprNode *step, StmtSeqNode *ss, int np);
        ~ForNode();
        void semant(Environ *e);
        void emit(BBGen &g);
    };

    struct ForEachNode : public StmtNode
    {
        int nextPos;
        VarNode *var;
        string typeIdent;
        StmtSeqNode *stmts;
        string sem_brk;
        ForEachNode(VarNode *v, const string &t, StmtSeqNode *ss, int np) : nextPos(np), var(v), typeIdent(t), stmts(ss) {}
        ~ForEachNode() { delete var; delete stmts; }
        void semant(Environ *e);
        void emit(BBGen &g);
    };

    struct ReturnNode : public StmtNode
    {
        ExprNode *expr;
        string returnLabel;
        int sem_level;
        ReturnNode(ExprNode *e) : expr(e), sem_level(0) {}
        ~ReturnNode() { delete expr; }
        void semant(Environ *e);
        void emit(BBGen &g);
    };

    struct DeleteNode : public StmtNode
    {
        ExprNode *expr;
        DeleteNode(ExprNode *e) : expr(e) {}
        ~DeleteNode() { delete expr; }
        void semant(Environ *e);
        void emit(BBGen &g);
    };

    struct DeleteEachNode : public StmtNode
    {
        string typeIdent;
        StructType *sem_struct;
        DeleteEachNode(const string &t) : typeIdent(t), sem_struct(0) {}
        void semant(Environ *e);
        void emit(BBGen &g);
    };

    struct InsertNode : public StmtNode
    {
        ExprNode *expr1, *expr2;
        bool before;
        InsertNode(ExprNode *e1, ExprNode *e2, bool b) : expr1(e1), expr2(e2), before(b) {}
        ~InsertNode() { delete expr1; delete expr2; }
        void semant(Environ *e);
        void emit(BBGen &g);
    };

    struct CaseNode : public Node
    {
        ExprSeqNode *exprs;
        StmtSeqNode *stmts;
        CaseNode(ExprSeqNode *e, StmtSeqNode *s) : exprs(e), stmts(s) {}
        ~CaseNode() { delete exprs; delete stmts; }
    };

    struct SelectNode : public StmtNode
    {
        ExprNode *expr;
        StmtSeqNode *defStmts;
        vector<CaseNode *> cases;
        VarNode *sem_temp;
        SelectNode(ExprNode *e) : expr(e), defStmts(0), sem_temp(0) {}
        ~SelectNode();
        void push_back(CaseNode *c) { cases.push_back(c); }
        void semant(Environ *e);
        void emit(BBGen &g);
    };

    struct RepeatNode : public StmtNode
    {
        int untilPos;
        StmtSeqNode *stmts;
        ExprNode *expr;
        string sem_brk;
        RepeatNode(StmtSeqNode *s, ExprNode *e, int up) : untilPos(up), stmts(s), expr(e) {}
        ~RepeatNode() { delete stmts; delete expr; }
        void semant(Environ *e);
        void emit(BBGen &g);
    };

    struct ReadNode : public StmtNode
    {
        VarNode *var;
        ReadNode(VarNode *v) : var(v) {}
        ~ReadNode() { delete var; }
        void semant(Environ *e);
        void emit(BBGen &g);
    };

    struct RestoreNode : public StmtNode
    {
        string ident;
        Label *sem_label;
        RestoreNode(const string &i) : ident(i), sem_label(0) {}
        void semant(Environ *e);
        void emit(BBGen &g);
    };

    /* ================= Program ================= */
    struct ProgNode : public Node
    {
        DeclSeqNode *consts, *structs, *funcs, *datas;
        StmtSeqNode *stmts;
        Environ *sem_env;
        ProgNode(DeclSeqNode *c, DeclSeqNode *s, DeclSeqNode *f, DeclSeqNode *d, StmtSeqNode *ss)
            : consts(c), structs(s), funcs(f), datas(d), stmts(ss), sem_env(0) {}
        ~ProgNode();
        Environ *semant(Environ *e);
    };
}

#endif
