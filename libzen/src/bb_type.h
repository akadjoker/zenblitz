/*
** bb_type.h — Blitz type system, declarations, labels and environments.
** (port of Blitz3D compiler/type.h, decl.h, label.h, environ.h)
*/
#ifndef BB_TYPE_H
#define BB_TYPE_H

#include "bb_std.h"

namespace bb
{
    struct Type;
    struct FuncType;
    struct ArrayType;
    struct StructType;
    struct ConstType;
    struct VectorType;

    /* ---------------- Decl ---------------- */
    enum
    {
        DECL_FUNC = 1, DECL_ARRAY = 2, DECL_STRUCT = 4,                 /* NOT vars */
        DECL_GLOBAL = 8, DECL_LOCAL = 16, DECL_PARAM = 32, DECL_FIELD = 64 /* ARE vars */
    };

    struct Decl
    {
        string name;
        Type *type;
        int kind;
        int offset;         /* local: register index; global/func: zen global index; field: field index */
        ConstType *defType; /* default value (params) */
        Decl(const string &s, Type *t, int k, ConstType *d = 0) : name(s), type(t), kind(k), offset(-1), defType(d) {}
    };

    struct DeclSeq
    {
        vector<Decl *> decls;
        DeclSeq() {}
        ~DeclSeq();
        Decl *findDecl(const string &s);
        Decl *insertDecl(const string &s, Type *t, int kind, ConstType *d = 0);
        int size() { return (int)decls.size(); }
    };

    /* ---------------- Type ---------------- */
    struct Type
    {
        virtual ~Type() {}

        virtual bool intType() { return false; }
        virtual bool floatType() { return false; }
        virtual bool stringType() { return false; }

        virtual FuncType *funcType() { return 0; }
        virtual ArrayType *arrayType() { return 0; }
        virtual StructType *structType() { return 0; }
        virtual ConstType *constType() { return 0; }
        virtual VectorType *vectorType() { return 0; }

        virtual bool canCastTo(Type *t) { return this == t; }

        static Type *void_type, *int_type, *float_type, *string_type, *null_type;
    };

    struct FuncType : public Type
    {
        Type *returnType;
        DeclSeq *params;
        bool userlib, cfunc;
        FuncType(Type *t, DeclSeq *p, bool ulib, bool cfn) : returnType(t), params(p), userlib(ulib), cfunc(cfn) {}
        ~FuncType() { delete params; }
        FuncType *funcType() { return this; }
    };

    struct ArrayType : public Type
    {
        Type *elementType;
        int dims;
        ArrayType(Type *t, int n) : elementType(t), dims(n) {}
        ArrayType *arrayType() { return this; }
    };

    struct StructType : public Type
    {
        string ident;
        DeclSeq *fields;
        int rt_index; /* runtime type index (assigned by codegen) */
        StructType(const string &i) : ident(i), fields(0), rt_index(-1) {}
        StructType(const string &i, DeclSeq *f) : ident(i), fields(f), rt_index(-1) {}
        ~StructType() { delete fields; }
        StructType *structType() { return this; }
        virtual bool canCastTo(Type *t);
    };

    struct ConstType : public Type
    {
        Type *valueType;
        long long intValue;
        double floatValue;
        string stringValue;
        ConstType(long long n) : valueType(Type::int_type), intValue(n), floatValue(0) {}
        ConstType(double n) : valueType(Type::float_type), intValue(0), floatValue(n) {}
        ConstType(const string &n) : valueType(Type::string_type), intValue(0), floatValue(0), stringValue(n) {}
        ConstType *constType() { return this; }
    };

    struct VectorType : public Type
    {
        string label;
        Type *elementType;
        vector<int> sizes;
        VectorType(const string &l, Type *t, const vector<int> &szs) : label(l), elementType(t), sizes(szs) {}
        VectorType *vectorType() { return this; }
        virtual bool canCastTo(Type *t);
        int totalSize() const { int n = 1; for (size_t k = 0; k < sizes.size(); ++k) n *= sizes[k]; return n; }
    };

    /* ---------------- Label ---------------- */
    struct Label
    {
        string name;
        int def, ref; /* pos of defn and goto/restore src */
        int data_sz;  /* number of Data items before this label */
        Label(const string &n, int d, int r, int sz) : name(n), def(d), ref(r), data_sz(sz) {}
    };

    /* ---------------- Environ ---------------- */
    class Environ
    {
    public:
        int level;
        DeclSeq *decls;
        DeclSeq *funcDecls;
        DeclSeq *typeDecls;

        vector<Type *> types;

        vector<Label *> labels;
        Environ *globals;
        Type *returnType;
        string funcLabel, breakLabel;
        list<Environ *> children;

        Environ(const string &f, Type *r, int l, Environ *gs);
        ~Environ();

        Decl *findDecl(const string &s);
        Decl *findFunc(const string &s);
        Type *findType(const string &s);
        Label *findLabel(const string &s);
        Label *insertLabel(const string &s, int def, int src, int sz);

        string setBreak(const string &s);
    };
}

#endif
