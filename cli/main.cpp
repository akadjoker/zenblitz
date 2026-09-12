/*
** main.cpp — CLI entry point for zenblitz (Blitz Basic on the Zen VM).
** Usage:
**   ./zenblitz              → REPL (interactive)
**   ./zenblitz file.bb     → compile and run file
**   ./zenblitz -e "code"    → compile and run inline code
**   ./zenblitz --dump out.zbc file.bb   → compile to bytecode
**   ./zenblitz --build game file.bb     → standalone executable (zenblitz-rt + bytecode)
**   ./zenblitz --build game.exe --stub zenblitz-rt.exe file.bb   → with another runtime
**   ./zenblitz --debug file.bb          → extra runtime checks (array bounds per dimension)
*/

#include "vm.h"
#include "compiler.h"
#include "memory.h"
#include "debug.h"
#include "bytecode.h"
#include "embedded.h"
#include "backend_stdio.h"
#include "host_loop.h"
#include "userlibs.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if !defined(_WIN32)
#include <sys/stat.h>
#endif

using namespace zen;

/* =========================================================
** Read file into malloc'd buffer
** ========================================================= */

static char *read_file(const char *path, long *out_size = nullptr)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        fprintf(stderr, "zenblitz: cannot open '%s'\n", path);
        return nullptr;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)malloc(size + 1);
    if (!buf)
    {
        fclose(f);
        fprintf(stderr, "zenblitz: out of memory\n");
        return nullptr;
    }

    size_t read = fread(buf, 1, size, f);
    buf[read] = '\0';
    fclose(f);
    if (out_size)
        *out_size = (long)read;
    return buf;
}

/* =========================================================
** Run source code (compile + execute)
** ========================================================= */

static bool g_disassemble = false;
static bool g_dis_only = false;
static bool g_verbose = false;
static const char *g_dump_path = nullptr;
static bool g_strip_debug = false;
static bool g_debug_checks = false;
static const char *g_build_path = nullptr;
static const char *g_stub_path = nullptr;
static const char *g_search_paths[16];
static int g_num_search_paths = 0;

static void register_default_libs(VM &vm)
{
    /* Blitz commands and runtime helpers become VM globals here, so both
       source compiles and precompiled bytecode see them in the same slots.
       Userlibs are part of that command set, so they must be installed in
       the same order in both paths — hence here, not at each call site. */
    vm.set_backend(stdio_backend());
    install_runtime(&vm);
    install_userlibs_beside_exe(vm, "zenblitz");
}

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

static void disassemble_nested(ObjFunc *fn)
{
    for (int i = 0; i < fn->const_count; i++)
    {
        Value v = fn->constants[i];
        if (v.type == VAL_OBJ && v.as.obj && v.as.obj->type == OBJ_FUNC)
        {
            ObjFunc *nested = (ObjFunc *)v.as.obj;
            const char *n = nested->name ? nested->name->chars : "<anon>";
            disassemble_func(nested, n);
            disassemble_nested(nested);
        }
    }
}

/* =========================================================
** Standalone executables: runtime binary + bytecode + trailer (embedded.h)
** ========================================================= */

/* The stub `--build` uses when none is given: zenblitz-rt (the runtime-only
   executable) next to this one, or this executable itself if it is not
   there — which works too, the program just carries the compiler along. */
static const char *default_stub(char *buf, size_t size)
{
#if defined(_WIN32)
    const char *rt = "zenblitz-rt.exe";
#else
    const char *rt = "zenblitz-rt";
#endif
    if (path_beside_exe(rt, buf, size))
    {
        FILE *f = fopen(buf, "rb");
        if (f)
        {
            fclose(f);
            return buf;
        }
    }
    /* No runtime beside us: fall back to this executable, which works too —
       the program just carries the compiler along. */
    return self_path(buf, size);
}

