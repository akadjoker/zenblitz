/*
** compiler.cpp — Compiler entry point: Blitz Basic source -> Zen bytecode.
** The heavy lifting lives in bb_toker/bb_parser/bb_semant/bb_codegen.
*/
#include "compiler.h"
#include "vm.h"
#include "bb_parser.h"
#include "bb_codegen.h"
#include "bb_runtime.h"
#include <sstream>
#include <cstdio>

namespace zen
{
    void install_runtime(VM *vm)
    {
        bb::bb_runtime_for(vm);
    }

    Compiler::Compiler() : debug_(false) { error_[0] = '\0'; }
    Compiler::~Compiler() {}

    ObjFunc *Compiler::compile(GC *gc, VM *vm, const char *source, const char *filename)
    {
        (void)gc;
        error_[0] = '\0';
        bb::BBRuntime *rt = bb::bb_runtime_for(vm);

        std::istringstream in(source);
        bb::ProgNode *prog = 0;
        ObjFunc *fn = 0;
        try
        {
            bb::Toker toker(in);
            bb::Parser parser(toker);
            prog = parser.parse(filename);
            prog->semant(rt->env);
            bb::BBGen gen(vm, rt, debug_);
            fn = gen.compile(prog, filename);
        }
        catch (bb::Ex &x)
        {
            const char *file = x.file.size() ? x.file.c_str() : filename;
            if (x.pos >= 0)
                snprintf(error_, sizeof(error_), "%s:%d:%d: error: %s", file, (x.pos >> 16) + 1, (x.pos & 0xffff) + 1, x.ex.c_str());
            else
                snprintf(error_, sizeof(error_), "%s: error: %s", file, x.ex.c_str());
            fprintf(stderr, "%s\n", error_);
            delete prog;
            return nullptr;
        }
        delete prog;
        return fn;
    }
}
