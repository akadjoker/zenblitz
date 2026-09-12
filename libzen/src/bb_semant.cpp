/*
** bb_semant.cpp — semantic analysis (type checking, constant folding,
** implicit declarations). Port of the `semant`/`proto` halves of the
** Blitz3D compiler node sources.
*/
#include "bb_nodes.h"

namespace bb
{
    /* ================= Node helpers ================= */
    void Node::ex() { ex("INTERNAL COMPILER ERROR"); }
    void Node::ex(const string &e) { throw Ex(e, -1, ""); }
    void Node::ex(const string &e, int pos) { throw Ex(e, pos, ""); }
    void Node::ex(const string &e, int pos, const string &f) { throw Ex(e, pos, f); }

    string Node::genLabel()
    {
        static int cnt;
        return "_" + bb_itoa(++cnt & 0x7fffffff);
    }

    ConstNode *Node::constValue(Type *ty)
    {
        ConstType *c = ty->constType();
        if (!c) return 0;
        ty = c->valueType;
        if (ty == Type::int_type) return new IntConstNode(c->intValue);
        if (ty == Type::float_type) return new FloatConstNode(c->floatValue);
        return new StringConstNode(c->stringValue);
    }

    Type *Node::tagType(const string &tag, Environ *e)
    {
        Type *t;
        if (tag.size())
        {
            t = e->findType(tag);
            if (!t) ex("Type \"" + tag + "\" not found");
        }
        else t = 0;
        return t;
    }

    /* ================= Expressions ================= */
    ExprNode *ExprNode::castTo(Type *ty, Environ *e)
    {
        if (!sem_type->canCastTo(ty)) ex("Illegal type conversion");
        ExprNode *cast = new CastNode(this, ty);
        return cast->semant(e);
    }

    ExprNode *CastNode::semant(Environ *e)
    {
        if (!expr->sem_type) expr = expr->semant(e);

        if (ConstNode *c = expr->constNode())
        {
            ExprNode *r;
            if (type == Type::int_type) r = new IntConstNode(c->intValue());
            else if (type == Type::float_type) r = new FloatConstNode(c->floatValue());
            else r = new StringConstNode(c->stringValue());
            delete this;
            return r;
        }
        sem_type = type;
        return this;
    }

    void ExprSeqNode::semant(Environ *e)
    {
        for (size_t k = 0; k < exprs.size(); ++k)
            if (exprs[k]) exprs[k] = exprs[k]->semant(e);
    }

    void ExprSeqNode::castTo(DeclSeq *decls, Environ *e, bool cfunc)
    {
        if ((int)exprs.size() > decls->size()) ex("Too many parameters");
        for (int k = 0; k < decls->size(); ++k)
        {
            Decl *d = decls->decls[k];
            if (k < (int)exprs.size() && exprs[k])
            {
                if (cfunc && d->type->structType())
                {
                    if (exprs[k]->sem_type->structType()) {}
                    else if (exprs[k]->sem_type->intType()) exprs[k]->sem_type = Type::void_type;
                    else ex("Illegal type conversion");
                    continue;
                }
                exprs[k] = exprs[k]->castTo(d->type, e);
            }
            else
            {
                if (!d->defType) ex("Not enough parameters");
                ExprNode *expr = constValue(d->defType);
                if (k < (int)exprs.size()) exprs[k] = expr;
                else exprs.push_back(expr);
            }
        }
    }

    void ExprSeqNode::castTo(Type *t, Environ *e)
    {
        for (size_t k = 0; k < exprs.size(); ++k) exprs[k] = exprs[k]->castTo(t, e);
    }

    ExprNode *CallNode::semant(Environ *e)
    {
        Type *t = e->findType(tag);
        sem_decl = e->findFunc(ident);
        if (!sem_decl || !(sem_decl->kind & DECL_FUNC)) ex("Function '" + ident + "' not found");
        FuncType *f = sem_decl->type->funcType();
        if (t && f->returnType != t) ex("incorrect function return type");
        exprs->semant(e);
        exprs->castTo(f->params, e, f->cfunc);
        sem_type = f->returnType;
        return this;
    }

    ExprNode *VarExprNode::semant(Environ *e)
    {
        var->semant(e);
        sem_type = var->sem_type;
        ConstType *c = sem_type->constType();
        if (!c) return this;
        ExprNode *expr = constValue(c);
        delete this;
        return expr;
    }