static int build_executable(const char *bytecode_path, const char *out_path)
{
    char stub[4096];
    const char *base = g_stub_path;
    if (!base)
    {
        base = default_stub(stub, sizeof(stub));
        if (!base)
        {
            fprintf(stderr, "zenblitz: cannot locate the runtime executable (use --stub)\n");
            return 1;
        }
    }
    long rt_size = 0, bc_size = 0;
    char *rt = read_file(base, &rt_size);
    if (!rt) return 1;
    char *bc = read_file(bytecode_path, &bc_size);
    if (!bc) { free(rt); return 1; }
    /* a runtime that itself carries a program: keep only the runtime part */
    long base_size = rt_size;
    if (rt_size > 16 && memcmp(rt + rt_size - 8, EXE_MAGIC, 8) == 0)
    {
        uint64_t emb = 0;
        memcpy(&emb, rt + rt_size - 16, 8);
        base_size = rt_size - 16 - (long)emb;
    }
    FILE *out = fopen(out_path, "wb");
    if (!out)
    {
        fprintf(stderr, "zenblitz: cannot write '%s'\n", out_path);
        free(rt); free(bc);
        return 1;
    }
    uint64_t sz = (uint64_t)bc_size;
    bool ok = fwrite(rt, 1, (size_t)base_size, out) == (size_t)base_size &&
              fwrite(bc, 1, (size_t)bc_size, out) == (size_t)bc_size &&
              fwrite(&sz, 1, 8, out) == 8 &&
              fwrite(EXE_MAGIC, 1, 8, out) == 8;
    fclose(out);
    free(rt); free(bc);
    if (!ok)
    {
        fprintf(stderr, "zenblitz: write error on '%s'\n", out_path);
        return 1;
    }
#ifndef _WIN32
    chmod(out_path, 0755);
#endif
    printf("zenblitz: built '%s' (%ld bytes of bytecode, runtime '%s')\n", out_path, bc_size, base);
    return 0;
}

static int run_source(const char *source, const char *filename,
                      int script_argc = 0, char **script_argv = nullptr)
{
    VM vm;
    register_default_libs(vm);
    install_args(vm, script_argc, script_argv);

    Compiler compiler;
    compiler.set_debug(g_debug_checks);
    ObjFunc *fn = compiler.compile(&vm.get_gc(), &vm, source, filename);

    if (!fn)
    {
        fprintf(stderr, "zenblitz: compilation failed.\n");
        return 1;
    }

    if (g_disassemble)
    {
        disassemble_func(fn, filename);
        disassemble_nested(fn);
        printf("--- execution ---\n");
    }

    if (g_build_path)
    {
        char err[256] = {0};
        const char *tmp = "/tmp/zenblitz_build.zbc";
        if (!dump_bytecode_file(&vm, fn, tmp, true, nullptr, err, sizeof(err)))
        {
            fprintf(stderr, "zenblitz: build failed: %s\n", err[0] ? err : "unknown error");
            return 1;
        }
        int rc = build_executable(tmp, g_build_path);
        remove(tmp);
        return rc;
    }
    if (g_dump_path)
    {
        char err[256] = {0};
        BytecodeStats stats;
        if (!dump_bytecode_file(&vm, fn, g_dump_path, g_strip_debug,
                                g_verbose ? &stats : nullptr, err, sizeof(err)))
        {
            fprintf(stderr, "zenblitz: bytecode dump failed: %s\n", err[0] ? err : "unknown error");
            return 1;
        }
        if (g_verbose)
        {
            printf("zenblitz: dumped bytecode '%s'\n", g_dump_path);
            printf("  bytes:        %zu\n", stats.bytes);
            printf("  globals:      %u\n", stats.globals);
            printf("  selectors:    %u\n", stats.selectors);
            printf("  functions:    %u\n", stats.functions);
            printf("  processes:    %u\n", stats.processes);
            printf("  classes:      %u\n", stats.classes);
            printf("  closures:     %u\n", stats.closures);
            printf("  strings:      %u\n", stats.strings);
            printf("  constants:    %u\n", stats.constants);
            printf("  instructions: %u\n", stats.instructions);
        }
        if (g_dis_only)
            return 0;
        return 0;
    }

    if (g_dis_only) return 0;

    return run_until_done(vm, fn);
}

