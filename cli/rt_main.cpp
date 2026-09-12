/*
** rt_main.cpp — zenblitz-rt, the runtime-only executable (no compiler).
**
** It is the default stub for `zenblitz --build`: the program is appended to
** a copy of this file (see embedded.h) and runs when that copy starts.
** Invoked directly it runs a precompiled bytecode file:
**   ./zenblitz-rt game.zbc arg1 arg2      → CommandLine$() = "arg1 arg2"
**
** Runtimes for other platforms and graphics back-ends are this same program
** linked with their libraries; the compiler picks one with --stub.
*/

#include "vm.h"
#include "runtime.h"
#include "memory.h"
#include "bytecode.h"
#include "embedded.h"
#include "backend_stdio.h"
#include "host_loop.h"
#include "userlibs.h"

using namespace zen;

static void install_args(VM &vm, int script_argc, char **script_argv)
{
    ObjArray *args_arr = new_array(&vm.get_gc());
    for (int i = 0; i < script_argc; i++)
    {
        ObjString *s = vm.make_string(script_argv[i]);
        array_push(&vm.get_gc(), args_arr, val_obj((Obj *)s));
    }
    vm.def_global("args", val_obj((Obj *)args_arr));
}

static int run_bytecode(const uint8_t *data, size_t size, int script_argc, char **script_argv)
{
    VM vm;
    vm.set_backend(stdio_backend());
    install_runtime(&vm);
    install_userlibs_beside_exe(vm, "zenblitz-rt");

    char err[256] = {0};
    ObjFunc *fn = load_bytecode_buffer(&vm, data, size, err, sizeof(err));
    if (!fn)
    {
        fprintf(stderr, "zenblitz-rt: bytecode load failed: %s\n", err[0] ? err : "unknown error");
        return 1;
    }
    /* after the load: the file carries the (empty) args array it was compiled with */
    install_args(vm, script_argc, script_argv);

    return run_until_done(vm, fn);
}

static char *read_file(const char *path, long *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        fprintf(stderr, "zenblitz-rt: cannot open '%s'\n", path);
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(size + 1);
    if (!buf)
    {
        fclose(f);
        fprintf(stderr, "zenblitz-rt: out of memory\n");
        return nullptr;
    }
    size_t read = fread(buf, 1, size, f);
    buf[read] = '\0';
    fclose(f);
    *out_size = (long)read;
    return buf;
}

int main(int argc, char **argv)
{
    long size = 0;
    char *data = embedded_program(&size);
    if (data)
    {
        int rc = run_bytecode((const uint8_t *)data, (size_t)size, argc - 1, argv + 1);
        free(data);
        return rc;
    }

    if (argc < 2)
    {
        fprintf(stderr, "usage: zenblitz-rt program.zbc [args...]\n"
                        "       (compile with: zenblitz --dump program.zbc program.bb)\n");
        return 1;
    }
    data = read_file(argv[1], &size);
    if (!data)
        return 1;
    if (!is_bytecode_buffer((const uint8_t *)data, (size_t)size))
    {
        fprintf(stderr, "zenblitz-rt: '%s' is not a bytecode file (this runtime has no compiler; "
                        "use zenblitz --dump)\n", argv[1]);
        free(data);
        return 1;
    }
    int rc = run_bytecode((const uint8_t *)data, (size_t)size, argc - 2, argv + 2);
    free(data);
    return rc;
}