    IntConstNode::IntConstNode(long long n) : value(n) { sem_type = Type::int_type; }
    long long IntConstNode::intValue() { return value; }
    double IntConstNode::floatValue() { return (double)value; }
    string IntConstNode::stringValue() { return bb_itoa(value); }

    FloatConstNode::FloatConstNode(double f) : value(f) { sem_type = Type::float_type; }
    long long FloatConstNode::intValue() { return bb_round(value); }
    double FloatConstNode::floatValue() { return value; }
    string FloatConstNode::stringValue() { return bb_ftoa(value); }

    StringConstNode::StringConstNode(const string &s) : value(s) { sem_type = Type::string_type; }
    long long StringConstNode::intValue() { return bb_atoi(value); }
    double StringConstNode::floatValue() { return bb_atof(value); }
    string StringConstNode::stringValue() { return value; }

    ExprNode *UniExprNode::semant(Environ *e)
    {
        expr = expr->semant(e);
        sem_type = expr->sem_type;
        if (sem_type != Type::int_type && sem_type != Type::float_type) ex("Illegal operator for type");
        if (ConstNode *c = expr->constNode())
        {
            ExprNode *r = 0;
            if (sem_type == Type::int_type)
            {
                long long v = c->intValue();
                switch (op)
                {
                case '+': r = new IntConstNode(+v); break;
                case '-': r = new IntConstNode(-v); break;
                case ABS: r = new IntConstNode(v >= 0 ? v : -v); break;
                case SGN: r = new IntConstNode(v > 0 ? 1 : (v < 0 ? -1 : 0)); break;
                }
            }
            else
            {
                double v = c->floatValue();
                switch (op)
                {
                case '+': r = new FloatConstNode(+v); break;
                case '-': r = new FloatConstNode(-v); break;
                case ABS: r = new FloatConstNode(v >= 0 ? v : -v); break;
                case SGN: r = new FloatConstNode(v > 0 ? 1 : (v < 0 ? -1 : 0)); break;
                }
            }
            delete this;
            return r;
        }
        return this;
    }

    ExprNode *BinExprNode::semant(Environ *e)
    {
        lhs = lhs->semant(e);
        lhs = lhs->castTo(Type::int_type, e);
        rhs = rhs->semant(e);
        rhs = rhs->castTo(Type::int_type, e);
        ConstNode *lc = lhs->constNode(), *rc = rhs->constNode();
        if (lc && rc)
        {
            ExprNode *expr = 0;
            long long l = lc->intValue(), r = rc->intValue();
            switch (op)
            {
            case AND: expr = new IntConstNode(l & r); break;
            case OR: expr = new IntConstNode(l | r); break;
            case XOR: expr = new IntConstNode(l ^ r); break;
            case SHL: expr = new IntConstNode((long long)(int)((unsigned)l << (r & 31))); break;
            case SHR: expr = new IntConstNode((long long)(int)((unsigned)l >> (r & 31))); break;
            case SAR: expr = new IntConstNode((long long)((int)l >> (r & 31))); break;
            }
            delete this;
            return expr;
        }
        sem_type = Type::int_type;
        return this;
    }

