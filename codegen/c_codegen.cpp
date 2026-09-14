/*
** c_codegen.cpp - native C++ source generator scaffold.
*/
#include "c_codegen.h"
#include "zen_native_cmds.hpp"

#include <cstdlib>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <typeinfo>
#include <vector>

namespace bb
{
    static bool generated_command(const std::string &name)
    {
        for (unsigned i = 0; i < zen_native_command_count; ++i)
        {
            std::string candidate = zen_native_commands[i].name;
            for (size_t k = 0; k < candidate.size(); ++k)
                candidate[k] = (char)std::tolower((unsigned char)candidate[k]);
            if (candidate == name) return true;
        }
        return false;
    }

    const char *CGen::native_wrapper(const std::string &name) const
    {
        for (unsigned i = 0; i < zen_native_command_count; ++i)
        {
            std::string candidate = zen_native_commands[i].name;
            for (size_t k = 0; k < candidate.size(); ++k)
                candidate[k] = (char)std::tolower((unsigned char)candidate[k]);
            if (candidate == name && zen_native_commands[i].native_wrapper[0])
                return zen_native_commands[i].native_wrapper;
        }
        return 0;
    }

    CGen::CGen(std::ostream &out, const std::string &filename)
        : out_(out), filename_(filename), indent_(0), temp_(0), uses_window_(false)
    {
    }

    void CGen::line(const std::string &text)
    {
        for (int i = 0; i < indent_; ++i) out_ << "    ";
        out_ << text << '\n';
    }

    std::string CGen::temporary(const char *prefix)
    {
        std::ostringstream name;
        name << "z_" << prefix << temp_++;
        return name.str();
    }

    std::string CGen::emit_type(Type *type) const
    {
        if (type == Type::float_type) return "double";
        if (type == Type::string_type) return "std::string";
        return "int64_t";
    }

    void CGen::declare_variable(Decl *decl)
    {
        if (!decl || names_.find(decl) != names_.end()) return;
        names_[decl] = "z_" + decl->name;
    }

    std::string CGen::emit_variable(VarNode *variable)
    {
        DeclVarNode *declared = dynamic_cast<DeclVarNode *>(variable);
        if (!declared || !declared->sem_decl)
            throw Ex("CGen: unsupported variable node");
        std::map<Decl *, std::string>::const_iterator found = names_.find(declared->sem_decl);
        if (found != names_.end()) return found->second;
        std::string name = "z_" + declared->sem_decl->name;
        names_[declared->sem_decl] = name;
        return name;
    }

