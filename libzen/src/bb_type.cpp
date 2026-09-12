#include "bb_type.h"

namespace bb
{
    /* ---------------- DeclSeq ---------------- */
    DeclSeq::~DeclSeq()
    {
        for (; decls.size(); decls.pop_back()) delete decls.back();
    }

    Decl *DeclSeq::findDecl(const string &s)
    {
        for (size_t k = 0; k < decls.size(); ++k)
            if (decls[k]->name == s) return decls[k];
        return 0;
    }

    Decl *DeclSeq::insertDecl(const string &s, Type *t, int kind, ConstType *d)
    {
        if (findDecl(s)) return 0;
        decls.push_back(new Decl(s, t, kind, d));
        return decls.back();
    }

    /* ---------------- builtin types ---------------- */
    static struct v_type : public Type
    {
        bool canCastTo(Type *t) { return t == Type::void_type; }
    } v;

    static struct i_type : public Type
    {
        bool intType() { return true; }
        bool canCastTo(Type *t) { return t == Type::int_type || t == Type::float_type || t == Type::string_type; }
    } i;

    static struct f_type : public Type
    {
        bool floatType() { return true; }
        bool canCastTo(Type *t) { return t == Type::int_type || t == Type::float_type || t == Type::string_type; }
    } f;

    static struct s_type : public Type
    {
        bool stringType() { return true; }
        bool canCastTo(Type *t) { return t == Type::int_type || t == Type::float_type || t == Type::string_type; }
    } s;

    bool StructType::canCastTo(Type *t)
    {
        return t == this || t == Type::null_type || (this == Type::null_type && t->structType());
    }

    bool VectorType::canCastTo(Type *t)
    {
        if (this == t) return true;
        if (VectorType *v = t->vectorType())
        {
            if (elementType != v->elementType) return false;
            if (sizes.size() != v->sizes.size()) return false;
            for (size_t k = 0; k < sizes.size(); ++k)
                if (sizes[k] != v->sizes[k]) return false;
            return true;
        }
        return false;
    }

    static StructType n("Null");

    Type *Type::void_type = &v;
    Type *Type::int_type = &i;
    Type *Type::float_type = &f;
    Type *Type::string_type = &s;
    Type *Type::null_type = &n;

    /* ---------------- Environ ---------------- */
    Environ::Environ(const string &f, Type *r, int l, Environ *gs)
        : level(l), globals(gs), returnType(r), funcLabel(f)
    {
        decls = new DeclSeq();
        typeDecls = new DeclSeq();
        funcDecls = new DeclSeq();
        if (globals) globals->children.push_back(this);
    }

    Environ::~Environ()
    {
        if (globals) globals->children.remove(this);
        while (children.size()) delete children.back();
        for (; labels.size(); labels.pop_back()) delete labels.back();
        delete decls;
        delete funcDecls;
        delete typeDecls;
        for (size_t k = 0; k < types.size(); ++k) delete types[k];
    }

    Decl *Environ::findDecl(const string &s)
    {
        for (Environ *e = this; e; e = e->globals)
        {
            if (Decl *d = e->decls->findDecl(s))
            {
                if (d->kind & (DECL_LOCAL | DECL_PARAM))
                {
                    if (e == this) return d;
                }
                else return d;
            }
        }
        return 0;
    }

    Decl *Environ::findFunc(const string &s)
    {
        for (Environ *e = this; e; e = e->globals)
            if (Decl *d = e->funcDecls->findDecl(s)) return d;
        return 0;
    }

    Type *Environ::findType(const string &s)
    {
        if (s == "%") return Type::int_type;
        if (s == "#") return Type::float_type;
        if (s == "$") return Type::string_type;
        for (Environ *e = this; e; e = e->globals)
            if (Decl *d = e->typeDecls->findDecl(s)) return d->type->structType();
        return 0;
    }

    Label *Environ::findLabel(const string &s)
    {
        for (size_t k = 0; k < labels.size(); ++k)
            if (labels[k]->name == s) return labels[k];
        return 0;
    }

    Label *Environ::insertLabel(const string &s, int def, int src, int sz)
    {
        Label *l = new Label(s, def, src, sz);
        labels.push_back(l);
        return l;
    }

    string Environ::setBreak(const string &s)
    {
        string t = breakLabel;
        breakLabel = s;
        return t;
    }
}