    ExprNode *ArithExprNode::semant(Environ *e)
    {
        lhs = lhs->semant(e);
        rhs = rhs->semant(e);
        if (lhs->sem_type->structType() || rhs->sem_type->structType())
            ex("Arithmetic operator cannot be applied to custom type objects");
        if (lhs->sem_type == Type::string_type || rhs->sem_type == Type::string_type)
        {
            if (op != '+') ex("Operator cannot be applied to strings");
            sem_type = Type::string_type;
        }
        else if (op == '^' || lhs->sem_type == Type::float_type || rhs->sem_type == Type::float_type)
        {
            sem_type = Type::float_type;
        }
        else
        {
            sem_type = Type::int_type;
        }
        lhs = lhs->castTo(sem_type, e);
        rhs = rhs->castTo(sem_type, e);
        ConstNode *lc = lhs->constNode(), *rc = rhs->constNode();
        if (rc && (op == '/' || op == MOD))
        {
            if ((sem_type == Type::int_type && !rc->intValue()) || (sem_type == Type::float_type && !rc->floatValue()))
                ex("Division by zero");
        }
        if (lc && rc)
        {
            ExprNode *expr = 0;
            if (sem_type == Type::string_type)
            {
                expr = new StringConstNode(lc->stringValue() + rc->stringValue());
            }
            else if (sem_type == Type::int_type)
            {
                long long l = lc->intValue(), r = rc->intValue();
                switch (op)
                {
                case '+': expr = new IntConstNode(l + r); break;
                case '-': expr = new IntConstNode(l - r); break;
                case '*': expr = new IntConstNode(l * r); break;
                case '/': expr = new IntConstNode(l / r); break;
                case MOD: expr = new IntConstNode(l % r); break;
                }
            }
            else
            {
                double l = lc->floatValue(), r = rc->floatValue();
                switch (op)
                {
                case '+': expr = new FloatConstNode(l + r); break;
                case '-': expr = new FloatConstNode(l - r); break;
                case '*': expr = new FloatConstNode(l * r); break;
                case '/': expr = new FloatConstNode(l / r); break;
                case MOD: expr = new FloatConstNode(fmod(l, r)); break;
                case '^': expr = new FloatConstNode(pow(l, r)); break;
                }
            }
            delete this;
            return expr;
        }
        return this;
    }

    ExprNode *RelExprNode::semant(Environ *e)
    {
        lhs = lhs->semant(e);
        rhs = rhs->semant(e);
        if (lhs->sem_type->structType() || rhs->sem_type->structType())
        {
            if (op != '=' && op != NE) ex("Illegal operator for custom type objects");
            opType = lhs->sem_type != Type::null_type ? lhs->sem_type : rhs->sem_type;
        }
        else if (lhs->sem_type == Type::string_type || rhs->sem_type == Type::string_type)
            opType = Type::string_type;
        else if (lhs->sem_type == Type::float_type || rhs->sem_type == Type::float_type)
            opType = Type::float_type;
        else
            opType = Type::int_type;
        sem_type = Type::int_type;
        lhs = lhs->castTo(opType, e);
        rhs = rhs->castTo(opType, e);
        ConstNode *lc = lhs->constNode(), *rc = rhs->constNode();
        if (lc && rc)
        {
            ExprNode *expr = 0;
            if (opType == Type::string_type)
            {
                string l = lc->stringValue(), r = rc->stringValue();
                switch (op)
                {
                case '<': expr = new IntConstNode(l < r); break;
                case '=': expr = new IntConstNode(l == r); break;
                case '>': expr = new IntConstNode(l > r); break;
                case LE: expr = new IntConstNode(l <= r); break;
                case NE: expr = new IntConstNode(l != r); break;
                case GE: expr = new IntConstNode(l >= r); break;
                }
            }
            else if (opType == Type::float_type)
            {
                double l = lc->floatValue(), r = rc->floatValue();
                switch (op)
                {
                case '<': expr = new IntConstNode(l < r); break;
                case '=': expr = new IntConstNode(l == r); break;
                case '>': expr = new IntConstNode(l > r); break;
                case LE: expr = new IntConstNode(l <= r); break;
                case NE: expr = new IntConstNode(l != r); break;
                case GE: expr = new IntConstNode(l >= r); break;
                }
            }
            else
            {
                long long l = lc->intValue(), r = rc->intValue();
                switch (op)
                {
                case '<': expr = new IntConstNode(l < r); break;
                case '=': expr = new IntConstNode(l == r); break;
                case '>': expr = new IntConstNode(l > r); break;
                case LE: expr = new IntConstNode(l <= r); break;
                case NE: expr = new IntConstNode(l != r); break;
                case GE: expr = new IntConstNode(l >= r); break;
                }
            }
            delete this;
            return expr;
        }
        return this;
    }

    ExprNode *NewNode::semant(Environ *e)
    {
        sem_type = e->findType(ident);
        if (!sem_type) ex("custom type name not found");
        if (sem_type->structType() == 0) ex("type is not a custom type");
        return this;
    }

    ExprNode *FirstNode::semant(Environ *e)
    {
        sem_type = e->findType(ident);
        if (!sem_type) ex("custom type name name not found");
        return this;
    }

    ExprNode *LastNode::semant(Environ *e)
    {
        sem_type = e->findType(ident);
        if (!sem_type) ex("custom type name not found");
        return this;
    }

