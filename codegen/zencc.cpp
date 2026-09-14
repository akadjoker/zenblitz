/*
** zencc.cpp - native C++ source emission entry point.
*/
#include "bb_parser.h"
#include "bb_runtime.h"
#include "c_codegen.h"
#include "runtime.h"
#include "vm.h"

#include <cstdio>
#include <fstream>
#include <sstream>

using namespace zen;

static void install_window_declarations(bb::BBRuntime *runtime)
{
    bb::DeclSeq *decls = runtime->env->funcDecls;
    bb::DeclSeq *graphics_params = new bb::DeclSeq();
    graphics_params->insertDecl("width", bb::Type::int_type, bb::DECL_PARAM);
    graphics_params->insertDecl("height", bb::Type::int_type, bb::DECL_PARAM);
    graphics_params->insertDecl("depth", bb::Type::int_type, bb::DECL_PARAM,
                                new bb::ConstType(0LL));
    graphics_params->insertDecl("mode", bb::Type::int_type, bb::DECL_PARAM,
                                new bb::ConstType(0LL));
    decls->insertDecl("graphics", new bb::FuncType(bb::Type::void_type,
                                                     graphics_params, false, false),
                      bb::DECL_FUNC);

    bb::DeclSeq *flip_params = new bb::DeclSeq();
    flip_params->insertDecl("vwait", bb::Type::int_type, bb::DECL_PARAM,
                            new bb::ConstType(1LL));
    decls->insertDecl("flip", new bb::FuncType(bb::Type::void_type,
                                                 flip_params, false, false),
                      bb::DECL_FUNC);
    decls->insertDecl("endgraphics", new bb::FuncType(bb::Type::void_type,
                                                        new bb::DeclSeq(), false, false),
                      bb::DECL_FUNC);
}

static bool read_source(const char *path, std::string &source)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        std::fprintf(stderr, "zencc: cannot open '%s'\n", path);
        return false;
    }
    std::ostringstream contents;
    contents << in.rdbuf();
    source = contents.str();
    return true;
}

int main(int argc, char **argv)
{
    if (argc != 4 || std::string(argv[1]) != "--emit-c")
    {
        std::fprintf(stderr, "usage: zencc --emit-c input.bb output.cpp\n");
        return 1;
    }

    std::string source;
    if (!read_source(argv[2], source))
        return 1;

    std::ofstream output(argv[3], std::ios::binary);
    if (!output)
    {
        std::fprintf(stderr, "zencc: cannot write '%s'\n", argv[3]);
        return 1;
    }

    VM vm;
    install_runtime(&vm);
    bb::BBRuntime *runtime = bb::bb_runtime_for(&vm);
    install_window_declarations(runtime);
    std::istringstream input(source);
    bb::Toker toker(input);
    bb::Parser parser(toker, &vm);
    bb::ProgNode *program = nullptr;

    try
    {
        program = parser.parse(argv[2]);
        program->semant(runtime->env);
        bb::CGen generator(output, argv[2]);
        generator.compile(program);
    }
    catch (bb::Ex &error)
    {
        std::fprintf(stderr, "zencc: %s\n", error.ex.c_str());
        delete program;
        return 1;
    }

    delete program;
    return 0;
}