    std::string CGen::emit_expression(ExprNode *expression)
    {
        if (!expression)
            throw Ex("CGen: null expression node");
        if (IntConstNode *value = dynamic_cast<IntConstNode *>(expression))
        {
            std::ostringstream text;
            text << value->intValue();
            return text.str();
        }
        if (FloatConstNode *value = dynamic_cast<FloatConstNode *>(expression))
        {
            std::ostringstream text;
            text << std::setprecision(17) << value->floatValue();
            return text.str();
        }
        if (StringConstNode *value = dynamic_cast<StringConstNode *>(expression))
        {
            std::string escaped = value->stringValue();
            std::string result = "\"";
            for (size_t i = 0; i < escaped.size(); ++i)
            {
                if (escaped[i] == '\\' || escaped[i] == '"') result += '\\';
                result += escaped[i];
            }
            return result + "\"";
        }
        if (VarExprNode *variable = dynamic_cast<VarExprNode *>(expression))
            return emit_variable(variable->var);
        if (UniExprNode *unary = dynamic_cast<UniExprNode *>(expression))
        {
            std::string value = emit_expression(unary->expr);
            if (unary->op == '+') return "(+" + value + ")";
            if (unary->op == '-') return "(-" + value + ")";
            if (unary->op == NOT) return "(!" + value + ")";
            if (unary->op == ABS) return "std::fabs(" + value + ")";
            if (unary->op == SGN)
                return "((" + value + " > 0) ? 1 : (" + value + " < 0 ? -1 : 0))";
            throw Ex("CGen: unsupported unary node");
        }
        if (CastNode *cast = dynamic_cast<CastNode *>(expression))
        {
            std::string value = emit_expression(cast->expr);
            Type *from = cast->expr ? cast->expr->sem_type : 0;
            if (cast->type == Type::string_type)
            {
                if (from == Type::string_type) return value;
                return "zen_str(" + value + ")";
            }
            if (cast->type == Type::float_type)
            {
                if (from == Type::string_type) return "zen_float(" + value + ")";
                return "static_cast<double>(" + value + ")";
            }
            if (cast->type == Type::int_type)
            {
                if (from == Type::string_type) return "zen_int(" + value + ")";
                return "static_cast<int64_t>(" + value + ")";
            }
            throw Ex("CGen: unsupported cast target");
        }
        if (CallNode *call = dynamic_cast<CallNode *>(expression))
        {
            if (!call->exprs)
                throw Ex("CGen: unsupported call expression " + call->ident);
            std::vector<std::string> arguments;
            for (size_t i = 0; i < call->exprs->exprs.size(); ++i)
                arguments.push_back(emit_expression(call->exprs->exprs[i]));
            if (arguments.size() == 1)
            {
                std::string argument = arguments[0];
                Type *argument_type = call->exprs->exprs[0]->sem_type;
            if (call->ident == "int")
            {
                if (argument_type == Type::string_type) return "zen_int(" + argument + ")";
                return "static_cast<int64_t>(" + argument + ")";
            }
            if (call->ident == "float")
            {
                if (argument_type == Type::string_type) return "zen_float(" + argument + ")";
                return "static_cast<double>(" + argument + ")";
            }
            if (call->ident == "str")
            {
                if (argument_type == Type::string_type) return argument;
                return "zen_str(" + argument + ")";
            }
            if (call->ident == "len")
                return "zen_len(" + argument + ")";
            if (call->ident == "upper") return "zen_upper(" + argument + ")";
            if (call->ident == "lower") return "zen_lower(" + argument + ")";
            if (call->ident == "chr") return "zen_chr(" + argument + ")";
            if (call->ident == "asc") return "zen_asc(" + argument + ")";
            if (call->ident == "hex") return "zen_hex(" + argument + ")";
            if (call->ident == "bin") return "zen_bin(" + argument + ")";
            if (call->ident == "sin") return "zen_sin(" + argument + ")";
            if (call->ident == "cos") return "zen_cos(" + argument + ")";
            if (call->ident == "tan") return "zen_tan(" + argument + ")";
            if (call->ident == "sqr") return "zen_sqr(" + argument + ")";
            if (call->ident == "floor") return "zen_floor(" + argument + ")";
            if (call->ident == "ceil") return "zen_ceil(" + argument + ")";
            if (call->ident == "exp") return "zen_exp(" + argument + ")";
            if (call->ident == "log") return "zen_log(" + argument + ")";
            if (call->ident == "log10") return "zen_log10(" + argument + ")";
            }
            if (call->ident == "left" && arguments.size() == 2)
                return "zen_left(" + arguments[0] + ", " + arguments[1] + ")";
            if (call->ident == "right" && arguments.size() == 2)
                return "zen_right(" + arguments[0] + ", " + arguments[1] + ")";
            if (call->ident == "mid" && (arguments.size() == 2 || arguments.size() == 3))
                return "zen_mid(" + arguments[0] + ", " + arguments[1] + ", " +
                       (arguments.size() == 3 ? arguments[2] : "-1") + ")";
            if (call->ident == "replace" && arguments.size() == 3)
                return "zen_replace(" + arguments[0] + ", " + arguments[1] + ", " + arguments[2] + ")";
            if (call->ident == "trim" && arguments.size() == 1)
                return "zen_trim(" + arguments[0] + ")";
            if (call->ident == "lset" && arguments.size() == 2)
                return "zen_lset(" + arguments[0] + ", " + arguments[1] + ")";
            if (call->ident == "rset" && arguments.size() == 2)
                return "zen_rset(" + arguments[0] + ", " + arguments[1] + ")";
            if (call->ident == "instr" && (arguments.size() == 2 || arguments.size() == 3))
                return "zen_instr(" + arguments[0] + ", " + arguments[1] + ", " +
                       (arguments.size() == 3 ? arguments[2] : "1") + ")";
            if (call->ident == "string" && arguments.size() == 2)
                return "zen_string(" + arguments[0] + ", " + arguments[1] + ")";
            if (const char *wrapper = native_wrapper(call->ident))
            {
                std::string result = std::string(wrapper) + "(";
                for (size_t i = 0; i < arguments.size(); ++i)
                {
                    if (i) result += ", ";
                    result += arguments[i];
                }
                return result + ")";
            }
            if (generated_command(call->ident))
                throw Ex("CGen: registered command has no native wrapper " + call->ident);
            if (call->sem_decl && call->sem_decl->type && call->sem_decl->type->funcType())
            {
                std::string result = "z_" + call->ident + "(";
                for (size_t i = 0; i < arguments.size(); ++i)
                {
                    if (i) result += ", ";
                    result += arguments[i];
                }
                return result + ")";
            }
            throw Ex("CGen: unsupported call expression " + call->ident);
        }
        if (ArithExprNode *arithmetic = dynamic_cast<ArithExprNode *>(expression))
        {
            const char *op = 0;
            switch (arithmetic->op)
            {
            case '+': op = "+"; break;
            case '-': op = "-"; break;
            case '*': op = "*"; break;
            case '/': op = "/"; break;
            case MOD:
                if (arithmetic->sem_type == Type::float_type)
                    return "std::fmod(" + emit_expression(arithmetic->lhs) + ", " +
                           emit_expression(arithmetic->rhs) + ")";
                op = "%";
                break;
            case '^':
                return "std::pow(" + emit_expression(arithmetic->lhs) + ", " +
                       emit_expression(arithmetic->rhs) + ")";
            default: throw Ex("CGen: unsupported arithmetic node");
            }
            return "(" + emit_expression(arithmetic->lhs) + " " + op + " " +
                   emit_expression(arithmetic->rhs) + ")";
        }
        if (BinExprNode *binary = dynamic_cast<BinExprNode *>(expression))
        {
            const char *op = 0;
            switch (binary->op)
            {
            case AND: op = "&"; break;
            case OR: op = "|"; break;
            case XOR: op = "^"; break;
            case SHL: op = "<<"; break;
            case SHR: op = ">>"; break;
            case SAR: op = ">>"; break;
            default: throw Ex("CGen: unsupported binary node");
            }
            return "(" + emit_expression(binary->lhs) + " " + op + " " +
                   emit_expression(binary->rhs) + ")";
        }
        if (RelExprNode *relation = dynamic_cast<RelExprNode *>(expression))
        {
            const char *op = 0;
            switch (relation->op)
            {
            case '=': op = "=="; break;
            case '<': op = "<"; break;
            case '>': op = ">"; break;
            case LE: op = "<="; break;
            case GE: op = ">="; break;
            case NE: op = "!="; break;
            default: throw Ex("CGen: unsupported relational node");
            }
            return "(" + emit_expression(relation->lhs) + " " + op + " " +
                   emit_expression(relation->rhs) + ")";
        }
        throw Ex("CGen: unsupported expression node " + std::string(typeid(*expression).name()));
    }