    ExprNode *AfterNode::semant(Environ *e)
    {
        expr = expr->semant(e);
        if (expr->sem_type == Type::null_type) ex("'After' cannot be used on 'Null'");
        if (expr->sem_type->structType() == 0) ex("'After' must be used with a custom type object");
        sem_type = expr->sem_type;
        return this;
    }

    ExprNode *BeforeNode::semant(Environ *e)
    {
        expr = expr->semant(e);
        if (expr->sem_type == Type::null_type) ex("'Before' cannot be used with 'Null'");
        if (expr->sem_type->structType() == 0) ex("'Before' must be used with a custom type object");
        sem_type = expr->sem_type;
        return this;
    }

    ExprNode *NullNode::semant(Environ *e)
    {
        (void)e;
        sem_type = Type::null_type;
        return this;
    }

    ExprNode *ObjectCastNode::semant(Environ *e)
    {
        expr = expr->semant(e);
        expr = expr->castTo(Type::int_type, e);
        sem_type = e->findType(type_ident);
        if (!sem_type) ex("custom type name not found");
        if (!sem_type->structType()) ex("type is not a custom type");
        return this;
    }

    ExprNode *ObjectHandleNode::semant(Environ *e)
    {
        expr = expr->semant(e);
        if (!expr->sem_type->structType()) ex("'ObjectHandle' must be used with an object");
        sem_type = Type::int_type;
        return this;
    }

    /* ================= Variables ================= */
    void DeclVarNode::semant(Environ *e) { (void)e; }

    void IdentVarNode::semant(Environ *e)
    {
        if (sem_decl) return;
        Type *t = tagType(tag, e);
        if (!t) t = Type::int_type;
        if ((sem_decl = e->findDecl(ident)))
        {
            if (!(sem_decl->kind & (DECL_GLOBAL | DECL_LOCAL | DECL_PARAM)))
                ex("Identifier '" + sem_decl->name + "' may not be used like this");
            Type *ty = sem_decl->type;
            if (ty->constType()) ty = ty->constType()->valueType;
            if (tag.size() && t != ty) ex("Variable type mismatch");
        }
        else
        {
            /* implicit local declaration */
            sem_decl = e->decls->insertDecl(ident, t, DECL_LOCAL);
        }
        sem_type = sem_decl->type;
    }

    void ArrayVarNode::semant(Environ *e)
    {
        exprs->semant(e);
        exprs->castTo(Type::int_type, e);
        Type *t = e->findType(tag);
        sem_decl = e->findDecl(ident);
        if (!sem_decl || !(sem_decl->kind & DECL_ARRAY)) ex("Array not found");
        ArrayType *a = sem_decl->type->arrayType();
        if (t && t != a->elementType) ex("array type mismtach");
        if (a->dims != exprs->size()) ex("incorrect number of dimensions");
        sem_type = a->elementType;
    }

    void FieldVarNode::semant(Environ *e)
    {
        expr = expr->semant(e);
        StructType *s = expr->sem_type->structType();
        if (!s) ex("Variable must be a Type");
        sem_field = s->fields->findDecl(ident);
        if (!sem_field) ex("Type field not found");
        sem_type = sem_field->type;
    }

    void VectorVarNode::semant(Environ *e)
    {
        expr = expr->semant(e);
        vec_type = expr->sem_type->vectorType();
        if (!vec_type) ex("Variable must be a Blitz array");
        if (vec_type->sizes.size() != (size_t)exprs->size()) ex("Incorrect number of subscripts");
        exprs->semant(e);
        exprs->castTo(Type::int_type, e);
        for (int k = 0; k < exprs->size(); ++k)
        {
            if (ConstNode *t = exprs->exprs[k]->constNode())
                if (t->intValue() >= vec_type->sizes[k]) ex("Blitz array subscript out of range");
        }
        sem_type = vec_type->elementType;
    }

    /* ================= Declarations ================= */
    void DeclSeqNode::proto(DeclSeq *d, Environ *e)
    {
        for (size_t k = 0; k < decls.size(); ++k)
        {
            try { decls[k]->proto(d, e); }
            catch (Ex &x)
            {
                if (x.pos < 0) x.pos = decls[k]->pos;
                if (!x.file.size()) x.file = decls[k]->file;
                throw;
            }
        }
    }

