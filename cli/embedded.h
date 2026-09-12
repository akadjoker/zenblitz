/*
** embedded.h — the standalone executable format, shared by the compiler
** (which writes it: `zenblitz --build`) and the runtimes (which look for a
** program appended to themselves at startup):
**
**   [runtime executable][.zbc bytes][u64 bytecode size]["ZBLZEXE1"]
*/
#ifndef ZENBLITZ_EMBEDDED_H
#define ZENBLITZ_EMBEDDED_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

static const char EXE_MAGIC[8] = {'Z', 'B', 'L', 'Z', 'E', 'X', 'E', '1'};

/* Path of the running executable, or nullptr where it cannot be found. */
static char *self_path(char *buf, size_t size)
{
#if defined(__linux__)
    ssize_t n = readlink("/proc/self/exe", buf, size - 1);
    if (n <= 0) return nullptr;
    buf[n] = '\0';
    return buf;
#elif defined(_WIN32)
    if (GetModuleFileNameA(nullptr, buf, (DWORD)size) == 0) return nullptr;
    return buf;
#else
    (void)buf; (void)size;
    return nullptr;
#endif
}

/* Path of a file or directory next to the running executable (Blitz3D kept
   its userlibs and runtime beside the compiler; so do we). */
static const char *path_beside_exe(const char *name, char *buf, size_t size)
{
    char self[4096];
    if (!self_path(self, sizeof(self))) return nullptr;
    const char *slash = strrchr(self, '/');
#if defined(_WIN32)
    const char *bslash = strrchr(self, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
    size_t dir = slash ? (size_t)(slash - self + 1) : 0;
    if (dir + strlen(name) + 1 > size) return nullptr;
    memcpy(buf, self, dir);
    strcpy(buf + dir, name);
    return buf;
}

/* If this executable carries an embedded program, return it (malloc'd). */
static char *embedded_program(long *out_size)
{
    char self[4096];
    if (!self_path(self, sizeof(self))) return nullptr;
    FILE *f = fopen(self, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long total = ftell(f);
    if (total < 16) { fclose(f); return nullptr; }
    char trailer[16];
    fseek(f, total - 16, SEEK_SET);
    if (fread(trailer, 1, 16, f) != 16 || memcmp(trailer + 8, EXE_MAGIC, 8) != 0) { fclose(f); return nullptr; }
    uint64_t sz = 0;
    memcpy(&sz, trailer, 8);
    if (sz == 0 || (long)sz > total - 16) { fclose(f); return nullptr; }
    char *buf = (char *)malloc((size_t)sz + 1);
    fseek(f, total - 16 - (long)sz, SEEK_SET);
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != sz) { free(buf); return nullptr; }
    buf[sz] = '\0';
    *out_size = (long)sz;
    return buf;
}

#endif /* ZENBLITZ_EMBEDDED_H */
