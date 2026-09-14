/*
** c_codegen.h - native C++ source generator scaffold.
*/
#ifndef ZEN_C_CODEGEN_H
#define ZEN_C_CODEGEN_H

#include "bb_nodes.h"

#include <iosfwd>
#include <map>
#include <string>

namespace bb
{
    class CGen
    {
    public:
        CGen(std::ostream &out, const std::string &filename);

        void compile(ProgNode *program);

    private:
        std::ostream &out_;
        std::string filename_;
        std::map<Decl *, std::string> names_;
        std::map<Decl *, std::string> global_names_;
        int indent_;
        int temp_;
        bool uses_window_;

        void emit_statement(StmtNode *statement);
        void emit_statements(StmtSeqNode *statements);
        void emit_function(FuncDeclNode *function);
        bool contains_window(StmtNode *statement) const;
        bool contains_window(StmtSeqNode *statements) const;
        void declare_variable(Decl *decl);
        const char *native_wrapper(const std::string &name) const;
        std::string emit_expression(ExprNode *expression);
        std::string emit_variable(VarNode *variable);
        std::string emit_type(Type *type) const;
        void line(const std::string &text);
        std::string temporary(const char *prefix);
    };
}

#endif