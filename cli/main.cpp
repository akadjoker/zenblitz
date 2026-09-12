/*
** main.cpp — CLI entry point for zenblitz (Blitz Basic on the Zen VM).
** Usage:
**   ./zenblitz              → REPL (interactive)
**   ./zenblitz file.bb     → compile and run file
**   ./zenblitz -e "code"    → compile and run inline code
*/

#include "vm.h"
#include "compiler.h"
#include "module.h"
#include "memory.h"
#include "debug.h"
#include "bytecode.h"
/* Native modules (graphics, audio, sqlite, dnn, ...) live in the separate
   zenvm-modules repo. It builds its own `zen` from this very file rather
   than keeping a second copy: compile with
       -DZEN_CLI_EXTRA_LIBS_HEADER='"zen_modules_libs.h"'
   and that header must declare
       void zen_cli_register_extra_libs(zen::VM &vm);
   which register_default_libs() calls after the core libs. Plain builds of
   this repo never define the macro and get the core-only CLI. */
#ifdef ZEN_CLI_EXTRA_LIBS_HEADER
#include ZEN_CLI_EXTRA_LIBS_HEADER
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
static const char *g_search_paths[16];
static int g_num_search_paths = 0;

static void register_default_libs(VM &vm)
{
    vm.open_lib_globals(&zen_lib_base); /* always available */
    vm.register_lib(&zen_lib_math);
    vm.register_lib(&zen_lib_os);
    vm.register_lib(&zen_lib_time);
    vm.register_lib(&zen_lib_fs);
    vm.register_lib(&zen_lib_path);
    vm.register_lib(&zen_lib_file);
#ifdef ZEN_ENABLE_REGEX
    vm.register_lib(&zen_lib_re);
#endif
#ifdef ZEN_ENABLE_ZIP
    vm.register_lib(&zen_lib_zip);
#endif
#ifdef ZEN_ENABLE_NET
    vm.register_lib(&zen_lib_net);
#endif
#ifdef ZEN_ENABLE_HTTP
    vm.register_lib(&zen_lib_http);
#endif
#ifdef ZEN_ENABLE_CRYPTO
    vm.register_lib(&zen_lib_crypto);
#endif
#ifdef ZEN_ENABLE_JSON
    vm.register_lib(&zen_lib_json);
#endif
#ifdef ZEN_ENABLE_UTF8
    vm.register_lib(&zen_lib_utf8);
#endif
    vm.register_lib(&zen_lib_easing);
    vm.register_lib(&zen_lib_base64);
    vm.register_lib(&zen_lib_csv);
    vm.register_lib(&zen_lib_xml);
    vm.register_lib(&zen_lib_ini);
    vm.register_lib(&zen_lib_log);
#ifdef ZEN_CLI_EXTRA_LIBS_HEADER
    zen_cli_register_extra_libs(vm); /* zenvm-modules: image, sdl2, sqlite, ... */
#endif
    for (int i = 0; i < g_num_search_paths; i++)
        vm.add_search_path(g_search_paths[i]);
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

static int run_source(const char *source, const char *filename,
                      int script_argc = 0, char **script_argv = nullptr)
{
    VM vm;
    register_default_libs(vm);
    install_args(vm, script_argc, script_argv);

    Compiler compiler;
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

    vm.run(fn);
    return vm.had_error() ? 1 : 0;
}

static int run_bytecode(const uint8_t *data, size_t size, const char *filename,
                        int script_argc = 0, char **script_argv = nullptr)
{
    VM vm;
    register_default_libs(vm);
    install_args(vm, script_argc, script_argv);

    char err[256] = {0};
    ObjFunc *fn = load_bytecode_buffer(&vm, data, size, err, sizeof(err));
    if (!fn)
    {
        fprintf(stderr, "zenblitz: bytecode load failed: %s\n", err[0] ? err : "unknown error");
        return 1;
    }

    if (g_disassemble)
    {
        disassemble_func(fn, filename);
        disassemble_nested(fn);
        printf("--- execution ---\n");
    }

    if (g_dis_only)
        return 0;

    vm.run(fn);
    return vm.had_error() ? 1 : 0;
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
            vm.run(fn);
        }
    }
}

/* =========================================================
** Main
** ========================================================= */

int main(int argc, char **argv)
{
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
