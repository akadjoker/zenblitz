/*
** c_codegen.h - native C++ source generator scaffold.
*/
#ifndef ZEN_C_CODEGEN_H
#define ZEN_C_CODEGEN_H

#include "bb_nodes.h"

#include <iosfwd>
#include <map>
#include <set>
#include <string>

namespace bb
{
    class CGen
    {
    public:
        CGen(std::ostream &out, const std::string &filename);
        void compile(ProgNode *program);
        void register_builtin(Decl *decl, const char *wrapper, bool uses_window = false);

    private:
        std::ostream &out_;
        std::string filename_;
        std::map<Decl *, std::string> names_;
        std::map<Decl *, std::string> global_names_;
        int indent_;
        int temp_;
        bool uses_window_;
        bool in_condition_;
        std::set<std::string> goto_labels_;
        std::vector<std::pair<int, std::string> > gosub_sites_;
        int gosub_id_;
        std::map<Decl *, std::string> builtin_wrappers_;
        std::set<Decl *> window_builtins_;
        /* the program's own functions, so a call that resolved to one is
           never mistaken for a command of the same name */
        std::set<Decl *> user_functions_;

        void emit_statement(StmtNode *statement);
        void emit_statements(StmtSeqNode *statements);
        void emit_struct(StructDeclNode *structure);
        void emit_struct_string(StructDeclNode *structure);
        void emit_function_prototype(FuncDeclNode *function);
        void emit_function(FuncDeclNode *function);
        void note_window_use(Decl *decl);
        void declare_variable(Decl *decl);
        const char *native_wrapper(const std::string &name) const;
        std::string emit_expression(ExprNode *expression);
        std::string emit_expression_ordered(ExprNode *expression);
        std::string emit_variable(VarNode *variable);
        std::string emit_variable_decl(Decl *decl);
        std::string sanitize_label(const std::string &ident) const;
        void emit_data(ProgNode *program);
        std::string emit_type(Type *type) const;
        void line(const std::string &text);
        std::string temporary(const char *prefix);
    };
}

#endif