    void CGen::emit_statements(StmtSeqNode *statements)
    {
        if (!statements) return;
        for (size_t i = 0; i < statements->stmts.size(); ++i)
            emit_statement(statements->stmts[i]);
    }

    bool CGen::contains_window(StmtSeqNode *statements) const
    {
        if (!statements) return false;
        for (size_t i = 0; i < statements->stmts.size(); ++i)
            if (contains_window(statements->stmts[i])) return true;
        return false;
    }

    bool CGen::contains_window(StmtNode *statement) const
    {
        if (ExprStmtNode *expr = dynamic_cast<ExprStmtNode *>(statement))
        {
            CallNode *call = dynamic_cast<CallNode *>(expr->expr);
            if (call && native_wrapper(call->ident))
                return true;
        }
        if (IfNode *conditional = dynamic_cast<IfNode *>(statement))
            return contains_window(conditional->stmts) || contains_window(conditional->elseOpt);
        if (WhileNode *loop = dynamic_cast<WhileNode *>(statement))
            return contains_window(loop->stmts);
        if (ForNode *loop = dynamic_cast<ForNode *>(statement))
            return contains_window(loop->stmts);
        return false;
    }

    void CGen::emit_statement(StmtNode *statement)
    {
        if (DeclStmtNode *declaration = dynamic_cast<DeclStmtNode *>(statement))
        {
            VarDeclNode *variable = dynamic_cast<VarDeclNode *>(declaration->decl);
            if (variable && variable->sem_var && variable->sem_var->sem_decl)
            {
                Decl *decl = variable->sem_var->sem_decl;
                declare_variable(decl);
                if (variable->expr)
                    line(names_[decl] + " = " + emit_expression(variable->expr) + ";");
            }
            return;
        }
        if (AssNode *assignment = dynamic_cast<AssNode *>(statement))
        {
            line(emit_variable(assignment->var) + " = " + emit_expression(assignment->expr) + ";");
            return;
        }
        if (ExprStmtNode *expression = dynamic_cast<ExprStmtNode *>(statement))
        {
            CallNode *call = dynamic_cast<CallNode *>(expression->expr);
            if (call && call->ident == "print" && call->exprs && !call->exprs->exprs.empty())
            {
                ExprNode *value = call->exprs->exprs[0];
                line("zen_print(" + emit_expression(value) + ");");
                return;
            }
            if (call && (call->ident == "graphics" || call->ident == "graphics3d"))
            {
                if (!call->exprs || call->exprs->exprs.size() < 2)
                    throw Ex("CGen: Graphics requires width and height");
                line("zen_graphics_open(" + emit_expression(call->exprs->exprs[0]) + ", " +
                     emit_expression(call->exprs->exprs[1]) + ");");
                return;
            }
            if (call && call->ident == "flip")
            {
                line("zen_flip();");
                return;
            }
            if (call && call->ident == "endgraphics")
            {
                line("zen_graphics_close();");
                return;
            }
            if (call && generated_command(call->ident))
                throw Ex("CGen: registered command has no native wrapper " + call->ident);
            throw Ex("CGen: unsupported call statement " + call->ident);
        }
        if (ReturnNode *result = dynamic_cast<ReturnNode *>(statement))
        {
            line("return " + emit_expression(result->expr) + ";");
            return;
        }
        if (IfNode *conditional = dynamic_cast<IfNode *>(statement))
        {
            line("if (" + emit_expression(conditional->expr) + ") {");
            ++indent_;
            emit_statements(conditional->stmts);
            --indent_;
            if (conditional->elseOpt)
            {
                line("} else {");
                ++indent_;
                emit_statements(conditional->elseOpt);
                --indent_;
            }
            line("}");
            return;
        }
        if (WhileNode *loop = dynamic_cast<WhileNode *>(statement))
        {
            line("while (" + emit_expression(loop->expr) + ") {");
            ++indent_;
            emit_statements(loop->stmts);
            --indent_;
            line("}");
            return;
        }
        if (ForNode *loop = dynamic_cast<ForNode *>(statement))
        {
            std::string variable = emit_variable(loop->var);
            std::string step = temporary("step");
            line("auto " + step + " = " + emit_expression(loop->stepExpr) + ";");
            line("for (" + variable + " = " + emit_expression(loop->fromExpr) + "; " +
                 "(" + step + " >= 0 ? " + variable + " <= " + emit_expression(loop->toExpr) +
                 " : " + variable + " >= " + emit_expression(loop->toExpr) + "); " +
                 variable + " += " + step + ") {");
            ++indent_;
            emit_statements(loop->stmts);
            --indent_;
            line("}");
            return;
        }
        throw Ex("CGen: unsupported statement node " + std::string(typeid(*statement).name()));
    }

