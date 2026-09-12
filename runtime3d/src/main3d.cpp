/*
** main3d.cpp — zenblitz3d entry point: compiles and runs a .bb file.
*/
#include "vm.h"
#include "compiler.h"
#include "runtime.h"
#include "runtime3d.h"
#include "backend_stdio.h" /* Print/DebugLog/files: same console backend as zenblitz */
#include <cstdio>
#include <cstdlib>

using namespace zen;

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "zenblitz3d: cannot open '%s'\n", path); return nullptr; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(size + 1);
    size_t n = fread(buf, 1, size, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: zenblitz3d program.bb\n");
        return 1;
    }

    char *source = read_file(argv[1]);
    if (!source) return 1;

    VM vm;
    vm.set_backend(stdio_backend());
    install_runtime(&vm);
    bb3d::install_graphics_commands(&vm);

    Compiler compiler;
    ObjFunc *fn = compiler.compile(&vm.get_gc(), &vm, source, argv[1]);
    free(source);
    if (!fn)
    {
        fprintf(stderr, "zenblitz3d: compilation failed.\n");
        return 1;
    }

    vm.run(fn);
    while (vm.suspended())
        if (!vm.resume()) break;

    int rc = vm.had_error() ? 1 : 0;
    bb3d::shutdown_graphics(&vm);
    return rc;
}