    void DeclSeqNode::semant(Environ *e)
    {
        for (size_t k = 0; k < decls.size(); ++k)
        {
            try { decls[k]->semant(e); }
            catch (Ex &x)
            {
                if (x.pos < 0) x.pos = decls[k]->pos;
                if (!x.file.size()) x.file = decls[k]->file;
                throw;
            }
        }
    }

    void VarDeclNode::proto(DeclSeq *d, Environ *e)
    {
        Type *ty = tagType(tag, e);
        if (!ty) ty = Type::int_type;
        ConstType *defType = 0;

        if (expr)
        {
            expr = expr->semant(e);
            expr = expr->castTo(ty, e);
            if (constant || (kind & DECL_PARAM))
            {
                ConstNode *c = expr->constNode();
                if (!c) ex("Expression must be constant");
                if (ty == Type::int_type) ty = new ConstType(c->intValue());
                else if (ty == Type::float_type) ty = new ConstType(c->floatValue());
                else ty = new ConstType(c->stringValue());
                e->types.push_back(ty);
                delete expr;
                expr = 0;
            }
            if (kind & DECL_PARAM)
            {
                defType = ty->constType();
                ty = defType->valueType;
            }
        }
        else if (constant) ex("Constants must be initialized");

        Decl *decl = d->insertDecl(ident, ty, kind, defType);
        if (!decl) ex("Duplicate variable name");
        if (expr) sem_var = new DeclVarNode(decl);
    }

    void VarDeclNode::semant(Environ *e) { (void)e; }

    FuncDeclNode::~FuncDeclNode()
    {
        delete params;
        delete stmts;
    }

    void FuncDeclNode::proto(DeclSeq *d, Environ *e)
    {
        Type *t = tagType(tag, e);
        if (!t) t = Type::int_type;
        a_ptr<DeclSeq> decls(new DeclSeq());
        params->proto(decls, e);
        sem_type = new FuncType(t, decls.release(), false, false);
        sem_decl = d->insertDecl(ident, sem_type, DECL_FUNC);
        if (!sem_decl)
        {
            delete sem_type;
            sem_type = 0;
            ex("duplicate identifier");
        }
        e->types.push_back(sem_type);
    }

    void FuncDeclNode::semant(Environ *e)
    {
        sem_env = new Environ(genLabel(), sem_type->returnType, 1, e);
        DeclSeq *decls = sem_env->decls;

        for (int k = 0; k < sem_type->params->size(); ++k)
        {
            Decl *d = sem_type->params->decls[k];
            if (!decls->insertDecl(d->name, d->type, d->kind)) ex("duplicate identifier");
        }

        stmts->semant(sem_env);
    }

    void StructDeclNode::proto(DeclSeq *d, Environ *e)
    {
        sem_type = new StructType(ident, new DeclSeq());
        if (!d->insertDecl(ident, sem_type, DECL_STRUCT))
        {
            delete sem_type;
            sem_type = 0;
            ex("Duplicate identifier");
        }
        e->types.push_back(sem_type);
    }

    void StructDeclNode::semant(Environ *e)
    {
        fields->proto(sem_type->fields, e);
        for (int k = 0; k < sem_type->fields->size(); ++k) sem_type->fields->decls[k]->offset = k;
    }

    void DataDeclNode::proto(DeclSeq *d, Environ *e)
    {
        (void)d;
        expr = expr->semant(e);
        ConstNode *c = expr->constNode();
        if (!c) ex("Data expression must be constant");
    }

    void VectorDeclNode::proto(DeclSeq *d, Environ *env)
    {
        Type *ty = tagType(tag, env);
        if (!ty) ty = Type::int_type;

        vector<int> sizes;
        for (int k = 0; k < exprs->size(); ++k)
        {
            ExprNode *e = exprs->exprs[k] = exprs->exprs[k]->semant(env);
            ConstNode *c = e->constNode();
            if (!c) ex("Blitz array sizes must be constant");
            long long n = c->intValue();
            if (n < 0) ex("Blitz array sizes must not be negative");
            sizes.push_back((int)n + 1);
        }
        string label = genLabel();
        sem_type = new VectorType(label, ty, sizes);
        sem_decl = d->insertDecl(ident, sem_type, kind);
        if (!sem_decl)
        {
            delete sem_type;
            sem_type = 0;
            ex("Duplicate identifier");
        }
        env->types.push_back(sem_type);
    }