static int run_bytecode(const uint8_t *data, size_t size, const char *filename,
                        int script_argc = 0, char **script_argv = nullptr)
{
    VM vm;
    register_default_libs(vm);

    char err[256] = {0};
    ObjFunc *fn = load_bytecode_buffer(&vm, data, size, err, sizeof(err));
    if (!fn)
    {
        fprintf(stderr, "zenblitz: bytecode load failed: %s\n", err[0] ? err : "unknown error");
        return 1;
    }
    /* after the load: the file carries the (empty) args array it was compiled with */
    install_args(vm, script_argc, script_argv);

    if (g_disassemble)
    {
        disassemble_func(fn, filename);
        disassemble_nested(fn);
        printf("--- execution ---\n");
    }

    if (g_dis_only)
        return 0;

    return run_until_done(vm, fn);
}

/* =========================================================
** REPL
** ========================================================= */

static void repl()
{
    printf("zenblitz %s  [type 'exit' to quit]\n", "0.1.0");

    VM vm;
    register_default_libs(vm);
    Compiler compiler;

    char line[4096];
    for (;;)
    {
        printf(">> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin))
        {
            printf("\n");
            break;
        }

        /* Strip newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = '\0';

        /* Exit command */
        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0)
            break;

        /* Skip empty lines */
        if (line[0] == '\0')
            continue;

        ObjFunc *fn = compiler.compile(&vm.get_gc(), &vm, line, "<repl>");
        if (fn)
        {
            run_until_done(vm, fn);
        }
    }
}

/* =========================================================
** Main
** ========================================================= */

int main(int argc, char **argv)
{
    {
        long emb_size = 0;
        char *emb = embedded_program(&emb_size);
        if (emb)
        {
            int rc = run_bytecode((const uint8_t *)emb, (size_t)emb_size, argv[0], argc - 1, argv + 1);
            free(emb);
            return rc;
        }
    }
    if (argc == 1)
    {
        /* No arguments: REPL */
        repl();
        return 0;
    }

    /* Parse flags */
    const char *source_code = nullptr;
    const char *file_path = nullptr;
    int script_arg_start = 0; /* index in argv where script args begin */

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--dis") == 0 || strcmp(argv[i], "-d") == 0)
        {
            g_disassemble = true;
        }
        else if (strcmp(argv[i], "--dis-only") == 0)
        {
            g_disassemble = true;
            g_dis_only = true;
        }
        else if (strcmp(argv[i], "--dump") == 0 && i + 1 < argc)
        {
            g_dump_path = argv[++i];
        }
        else if (strcmp(argv[i], "--strip-debug") == 0)
        {
            g_strip_debug = true;
        }
        else if (strcmp(argv[i], "--debug") == 0)
        {
            g_debug_checks = true;
        }
        else if (strcmp(argv[i], "--build") == 0 && i + 1 < argc)
        {
            g_build_path = argv[++i];
        }
        else if (strcmp(argv[i], "--stub") == 0 && i + 1 < argc)
        {
            g_stub_path = argv[++i];
        }
        else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0)
        {
            g_verbose = true;
        }
        else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc)
        {
            source_code = argv[++i];
        }
        else if ((strcmp(argv[i], "-I") == 0 || strcmp(argv[i], "--include") == 0) && i + 1 < argc)
        {
            if (g_num_search_paths < 16)
                g_search_paths[g_num_search_paths++] = argv[++i];
            else
                i++; /* skip silently */
        }
        else if (argv[i][0] != '-')
        {
            file_path = argv[i];
            script_arg_start = i + 1; /* everything after is script args */
            break;
        }
        else
        {
            fprintf(stderr, "zenblitz: unknown option '%s'\n", argv[i]);
            return 1;
        }
    }

    if (source_code)
    {
        return run_source(source_code, "<cmdline>");
    }

    if (file_path)
    {
        long size = 0;
        char *data = read_file(file_path, &size);
        if (!data)
            return 1;
        int sa = script_arg_start;
        int result = is_bytecode_buffer((const uint8_t *)data, (size_t)size)
                         ? run_bytecode((const uint8_t *)data, (size_t)size, file_path, argc - sa, argv + sa)
                         : run_source(data, file_path, argc - sa, argv + sa);
        free(data);
        return result;
    }

    fprintf(stderr, "zenblitz: no input specified\n");
    return 1;
}
