/*
** main3d.cpp — zenblitz3d entry point: compiles and runs a .bb file.
*/
#include "vm.h"
#include "compiler.h"
#include "runtime.h"
#include "runtime3d.h"
#include "backend_stdio.h" /* Print/DebugLog/files: same console backend as zenblitz */

/* bb_cmds.cpp, via bb_runtime.h - declared here rather than pulling in a
   libzen-internal header just to free the timers a script left behind. */
namespace bb { void free_all_timers(); }
static void bb_free_all_timers() { bb::free_all_timers(); }
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(_WIN32)
#include <direct.h>
#define zen_chdir _chdir
#else
#include <unistd.h>
#define zen_chdir chdir
#endif

using namespace zen;

static void chdir_to_program(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *last = slash > backslash ? slash : backslash;
    if (!last) return;

    const size_t length = (size_t)(last - path) + 1;
    char *directory = (char *)malloc(length + 1);
    if (!directory) return;
    memcpy(directory, path, length);
    directory[length] = '\0';

    if (zen_chdir(directory) != 0)
        fprintf(stderr, "zenblitz3d: could not enter '%s' - relative media paths may not resolve\n",
                directory);
    free(directory);
}

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

    chdir_to_program(argv[1]);

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
    {
        if (bb3d::graphics_window_closed(&vm)) break;
        if (!vm.resume()) break;
    }

    int rc = vm.had_error() ? 1 : 0;
    bb3d::shutdown_graphics(&vm);
    bb_free_all_timers();
    return rc;
}