    /* ================= Statements ================= */
    void StmtSeqNode::semant(Environ *e)
    {
        for (size_t k = 0; k < stmts.size(); ++k)
        {
            try { stmts[k]->semant(e); }
            catch (Ex &x)
            {
                if (x.pos < 0) x.pos = stmts[k]->pos;
                if (!x.file.size()) x.file = file;
                throw;
            }
        }
    }

    void IncludeNode::semant(Environ *e) { stmts->semant(e); }

    void DeclStmtNode::semant(Environ *e)
    {
        decl->proto(e->decls, e);
        decl->semant(e);
    }

    void DimNode::semant(Environ *e)
    {
        Type *t = tagType(tag, e);
        if (Decl *d = e->findDecl(ident))
        {
            ArrayType *a = d->type->arrayType();
            if (!a || a->dims != exprs->size() || (t && a->elementType != t)) ex("Duplicate identifier");
            sem_type = a;
            sem_decl = 0;
            sem_array = d;
        }
        else
        {
            if (e->level > 0) ex("Array not found in main program");
            if (!t) t = Type::int_type;
            sem_type = new ArrayType(t, exprs->size());
            sem_decl = e->decls->insertDecl(ident, sem_type, DECL_ARRAY);
            sem_array = sem_decl;
            e->types.push_back(sem_type);
        }
        exprs->semant(e);
        exprs->castTo(Type::int_type, e);
    }

    void AssNode::semant(Environ *e)
    {
        var->semant(e);
        if (var->sem_type->constType()) ex("Constants can not be assigned to");
        if (var->sem_type->vectorType()) ex("Blitz arrays can not be assigned to");
        expr = expr->semant(e);
        expr = expr->castTo(var->sem_type, e);
    }

    void ExprStmtNode::semant(Environ *e) { expr = expr->semant(e); }

    void LabelNode::semant(Environ *e)
    {
        if (Label *l = e->findLabel(ident))
        {
            if (l->def >= 0) ex("duplicate label");
            l->def = pos;
            l->data_sz = data_sz;
        }
        else e->insertLabel(ident, pos, -1, data_sz);
        ident = e->funcLabel + ident;
    }

    void RestoreNode::semant(Environ *e)
    {
        if (e->level > 0) e = e->globals;
        if (ident.size() == 0) sem_label = 0;
        else
        {
            sem_label = e->findLabel(ident);
            if (!sem_label) sem_label = e->insertLabel(ident, -1, pos, -1);
        }
    }

    void GotoNode::semant(Environ *e)
    {
        if (!e->findLabel(ident)) e->insertLabel(ident, -1, pos, -1);
        ident = e->funcLabel + ident;
    }

    void GosubNode::semant(Environ *e)
    {
        if (e->level > 0) ex("'Gosub' may not be used inside a function");
        if (!e->findLabel(ident)) e->insertLabel(ident, -1, pos, -1);
        ident = e->funcLabel + ident;
    }

    void IfNode::semant(Environ *e)
    {
        expr = expr->semant(e);
        expr = expr->castTo(Type::int_type, e);
        stmts->semant(e);
        if (elseOpt) elseOpt->semant(e);
    }

    void ExitNode::semant(Environ *e)
    {
        sem_brk = e->breakLabel;
        if (!sem_brk.size()) ex("break must appear inside a loop");
    }

    void WhileNode::semant(Environ *e)
    {
        expr = expr->semant(e);
        expr = expr->castTo(Type::int_type, e);
        string brk = e->setBreak(sem_brk = genLabel());
        stmts->semant(e);
        e->setBreak(brk);
    }

    ForNode::ForNode(VarNode *var, ExprNode *from, ExprNode *to, ExprNode *step, StmtSeqNode *ss, int np)
        : nextPos(np), var(var), fromExpr(from), toExpr(to), stepExpr(step), stmts(ss) {}

    ForNode::~ForNode()
    {
        delete stmts;
        delete stepExpr;
        delete toExpr;
        delete fromExpr;
        delete var;
    }

