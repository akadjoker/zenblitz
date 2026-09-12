/*
** bb_codegen.h — Zen bytecode generator for the Blitz AST.
*/
#ifndef BB_CODEGEN_H
#define BB_CODEGEN_H

#include "bb_nodes.h"
#include "bb_runtime.h"
#include "emitter.h"

namespace bb
{
    struct ArrayInfo
    {
        int gArr;              /* global holding the zen array */
        vector<int> gSizes;    /* globals holding each dimension size */
        int kind;
    };

    class BBGen
    {
    public:
        BBGen(zen::VM *vm, BBRuntime *rt, bool debug);
        ~BBGen();

        /* Compile a semant'ed program; returns the main function. */
        zen::ObjFunc *compile(ProgNode *prog, const string &filename);

        /* ---- state for the function being emitted ---- */
        struct Func
        {
            zen::Emitter em;
            Environ *env;
            int nlocals;
            int top;
            int maxreg;
            string name;
            map<string, int> labels;                 /* label -> code offset */
            vector<std::pair<int, string> > pending;  /* (hole, label) */
            /* gosub (main only) */
            int gosubReg;
            vector<std::pair<int, string> > gosubSites; /* (id, return label) */
            vector<int> gosubReturnHoles;
            Func(zen::GC *gc) : em(gc), env(0), nlocals(0), top(0), maxreg(0), gosubReg(-1) {}
        };

        Func *f;
        int line;
        zen::VM *vm;
        BBRuntime *rt;
        bool debug;
        string filename;

        /* registers */
        int allocTemp();
        void freeTo(int t) { f->top = t; }
        int top() const { return f->top; }
        int finish(int reg, int dest, int save);

        /* raw emission */
        int emitABC(zen::OpCode op, int a, int b, int c);
        int emitABx(zen::OpCode op, int a, int bx);
        int here() const { return f->em.current_offset(); }
        int emitJump(zen::OpCode op, int a);          /* 1-word jump, returns hole */
        int emitFused(zen::OpCode op, int b, int c);  /* 2-word compare+jump, returns hole */
        int emitJumpIfNil(int a);                     /* 2-word, returns hole */
        void patch(int hole) { patchTo(hole, here()); }
        void patchTo(int hole, int target);
        void patchAll(vector<int> &holes) { for (size_t k = 0; k < holes.size(); ++k) patch(holes[k]); holes.clear(); }

        /* constants */
        void loadInt(int reg, long long v);
        void loadFloat(int reg, double v);
        void loadStr(int reg, const string &s);
        void loadNil(int reg);
        void loadDefault(int reg, Type *t);
        void move(int dst, int src);

        /* calls */
        void callGlobal(int base, int nargs, int gidx, int nresults = 1);
        int callHelper1(int gidx, ExprNode *arg, int dest);
        int callHelper1r(int gidx, int argReg, int dest);

        /* expressions */
        int expr(ExprNode *e, int dest = -1) { return e->emit(*this, dest); }
        int exprInto(ExprNode *e, int dest);
        void condFalse(ExprNode *e, vector<int> &holes);
        void condTrue(ExprNode *e, vector<int> &holes);

        /* labels / control flow */
        void defineLabel(const string &l);
        void jumpToLabel(const string &l);
        void resolveLabels();

        /* globals / arrays / types */
        int globalOf(Decl *d);
        ArrayInfo &arrayOf(Decl *d);
        BBTypeInfo *typeOf(Type *t);
        int gosubReg();
        void boundsCheck(int idx, int gSize);

        /* var helpers */
        int fieldCount(StructType *s) { return s->fields->size(); }

    private:
        map<Decl *, ArrayInfo> arrays;
        void prepareGlobals(ProgNode *prog);
        void assignLocals(Environ *env);
        zen::ObjFunc *compileFunc(FuncDeclNode *fn);
        void initLocals(Environ *env, bool isMain);
        vector<zen::ObjFunc *> funcObjs;
        vector<Decl *> funcDecls;
    };
}

#endif