    void CGen::emit_function(FuncDeclNode *function)
    {
        if (!function || !function->sem_type || !function->sem_env)
            throw Ex("CGen: malformed function declaration");

        names_ = global_names_;
        std::string return_type = emit_type(function->sem_type->returnType);
        out_ << "static " << return_type << " z_" << function->ident << "(";
        for (size_t i = 0; i < function->sem_type->params->decls.size(); ++i)
        {
            Decl *decl = function->sem_type->params->decls[i];
            if (i) out_ << ", ";
            out_ << emit_type(decl->type) << " z_" << decl->name;
        }
        out_ << ")\n{\n";
        indent_ = 1;
        for (size_t i = 0; i < function->sem_env->decls->decls.size(); ++i)
        {
            Decl *decl = function->sem_env->decls->decls[i];
            names_[decl] = "z_" + decl->name;
            bool parameter = false;
            for (size_t k = 0; k < function->sem_type->params->decls.size(); ++k)
                if (function->sem_type->params->decls[k]->name == decl->name)
                    parameter = true;
            if (parameter) continue;
            line(emit_type(decl->type) + " " + names_[decl] + " = 0;");
        }
        emit_statements(function->stmts);
        if (function->sem_type->returnType == Type::void_type)
            line("return;");
        else
            line("return 0;");
        out_ << "}\n\n";
        names_ = global_names_;
    }

    void CGen::compile(ProgNode *program)
    {
        uses_window_ = contains_window(program ? program->stmts : 0);
        out_ << "#include \"zen_codegen_support.hpp\"\n\n";
        if (uses_window_)
            out_ << "#include \"zen_codegen_window.hpp\"\n\n";
        global_names_.clear();
        names_.clear();
        if (program && program->sem_env && program->sem_env->decls)
        {
            for (size_t i = 0; i < program->sem_env->decls->decls.size(); ++i)
            {
                Decl *decl = program->sem_env->decls->decls[i];
                if (decl->type->arrayType() || decl->type->structType())
                    throw Ex("CGen: unsupported global type " + decl->name);
                global_names_[decl] = "z_" + decl->name;
            }
            names_ = global_names_;
            for (std::map<Decl *, std::string>::const_iterator it = global_names_.begin();
                 it != global_names_.end(); ++it)
            {
                std::string type = emit_type(it->first->type);
                std::string initial = type == "std::string" ? "{}" : "0";
                out_ << "static " << type << " " << it->second << " = " << initial << ";\n";
            }
            out_ << "\n";
        }
        if (program && program->funcs)
            for (size_t i = 0; i < program->funcs->decls.size(); ++i)
                emit_function(dynamic_cast<FuncDeclNode *>(program->funcs->decls[i]));
        out_ << "int main()\n";
        out_ << "{\n";
        indent_ = 1;
        names_ = global_names_;
        for (std::map<Decl *, std::string>::const_iterator it = global_names_.begin(); it != global_names_.end(); ++it)
        {
            line("(void)" + it->second + ";");
        }
        emit_statements(program ? program->stmts : 0);
        out_ << "    return 0;\n";
        out_ << "}\n";
    }
}