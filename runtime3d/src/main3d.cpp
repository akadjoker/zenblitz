/*
** main3d.cpp — zenblitz3d entry point: compiles and runs a .bb file.
*/
#include "vm.h"
#include "compiler.h"
#include "runtime.h"
#include "runtime3d.h"
#include "backend_stdio.h" /* Print/DebugLog/files: same console backend as zenblitz */
#include <SDL2/SDL_log.h>
#include <SDL2/SDL_rwops.h>
#include <cstdlib>
#include <cstring>
#include <limits>
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
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "zenblitz3d: could not enter '%s' - relative media paths may not resolve",
                 directory);
    free(directory);
}

static char *read_file(const char *path)
{
    SDL_RWops *f = SDL_RWFromFile(path, "rb");
    if (!f)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "zenblitz3d: cannot open '%s'", path);
        return nullptr;
    }
    Sint64 size = SDL_RWsize(f);
    if (size < 0 || static_cast<Uint64>(size) >=
                        static_cast<Uint64>(std::numeric_limits<size_t>::max()))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "zenblitz3d: cannot determine size of '%s'", path);
        SDL_RWclose(f);
        return nullptr;
    }
    if (SDL_RWseek(f, 0, RW_SEEK_SET) < 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "zenblitz3d: cannot seek '%s'", path);
        SDL_RWclose(f);
        return nullptr;
    }

    const size_t length = static_cast<size_t>(size);
    char *buf = (char *)malloc(length + 1);
    if (!buf)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "zenblitz3d: out of memory reading '%s'", path);
        SDL_RWclose(f);
        return nullptr;
    }
    size_t n = SDL_RWread(f, buf, 1, length);
    if (n != length)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "zenblitz3d: cannot read '%s'", path);
        free(buf);
        SDL_RWclose(f);
        return nullptr;
    }
    buf[n] = '\0';
    SDL_RWclose(f);
    return buf;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        SDL_Log("usage: zenblitz3d program.bb");
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
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "zenblitz3d: compilation failed.");
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
    return rc;
}