    void ForNode::semant(Environ *e)
    {
        var->semant(e);
        Type *ty = var->sem_type;
        if (ty->constType()) ex("Index variable can not be constant");
        if (ty != Type::int_type && ty != Type::float_type) ex("index variable must be integer or real");
        fromExpr = fromExpr->semant(e);
        fromExpr = fromExpr->castTo(ty, e);
        toExpr = toExpr->semant(e);
        toExpr = toExpr->castTo(ty, e);
        stepExpr = stepExpr->semant(e);
        stepExpr = stepExpr->castTo(ty, e);

        if (!stepExpr->constNode()) ex("Step value must be constant");

        string brk = e->setBreak(sem_brk = genLabel());
        stmts->semant(e);
        e->setBreak(brk);
    }

    void ForEachNode::semant(Environ *e)
    {
        var->semant(e);
        Type *ty = var->sem_type;
        if (ty->structType() == 0) ex("Index variable is not a NewType");
        Type *t = e->findType(typeIdent);
        if (!t) ex("Type name not found");
        if (t != ty) ex("Type mismatch");

        string brk = e->setBreak(sem_brk = genLabel());
        stmts->semant(e);
        e->setBreak(brk);
    }

    void ReturnNode::semant(Environ *e)
    {
        sem_level = e->level;
        if (e->level <= 0 && expr) ex("Main program cannot return a value");
        if (e->level > 0)
        {
            if (!expr)
            {
                if (e->returnType == Type::float_type) expr = new FloatConstNode(0);
                else if (e->returnType == Type::string_type) expr = new StringConstNode("");
                else if (e->returnType->structType()) expr = new NullNode();
                else expr = new IntConstNode(0);
            }
            expr = expr->semant(e);
            expr = expr->castTo(e->returnType, e);
            returnLabel = e->funcLabel + "_leave";
        }
    }

    void DeleteNode::semant(Environ *e)
    {
        expr = expr->semant(e);
        if (expr->sem_type->structType() == 0) ex("Can't delete non-Newtype");
    }

    void DeleteEachNode::semant(Environ *e)
    {
        Type *t = e->findType(typeIdent);
        if (!t || t->structType() == 0) ex("Specified name is not a NewType name");
        sem_struct = t->structType();
    }

    void InsertNode::semant(Environ *e)
    {
        expr1 = expr1->semant(e);
        expr2 = expr2->semant(e);
        StructType *t1 = expr1->sem_type->structType();
        StructType *t2 = expr2->sem_type->structType();
        if (!t1 || !t2) ex("Illegal expression type");
        if (t1 != t2) ex("Objects types are differnt");
    }

    SelectNode::~SelectNode()
    {
        for (; cases.size(); cases.pop_back()) delete cases.back();
        delete expr;
        delete defStmts;
        delete sem_temp;
    }

    void SelectNode::semant(Environ *e)
    {
        expr = expr->semant(e);
        Type *ty = expr->sem_type;
        if (ty->structType()) ex("Select cannot be used with objects");

        Decl *d = e->decls->insertDecl(genLabel(), expr->sem_type, DECL_LOCAL);
        sem_temp = new DeclVarNode(d);

        for (size_t k = 0; k < cases.size(); ++k)
        {
            CaseNode *c = cases[k];
            c->exprs->semant(e);
            c->exprs->castTo(ty, e);
            c->stmts->semant(e);
        }
        if (defStmts) defStmts->semant(e);
    }

    void RepeatNode::semant(Environ *e)
    {
        sem_brk = genLabel();
        string brk = e->setBreak(sem_brk);
        stmts->semant(e);
        e->setBreak(brk);
        if (expr)
        {
            expr = expr->semant(e);
            expr = expr->castTo(Type::int_type, e);
        }
    }

    void ReadNode::semant(Environ *e)
    {
        var->semant(e);
        if (var->sem_type->constType()) ex("Constants can not be modified");
        if (var->sem_type->structType()) ex("Data can not be read into an object");
    }

    /* ================= Program ================= */
    ProgNode::~ProgNode()
    {
        delete stmts;
        delete datas;
        delete funcs;
        delete structs;
        delete consts;
        delete sem_env;
    }

    Environ *ProgNode::semant(Environ *e)
    {
        a_ptr<Environ> env(new Environ(genLabel(), Type::int_type, 0, e));
        consts->proto(env->decls, env);
        structs->proto(env->typeDecls, env);
        structs->semant(env);
        funcs->proto(env->funcDecls, env);
        stmts->semant(env);
        funcs->semant(env);
        datas->proto(env->decls, env);
        datas->semant(env);
        sem_env = env.release();
        return sem_env;
    }
}
