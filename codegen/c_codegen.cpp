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

    void CGen::emit_struct(StructDeclNode *structure)
    {
        if (!structure || !structure->sem_type || !structure->sem_type->fields)
            throw Ex("CGen: malformed Type declaration");
        out_ << "struct z_type_" << structure->ident << " : zen_native::Object\n{\n";
        for (size_t i = 0; i < structure->sem_type->fields->decls.size(); ++i)
        {
            Decl *field = structure->sem_type->fields->decls[i];
            out_ << "    " << emit_type(field->type) << " z_" << field->name << ";\n";
        }
        out_ << "    z_type_" << structure->ident << "()";
        bool initialized = false;
        for (size_t i = 0; i < structure->sem_type->fields->decls.size(); ++i)
        {
            Decl *field = structure->sem_type->fields->decls[i];
            if (field->type->vectorType()) continue;
            out_ << (initialized ? ", " : " : ");
            out_ << "z_" << field->name << "("
                 << (field->type->structType() ? "0" : field->type == Type::string_type ? "\"\"" : "0")
                 << ")";
            initialized = true;
        }
        out_ << " {}\n";
        out_ << "    void trace(zen_native::Heap &heap)\n    {\n        (void)heap;\n";
        for (size_t i = 0; i < structure->sem_type->fields->decls.size(); ++i)
        {
            Decl *field = structure->sem_type->fields->decls[i];
            if (field->type->structType()) out_ << "        heap.mark(z_" << field->name << ");\n";
        }
        out_ << "    }\n};\n\n";
        out_ << "static zen_native::TypeList<z_type_" << structure->ident << "> *z_list_"
             << structure->ident << ";\n";
        out_ << "static z_type_" << structure->ident << " *z_new_" << structure->ident << "()\n{\n"
             << "    z_type_" << structure->ident << " *object = z_heap.allocate<z_type_"
             << structure->ident << ">();\n    z_list_" << structure->ident
             << "->append(object);\n    return object;\n}\n\n";
    }

    void CGen::emit_struct_string(StructDeclNode *structure)
    {
        if (!structure || !structure->sem_type || !structure->sem_type->fields)
            throw Ex("CGen: malformed Type declaration");
        out_ << "static std::string __attribute__((unused)) z_str_" << structure->ident << "(z_type_"
             << structure->ident << " *object)\n{\n"
             << "    if (!object || !object->alive()) return \"[NULL]\";\n"
             << "    std::string result = \"[\";\n";
        for (size_t i = 0; i < structure->sem_type->fields->decls.size(); ++i)
        {
            Decl *field = structure->sem_type->fields->decls[i];
            if (i) out_ << "    result += \",\";\n";
            if (field->type->structType())
                out_ << "    result += z_str_" << field->type->structType()->ident
                     << "(object->z_" << field->name << ");\n";
            else if (field->type == Type::string_type)
                out_ << "    result += \"\\\"\" + object->z_" << field->name
                     << " + \"\\\"\";\n";
            else if (field->type->vectorType())
                out_ << "    result += \"???\";\n";
            else
                out_ << "    result += zen_str(object->z_" << field->name << ");\n";
        }
        out_ << "    result += \"]\";\n    return result;\n}\n\n";
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
        : out_(out), filename_(filename), indent_(0), temp_(0), uses_window_(false), in_condition_(false), gosub_id_(0)
    {
    }

    void CGen::register_builtin(Decl *decl, const char *wrapper, bool uses_window)
    {
        if (decl) builtin_wrappers_[decl] = wrapper;
        if (decl && uses_window) window_builtins_.insert(decl);
    }

    /* A command that needs the engine header is recorded when its call is
       emitted, not predicted beforehand: the body is written to a buffer
       and the prelude prepended afterwards. A walk that guessed ahead had
       to know every statement node kind, and silently emitted a call with
       no declaration for the ones it did not (Select, ForEach, Include
       each cost a compile failure in turn). */
    void CGen::note_window_use(Decl *decl)
    {
        if (decl && window_builtins_.find(decl) != window_builtins_.end()) uses_window_ = true;
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
        if (type == Type::void_type) return "void";
        if (type == Type::float_type) return "double";
        if (type == Type::string_type) return "std::string";
        if (StructType *structure = type->structType())
            return "z_type_" + structure->ident + " *";
        if (VectorType *vector = type->vectorType())
        {
            std::ostringstream result;
            result << "zen_native::StaticArray<" << emit_type(vector->elementType)
                   << ", " << vector->totalSize() << ">";
            return result.str();
        }
        if (ArrayType *array = type->arrayType())
            return "zen_native::DynArray<" + emit_type(array->elementType) + ">";
        return "int64_t";
    }

    void CGen::declare_variable(Decl *decl)
    {
        if (!decl || names_.find(decl) != names_.end()) return;
        names_[decl] = "z_" + decl->name;
    }

    static void collect_goto_labels(StmtSeqNode *statements, std::set<std::string> &labels)
    {
        if (!statements) return;
        for (size_t i = 0; i < statements->stmts.size(); ++i)
        {
            StmtNode *stmt = statements->stmts[i];
            if (GotoNode *node = dynamic_cast<GotoNode *>(stmt))
                labels.insert(node->ident);
            else if (GosubNode *node = dynamic_cast<GosubNode *>(stmt))
                labels.insert(node->ident);
            else if (IfNode *conditional = dynamic_cast<IfNode *>(stmt))
            {
                collect_goto_labels(conditional->stmts, labels);
                collect_goto_labels(conditional->elseOpt, labels);
            }
            else if (WhileNode *loop = dynamic_cast<WhileNode *>(stmt))
                collect_goto_labels(loop->stmts, labels);
            else if (ForNode *loop = dynamic_cast<ForNode *>(stmt))
                collect_goto_labels(loop->stmts, labels);
            else if (RepeatNode *loop = dynamic_cast<RepeatNode *>(stmt))
                collect_goto_labels(loop->stmts, labels);
            else if (ForEachNode *loop = dynamic_cast<ForEachNode *>(stmt))
                collect_goto_labels(loop->stmts, labels);
        }
    }

    std::string CGen::sanitize_label(const std::string &ident) const
    {
        const std::string normalized = ident.compare(0, 3, "_20") == 0 ? ident.substr(3) : ident;
        std::string result = "z_label_";
        for (size_t i = 0; i < normalized.size(); ++i)
        {
            char c = normalized[i];
            result += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
        }
        return result;
    }

    void CGen::emit_data(ProgNode *program)
    {
        if (!program || !program->datas || program->datas->decls.empty())
        {
            out_ << "static ZenDataValue z_data_values[1] = {{0,0,0.0,std::string()}};\n\n";
            return;
        }
        out_ << "static ZenDataValue z_data_values[] = {\n";
        for (size_t i = 0; i < program->datas->decls.size(); ++i)
        {
            DataDeclNode *data = dynamic_cast<DataDeclNode *>(program->datas->decls[i]);
            if (!data || !data->expr) continue;
            ConstNode *c = data->expr->constNode();
            if (!c) continue;
            Type *t = data->expr->sem_type;
            out_ << "    {";
            if (t == Type::float_type)
                out_ << "1, 0, " << std::setprecision(17) << c->floatValue() << ", std::string()}";
            else if (t == Type::string_type)
            {
                std::string escaped = c->stringValue();
                std::string result;
                for (size_t k = 0; k < escaped.size(); ++k)
                {
                    if (escaped[k] == '\\' || escaped[k] == '"') result += '\\';
                    result += escaped[k];
                }
                out_ << "2, 0, 0.0, std::string(" << char(34) << result << char(34) << ")}";
            }
            else
                out_ << "0, " << c->intValue() << ", 0.0, std::string()}";
            out_ << (i + 1 < program->datas->decls.size() ? "," : "") << "\n";
        }
        out_ << "};\n\n";
    }

    std::string CGen::emit_variable_decl(Decl *decl)
    {
        if (!decl) throw Ex("CGen: null declaration");
        std::map<Decl *, std::string>::const_iterator found = global_names_.find(decl);
        if (found != global_names_.end()) return found->second;
        found = names_.find(decl);
        if (found != names_.end()) return found->second;
        std::string name = "z_" + decl->name;
        names_[decl] = name;
        return name;
    }

    std::string CGen::emit_variable(VarNode *variable)
    {
        if (ArrayVarNode *array = dynamic_cast<ArrayVarNode *>(variable))
        {
            if (!array->sem_decl) throw Ex("CGen: unresolved Dim array");
            std::string name = emit_variable_decl(array->sem_decl);
            std::string index = emit_expression(array->exprs->exprs[0]);
            for (size_t i = 1; i < array->exprs->exprs.size(); ++i)
                index = "(" + index + " * " + name + ".dim_size(" +
                        std::to_string(i) + ")) + " +
                        emit_expression(array->exprs->exprs[i]);
            return name + ".at(" + index + ")";
        }
        if (VectorVarNode *vector = dynamic_cast<VectorVarNode *>(variable))
        {
            std::string index;
            int stride = 1;
            for (size_t i = 0; i < vector->exprs->exprs.size(); ++i)
            {
                if (i) index += " + ";
                if (stride != 1) index += "(" + emit_expression(vector->exprs->exprs[i]) +
                                        " * " + std::to_string(stride) + ")";
                else index += emit_expression(vector->exprs->exprs[i]);
                stride *= vector->vec_type->sizes[i];
            }
            return emit_expression(vector->expr) + "[" + index + "]";
        }
        if (FieldVarNode *field = dynamic_cast<FieldVarNode *>(variable))
        {
            if (!field->sem_field) throw Ex("CGen: unresolved Type field");
            return emit_expression(field->expr) + "->z_" + field->sem_field->name;
        }
        DeclVarNode *declared = dynamic_cast<DeclVarNode *>(variable);
        if (!declared || !declared->sem_decl)
            throw Ex("CGen: unsupported variable node");
        std::map<Decl *, std::string>::const_iterator found = names_.find(declared->sem_decl);
        if (found != names_.end()) return found->second;
        std::string name = "z_" + declared->sem_decl->name;
        names_[declared->sem_decl] = name;
        return name;
    }

    std::string CGen::emit_expression_ordered(ExprNode *expression)
    {
        if (expression && expression->impure() && !in_condition_)
        {
            std::string var = temporary("t");
            line(emit_type(expression->sem_type) + " " + var + " = " +
                 emit_expression(expression) + ";");
            return var;
        }
        return emit_expression(expression);
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
        if (NewNode *node = dynamic_cast<NewNode *>(expression))
            return "z_new_" + node->ident + "()";
        if (FirstNode *node = dynamic_cast<FirstNode *>(expression))
            return "z_list_" + node->ident + "->first()";
        if (LastNode *node = dynamic_cast<LastNode *>(expression))
            return "z_list_" + node->ident + "->last()";
        if (AfterNode *node = dynamic_cast<AfterNode *>(expression))
            return "z_list_" + node->expr->sem_type->structType()->ident + "->after(" +
                   emit_expression(node->expr) + ")";
        if (BeforeNode *node = dynamic_cast<BeforeNode *>(expression))
            return "z_list_" + node->expr->sem_type->structType()->ident + "->before(" +
                   emit_expression(node->expr) + ")";
        if (ObjectHandleNode *node = dynamic_cast<ObjectHandleNode *>(expression))
            return "z_heap.handle(" + emit_expression(node->expr) + ")";
        if (ObjectCastNode *node = dynamic_cast<ObjectCastNode *>(expression))
            return "z_heap.object<z_type_" + node->type_ident + ">(" + emit_expression(node->expr) + ")";
        if (dynamic_cast<NullNode *>(expression)) return "0";
        if (UniExprNode *unary = dynamic_cast<UniExprNode *>(expression))
        {
            std::string value = emit_expression(unary->expr);
            if (unary->op == '+') return "(+" + value + ")";
            if (unary->op == '-') return "(-" + value + ")";
            if (unary->op == NOT) return "(!" + value + ")";
            if (unary->op == ABS)
                return unary->sem_type == Type::int_type ? "std::llabs(" + value + ")" :
                                                           "std::fabs(" + value + ")";
            if (unary->op == SGN)
                return "((" + value + " > 0) ? 1 : (" + value + " < 0 ? -1 : 0))";
            throw Ex("CGen: unsupported unary node");
        }
        if (CastNode *cast = dynamic_cast<CastNode *>(expression))
        {
            std::string value = emit_expression(cast->expr);
            Type *from = cast->expr ? cast->expr->sem_type : 0;
            if (cast->type->structType())
                return value;
            if (cast->type == Type::string_type)
            {
                if (from == Type::string_type) return value;
                if (from && from->structType()) return "z_str_" + from->structType()->ident + "(" + value + ")";
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
                if (from == Type::float_type) return "zen_round_int(" + value + ")";
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
                arguments.push_back(emit_expression_ordered(call->exprs->exprs[i]));
            /* Type-conversion builtins: wrapper depends on argument type */
            if (arguments.size() == 1)
            {
                std::string argument = arguments[0];
                Type *argument_type = call->exprs->exprs[0]->sem_type;
                if (call->ident == "int")
                {
                    if (argument_type == Type::string_type) return "zen_int(" + argument + ")";
                    if (argument_type == Type::float_type) return "zen_round_int(" + argument + ")";
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
                    if (argument_type && argument_type->structType())
                        return "z_str_" + argument_type->structType()->ident + "(" + argument + ")";
                    return "zen_str(" + argument + ")";
                }
            }
            /* All other builtins: O(1) Decl* lookup, no string scanning */
            if (call->sem_decl)
            {
                std::map<Decl *, std::string>::const_iterator it =
                    builtin_wrappers_.find(call->sem_decl);
                if (it != builtin_wrappers_.end())
                {
                    note_window_use(call->sem_decl);
                    std::string result = it->second + "(";
                    for (size_t i = 0; i < arguments.size(); ++i)
                    {
                        if (i) result += ", ";
                        result += arguments[i];
                    }
                    return result + ")";
                }
            }
            /* Multi-arg string builtins with optional params */
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
            /* A user function may legally carry a command's name: semant
               binds the call to the one in the program's own scope, so the
               Decl - not the spelling - says which it is. Only a call that
               really resolved to a command with no wrapper is an error. */
            const bool is_user_function =
                call->sem_decl && user_functions_.find(call->sem_decl) != user_functions_.end();
            if (!is_user_function && generated_command(call->ident))
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
            std::string lhs = emit_expression_ordered(arithmetic->lhs);
            std::string rhs = emit_expression_ordered(arithmetic->rhs);
            const char *op = 0;
            switch (arithmetic->op)
            {
            case '+': op = "+"; break;
            case '-': op = "-"; break;
            case '*': op = "*"; break;
            case '/':
                if (arithmetic->sem_type == Type::float_type)
                    op = "/";
                else
                    return "zen_div(" + lhs + ", " + rhs + ")";
                break;
            case MOD:
                if (arithmetic->sem_type == Type::float_type)
                    return "std::fmod(" + lhs + ", " + rhs + ")";
                return "zen_mod(" + lhs + ", " + rhs + ")";
            case '^':
                return "std::pow(" + lhs + ", " + rhs + ")";
            default: throw Ex("CGen: unsupported arithmetic node");
            }
            return "(" + lhs + " " + op + " " + rhs + ")";
        }
        if (BinExprNode *binary = dynamic_cast<BinExprNode *>(expression))
        {
            std::string lhs = emit_expression_ordered(binary->lhs);
            std::string rhs = emit_expression_ordered(binary->rhs);
            const char *op = 0;
            switch (binary->op)
            {
            case AND: op = "&"; break;
            case OR: op = "|"; break;
            case XOR: op = "^"; break;
            case SHL: op = "<<"; break;
            case SHR:
                return "static_cast<int64_t>(static_cast<uint32_t>(" +
                       lhs + ") >> (" + rhs + " & 31))";
            case SAR: op = ">>"; break;
            default: throw Ex("CGen: unsupported binary node");
            }
            return "(" + lhs + " " + op + " " + rhs + ")";
        }
        if (RelExprNode *relation = dynamic_cast<RelExprNode *>(expression))
        {
            std::string lhs = emit_expression_ordered(relation->lhs);
            std::string rhs = emit_expression_ordered(relation->rhs);
            if (relation->lhs->sem_type->structType())
            {
                std::string equal = "zen_native::object_equal(" + lhs + ", " + rhs + ")";
                if (relation->op == '=') return equal;
                if (relation->op == NE) return "(!" + equal + ")";
            }
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
            return "(" + lhs + " " + op + " " + rhs + ")";
        }
        throw Ex("CGen: unsupported expression node " + std::string(typeid(*expression).name()));
    }

    void CGen::emit_statements(StmtSeqNode *statements)
    {
        if (!statements) return;
        for (size_t i = 0; i < statements->stmts.size(); ++i)
            emit_statement(statements->stmts[i]);
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
        if (DimNode *node = dynamic_cast<DimNode *>(statement))
        {
            std::string name = emit_variable_decl(node->sem_array);
            std::string dims = temporary("dims");
            line("int64_t " + dims + "[] = {");
            ++indent_;
            for (size_t i = 0; i < node->exprs->exprs.size(); ++i)
            {
                bool last = (i + 1 == node->exprs->exprs.size());
                line(emit_expression(node->exprs->exprs[i]) + " + 1" + (last ? "" : ","));
            }
            --indent_;
            line("};");
            line(name + ".dim(" + dims + ", " +
                 std::to_string(node->exprs->exprs.size()) + ");");
            return;
        }
        if (IncludeNode *node = dynamic_cast<IncludeNode *>(statement))
        {
            emit_statements(node->stmts);
            return;
        }
        if (SelectNode *select = dynamic_cast<SelectNode *>(statement))
        {
            line(emit_variable(select->sem_temp) + " = " + emit_expression(select->expr) + ";");
            for (size_t k = 0; k < select->cases.size(); ++k)
            {
                CaseNode *c = select->cases[k];
                std::string condition;
                in_condition_ = true;
                for (int j = 0; j < c->exprs->size(); ++j)
                {
                    if (j) condition += " || ";
                    condition += emit_variable(select->sem_temp) + " == " +
                                 emit_expression(c->exprs->exprs[j]);
                }
                in_condition_ = false;
                line((k == 0 ? "if (" : "} else if (") + condition + ") {");
                ++indent_;
                emit_statements(c->stmts);
                --indent_;
            }
            if (select->defStmts)
            {
                line("} else {");
                ++indent_;
                emit_statements(select->defStmts);
                --indent_;
            }
            line("}");
            return;
        }
        if (LabelNode *node = dynamic_cast<LabelNode *>(statement))
        {
            if (goto_labels_.find(node->ident) != goto_labels_.end())
                line(sanitize_label(node->ident) + ":;");
            return;
        }
        if (GotoNode *node = dynamic_cast<GotoNode *>(statement))
        {
            line("goto " + sanitize_label(node->ident) + ";");
            return;
        }
        if (GosubNode *node = dynamic_cast<GosubNode *>(statement))
        {
            int id = ++gosub_id_;
            std::string ret = "z_gosub_ret_" + std::to_string(id);
            gosub_sites_.push_back(std::make_pair(id, ret));
            line("z_gosub_stack[z_gosub_sp++] = " + std::to_string(id) + ";");
            line("goto " + sanitize_label(node->ident) + ";");
            line(ret + ":;");
            return;
        }
        if (ReadNode *node = dynamic_cast<ReadNode *>(statement))
        {
            DeclVarNode *declared = dynamic_cast<DeclVarNode *>(node->var);
            if (!declared || !declared->sem_decl)
                throw Ex("CGen: unsupported Read target");
            std::string name = emit_variable_decl(declared->sem_decl);
            if (declared->sem_decl->type == Type::float_type)
                line(name + " = z_read_float();");
            else if (declared->sem_decl->type == Type::string_type)
                line(name + " = z_read_str();");
            else
                line(name + " = z_read_int();");
            return;
        }
        if (RestoreNode *node = dynamic_cast<RestoreNode *>(statement))
        {
            line("z_data_ptr = " + std::to_string(node->sem_label ? node->sem_label->data_sz : 0) + ";");
            return;
        }
        if (ForEachNode *loop = dynamic_cast<ForEachNode *>(statement))
        {
            std::string variable = emit_variable(loop->var);
            line("for (" + variable + " = z_list_" + loop->typeIdent + "->first(); " +
                 variable + "; " + variable + " = z_list_" + loop->typeIdent +
                 "->after(" + variable + ")) {");
            ++indent_;
            emit_statements(loop->stmts);
            --indent_;
            line("}");
            return;
        }
        if (DeleteNode *node = dynamic_cast<DeleteNode *>(statement))
        {
            StructType *type = node->expr->sem_type->structType();
            line("z_list_" + type->ident + "->erase(" + emit_expression(node->expr) + ");");
            return;
        }
        if (DeleteEachNode *node = dynamic_cast<DeleteEachNode *>(statement))
        {
            line("z_list_" + node->typeIdent + "->clear();");
            return;
        }
        if (InsertNode *node = dynamic_cast<InsertNode *>(statement))
        {
            StructType *type = node->expr1->sem_type->structType();
            line("z_list_" + type->ident + "->insert_" +
                 std::string(node->before ? "before" : "after") + "(" +
                 emit_expression(node->expr1) + ", " + emit_expression(node->expr2) + ");");
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
            /* All builtins via O(1) Decl* lookup, no string scanning */
            if (call && call->sem_decl)
            {
                std::map<Decl *, std::string>::const_iterator it =
                    builtin_wrappers_.find(call->sem_decl);
                if (it != builtin_wrappers_.end())
                {
                    note_window_use(call->sem_decl);
                    std::string result = it->second + "(";
                    if (call->exprs)
                    {
                        for (size_t i = 0; i < call->exprs->exprs.size(); ++i)
                        {
                            if (i) result += ", ";
                            result += emit_expression(call->exprs->exprs[i]);
                        }
                    }
                    line(result + ");");
                    return;
                }
            }
            /* User function calls */
            if (call && call->sem_decl && call->sem_decl->type &&
                call->sem_decl->type->funcType() &&
                !call->sem_decl->type->funcType()->cfunc)
            {
                line(emit_expression(call) + ";");
                return;
            }
            if (call && generated_command(call->ident))
                throw Ex("CGen: registered command has no native wrapper " + call->ident);
            throw Ex("CGen: unsupported call statement " + call->ident);
        }
        if (ReturnNode *result = dynamic_cast<ReturnNode *>(statement))
        {
            if (result->sem_level <= 0)
            {
                line("goto z_gosub_dispatch;");
                return;
            }
            if (result->expr)
                line("return " + emit_expression(result->expr) + ";");
            else
                line("return;");
            return;
        }
        if (IfNode *conditional = dynamic_cast<IfNode *>(statement))
        {
            in_condition_ = true;
            std::string cond = emit_expression(conditional->expr);
            in_condition_ = false;
            line("if (" + cond + ") {");
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
        if (dynamic_cast<ExitNode *>(statement))
        {
            line("break;");
            return;
        }
        if (RepeatNode *loop = dynamic_cast<RepeatNode *>(statement))
        {
            line("do {");
            ++indent_;
            emit_statements(loop->stmts);
            --indent_;
            if (loop->expr)
            {
                in_condition_ = true;
                std::string cond = emit_expression(loop->expr);
                in_condition_ = false;
                line("} while (!(" + cond + "));");
            }
            else
                line("} while (true);");
            return;
        }
        if (WhileNode *loop = dynamic_cast<WhileNode *>(statement))
        {
            in_condition_ = true;
            std::string cond = emit_expression(loop->expr);
            in_condition_ = false;
            line("while (" + cond + ") {");
            ++indent_;
            emit_statements(loop->stmts);
            --indent_;
            line("}");
            return;
        }
        if (ForNode *loop = dynamic_cast<ForNode *>(statement))
        {
            std::string variable = emit_variable(loop->var);
            std::string step = emit_expression(loop->stepExpr);
            std::string to_var = temporary("to");
            line(variable + " = " + emit_expression(loop->fromExpr) + ";");
            line("for (;;) {");
            ++indent_;
            line(emit_type(loop->var->sem_type) + " " + to_var + " = " +
                 emit_expression(loop->toExpr) + ";");
            line("if (" + step + " >= 0 ? " + variable + " > " + to_var +
                 " : " + variable + " < " + to_var + ") break;");
            emit_statements(loop->stmts);
            line(variable + " += " + step + ";");
            --indent_;
            line("}");
            return;
        }
        throw Ex("CGen: unsupported statement node " + std::string(typeid(*statement).name()));
    }
    void CGen::emit_function_prototype(FuncDeclNode *function)
    {
        if (function && function->sem_decl) user_functions_.insert(function->sem_decl);
        if (!function || !function->sem_type)
            throw Ex("CGen: malformed function declaration");
        out_ << "static " << emit_type(function->sem_type->returnType) << " z_"
             << function->ident << "(";
        for (size_t i = 0; i < function->sem_type->params->decls.size(); ++i)
        {
            Decl *decl = function->sem_type->params->decls[i];
            if (i) out_ << ", ";
            out_ << emit_type(decl->type) << " z_" << decl->name;
        }
        out_ << ");\n";
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
            std::string initial = (decl->type == Type::string_type || decl->type->vectorType()) ? "{}" : "0";
            line(emit_type(decl->type) + " " + names_[decl] + " = " + initial + ";");
        }
        std::set<std::string> function_labels;
        collect_goto_labels(function->stmts, function_labels);
        for (std::set<std::string>::const_iterator it = function_labels.begin(); it != function_labels.end(); ++it)
            if (it->compare(0, 3, "_20") == 0) line(sanitize_label(*it) + ":;");
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
        /* The whole program goes to a buffer so the includes can be
           written once it is known which ones it needs (note_window_use). */
        std::ostringstream body;
        std::streambuf *const final_buf = out_.rdbuf(body.rdbuf());
        uses_window_ = false;
        global_names_.clear();
        names_.clear();
        goto_labels_.clear();
        if (program) collect_goto_labels(program->stmts, goto_labels_);
        out_ << "static zen_native::Heap z_heap;\n\n";
        emit_data(program);
        if (program && program->structs)
        {
            for (size_t i = 0; i < program->structs->decls.size(); ++i)
            {
                StructDeclNode *structure = dynamic_cast<StructDeclNode *>(program->structs->decls[i]);
                if (!structure) throw Ex("CGen: malformed Type declaration");
                out_ << "struct z_type_" << structure->ident << ";\n";
            }
            out_ << "\n";
            for (size_t i = 0; i < program->structs->decls.size(); ++i)
                emit_struct(dynamic_cast<StructDeclNode *>(program->structs->decls[i]));
            for (size_t i = 0; i < program->structs->decls.size(); ++i)
            {
                StructDeclNode *structure = dynamic_cast<StructDeclNode *>(program->structs->decls[i]);
                if (structure) out_ << "static std::string z_str_" << structure->ident
                                    << "(z_type_" << structure->ident << " *);\n";
            }
            out_ << "\n";
            for (size_t i = 0; i < program->structs->decls.size(); ++i)
                emit_struct_string(dynamic_cast<StructDeclNode *>(program->structs->decls[i]));
        }
        if (program && program->sem_env && program->sem_env->decls)
        {
            for (size_t i = 0; i < program->sem_env->decls->decls.size(); ++i)
            {
                Decl *decl = program->sem_env->decls->decls[i];
                if (!decl) continue;
                bool is_type_name = false;
                if (program->structs)
                    for (size_t k = 0; k < program->structs->decls.size(); ++k)
                    {
                        StructDeclNode *structure = dynamic_cast<StructDeclNode *>(program->structs->decls[k]);
                        if (structure && (structure->ident == decl->name ||
                                          decl->name == "type_" + structure->ident))
                        {
                            is_type_name = true;
                            break;
                        }
                    }
                if (is_type_name) continue;
                global_names_[decl] = "z_" + decl->name;
            }
            names_ = global_names_;
            for (std::map<Decl *, std::string>::const_iterator it = global_names_.begin();
                 it != global_names_.end(); ++it)
            {
                std::string type = emit_type(it->first->type);
                std::string initial;
                if (type == "std::string" || it->first->type->arrayType() ||
                    it->first->type->vectorType())
                    initial = "{}";
                else
                    initial = "0";
                out_ << "static " << type << " " << it->second << " = " << initial << ";\n";
            }
            out_ << "\n";
        }
        if (program && program->funcs)
        {
            for (size_t i = 0; i < program->funcs->decls.size(); ++i)
                emit_function_prototype(dynamic_cast<FuncDeclNode *>(program->funcs->decls[i]));
            out_ << "\n";
            for (size_t i = 0; i < program->funcs->decls.size(); ++i)
                emit_function(dynamic_cast<FuncDeclNode *>(program->funcs->decls[i]));
        }
        out_ << "int main()\n";
        out_ << "{\n";
        indent_ = 1;
        if (program && program->structs)
            for (size_t i = 0; i < program->structs->decls.size(); ++i)
            {
                StructDeclNode *structure = dynamic_cast<StructDeclNode *>(program->structs->decls[i]);
                line("z_list_" + structure->ident + " = new zen_native::TypeList<z_type_" +
                     structure->ident + ">(z_heap);");
            }
        line("z_data = z_data_values;");
        names_ = global_names_;
        for (std::map<Decl *, std::string>::const_iterator it = global_names_.begin(); it != global_names_.end(); ++it)
        {
            line("(void)" + it->second + ";");
        }
        emit_statements(program ? program->stmts : 0);
        if (!gosub_sites_.empty())
        {
            /* The dispatch block is the Return of every Gosub: it pops the
               site the call came from and jumps back there. Reaching it by
               falling off the end of the program instead would pop an empty
               stack, so the program jumps over it to its own end. */
            line("goto z_program_end;");
            line("z_gosub_dispatch:");
            line("switch (z_gosub_stack[--z_gosub_sp]) {");
            for (size_t i = 0; i < gosub_sites_.size(); ++i)
                line("case " + std::to_string(gosub_sites_[i].first) + ": goto " +
                     gosub_sites_[i].second + ";");
            line("}");
            line("z_program_end:;");
        }
        out_ << "    return 0;\n";
        out_ << "}\n";

        out_.rdbuf(final_buf);
        out_ << "#include \"zen_codegen_support.hpp\"\n\n";
        out_ << "#include \"zen_native_runtime.hpp\"\n\n";
        out_ << "#include \"zenblitzsdk.h\"\n\n";
        if (uses_window_)
            out_ << "#include \"zen_engine_sdk.h\"\n\n";
        out_ << body.str();
    }
}