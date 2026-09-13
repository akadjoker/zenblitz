/*
** backend_stdio.cpp — console implementation of zen::Backend (see the header).
*/
#include "backend_stdio.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(_WIN32)
#include <windows.h>
#include <direct.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctime>
#include <cerrno>
#include <dlfcn.h>
#endif

namespace zen
{
    /* ---- text ---- */
    static void sb_print(const char *s, int n, void *)
    {
        fwrite(s, 1, (size_t)n, stdout);
        // stdout becomes block-buffered when the editor captures it through
        // a pipe. Flush every Blitz Print/Write call so the Output panel can
        // display messages while the program is still running.
        fflush(stdout);
    }
    static void sb_log(int, const char *msg, void *)
    {
        fflush(stdout);
        fputs(msg, stderr);
        fputc('\n', stderr);
    }
    static int sb_input(char *buf, int cap, void *)
    {
        fflush(stdout);
        if (!fgets(buf, cap, stdin)) return 0;
        int n = (int)strlen(buf);
        if (n > 0 && buf[n - 1] == '\n') --n;
        buf[n] = '\0';
        return n;
    }

    /* ---- files ---- */
    static ZenFile sb_open(const char *path, int mode, void *)
    {
        const char *m = mode == FILE_READ ? "rb" : mode == FILE_WRITE ? "w+b" : "r+b";
        return (ZenFile)fopen(path, m);
    }
    static int64_t sb_read(ZenFile f, void *buf, int64_t n, void *)
    {
        return (int64_t)fread(buf, 1, (size_t)n, (FILE *)f);
    }
    static int64_t sb_write(ZenFile f, const void *buf, int64_t n, void *)
    {
        return (int64_t)fwrite(buf, 1, (size_t)n, (FILE *)f);
    }
#if defined(_WIN32)
    static int64_t sb_tell(ZenFile f, void *) { return _ftelli64((FILE *)f); }
    static int64_t sb_seek(ZenFile f, int64_t pos, void *)
    {
        return _fseeki64((FILE *)f, pos, SEEK_SET) == 0 ? _ftelli64((FILE *)f) : -1;
    }
#else
    static int64_t sb_tell(ZenFile f, void *) { return (int64_t)ftello((FILE *)f); }
    static int64_t sb_seek(ZenFile f, int64_t pos, void *)
    {
        return fseeko((FILE *)f, (off_t)pos, SEEK_SET) == 0 ? (int64_t)ftello((FILE *)f) : -1;
    }
#endif
    static int64_t sb_size(ZenFile f, void *ud)
    {
        int64_t cur = sb_tell(f, ud);
#if defined(_WIN32)
        _fseeki64((FILE *)f, 0, SEEK_END);
#else
        fseeko((FILE *)f, 0, SEEK_END);
#endif
        int64_t end = sb_tell(f, ud);
        sb_seek(f, cur, ud);
        return end;
    }
    static void sb_close(ZenFile f, void *) { fclose((FILE *)f); }

    /* ---- filesystem ---- */
#if defined(_WIN32)
    static int sb_path_type(const char *path, int64_t *size, void *)
    {
        struct __stat64 st;
        if (_stat64(path, &st) != 0) return PATH_NONE;
        if (size) *size = (int64_t)st.st_size;
        return (st.st_mode & _S_IFDIR) ? PATH_DIR : PATH_FILE;
    }
    static int sb_delete_file(const char *path, void *) { return DeleteFileA(path) ? 1 : 0; }

    struct WinDir
    {
        HANDLE h;
        WIN32_FIND_DATAA fd;
        bool pending; /* the entry from FindFirstFile not yet returned */
    };
    static ZenDir sb_open_dir(const char *path, void *)
    {
        char pattern[MAX_PATH];
        snprintf(pattern, sizeof(pattern), "%s\\*", path);
        WinDir *d = new WinDir;
        d->h = FindFirstFileA(pattern, &d->fd);
        if (d->h == INVALID_HANDLE_VALUE) { delete d; return nullptr; }
        d->pending = true;
        return d;
    }
    static const char *sb_next_file(ZenDir dp, void *)
    {
        WinDir *d = (WinDir *)dp;
        if (d->pending) { d->pending = false; return d->fd.cFileName; }
        return FindNextFileA(d->h, &d->fd) ? d->fd.cFileName : nullptr;
    }
    static void sb_close_dir(ZenDir dp, void *)
    {
        WinDir *d = (WinDir *)dp;
        FindClose(d->h);
        delete d;
    }
    static int sb_create_dir(const char *path, void *) { return _mkdir(path) == 0; }
    static int sb_delete_dir(const char *path, void *) { return _rmdir(path) == 0; }
    static int sb_change_dir(const char *path, void *) { return _chdir(path) == 0; }
    static const char *sb_current_dir(void *)
    {
        static char buf[4096];
        return _getcwd(buf, sizeof(buf)) ? buf : "";
    }
#else
    /* Blitz3D shipped for Windows, where "\" is the path separator, and
       real .bb source writes it that way - castle.bb's own ChangeDir "..\"
       is typical. Here "\" is an ordinary filename character, so chdir()
       looks for a directory literally called "..\", fails, and the script
       carries on in the wrong directory with every later load silently
       missing its asset. Rewrite the separator for the syscalls that take
       a directory path; a real "\" in a POSIX directory name would be
       unreachable this way, but matching what the .bb source means is
       worth more than that. (Files go through engine's own
       resolveCaseInsensitive(), which already normalises separators.) */
    struct NativePath
    {
        char buf[4096];
        explicit NativePath(const char *path)
        {
            size_t n = path ? strlen(path) : 0;
            if (n >= sizeof(buf)) n = sizeof(buf) - 1;
            for (size_t k = 0; k < n; ++k) buf[k] = path[k] == '\\' ? '/' : path[k];
            buf[n] = '\0';
        }
        const char *c_str() const { return buf; }
    };
    static int sb_path_type(const char *path, int64_t *size, void *)
    {
        struct stat st;
        if (stat(NativePath(path).c_str(), &st) != 0) return PATH_NONE;
        if (size) *size = (int64_t)st.st_size;
        return S_ISDIR(st.st_mode) ? PATH_DIR : PATH_FILE;
    }
    static int sb_delete_file(const char *path, void *) { return remove(path) == 0; }
    static ZenDir sb_open_dir(const char *path, void *)
    {
        return (ZenDir)opendir(NativePath(path).c_str());
    }
    static const char *sb_next_file(ZenDir d, void *)
    {
        struct dirent *e = readdir((DIR *)d);
        return e ? e->d_name : nullptr;
    }
    static void sb_close_dir(ZenDir d, void *) { closedir((DIR *)d); }
    static int sb_create_dir(const char *path, void *)
    {
        return mkdir(NativePath(path).c_str(), 0777) == 0;
    }
    static int sb_delete_dir(const char *path, void *)
    {
        return rmdir(NativePath(path).c_str()) == 0;
    }
    static int sb_change_dir(const char *path, void *)
    {
        return chdir(NativePath(path).c_str()) == 0;
    }
    static const char *sb_current_dir(void *)
    {
        static char buf[4096];
        return getcwd(buf, sizeof(buf)) ? buf : "";
    }
#endif

    /* ---- dynamic libraries (userlibs) ---- */
#if defined(_WIN32)
    static void *sb_lib_open(const char *path, void *) { return (void *)LoadLibraryA(path); }
    static void *sb_lib_symbol(void *lib, const char *name, void *)
    {
        return (void *)GetProcAddress((HMODULE)lib, name);
    }
    static void sb_lib_close(void *lib, void *) { FreeLibrary((HMODULE)lib); }
#else
    static void *sb_lib_open(const char *path, void *) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
    static void *sb_lib_symbol(void *lib, const char *name, void *) { return dlsym(lib, name); }
    static void sb_lib_close(void *lib, void *) { dlclose(lib); }
#endif

    /* ---- environment ---- */
    static const char *sb_get_property(const char *name, void *)
    {
        if (strcmp(name, "os") == 0)
        {
#if defined(_WIN32)
            return "Windows";
#elif defined(__APPLE__)
            return "MacOS";
#else
            return "Linux";
#endif
        }
        if (strcmp(name, "cpu") == 0)
        {
#if defined(__x86_64__) || defined(_M_X64)
            return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
            return "arm64";
#else
            return "";
#endif
        }
        const char *v = getenv(name);
        return v ? v : "";
    }
    static int sb_exec(const char *command, void *)
    {
        return system(command);
    }
    static void sb_set_property(const char *name, const char *value, void *)
    {
#if defined(_WIN32)
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s=%s", name, value);
        _putenv(buf);
#else
        setenv(name, value, 1);
#endif
    }

    /* ---- time ---- */
#if defined(_WIN32)
    static int64_t sb_millisecs(void *) { return (int64_t)GetTickCount64(); }
    static void sb_delay(int64_t ms, void *) { Sleep((DWORD)ms); }
#else
    static int64_t sb_millisecs(void *)
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    }
    static void sb_delay(int64_t ms, void *)
    {
        struct timespec ts;
        ts.tv_sec = (time_t)(ms / 1000);
        ts.tv_nsec = (long)(ms % 1000) * 1000000;
        while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {}
    }
#endif

    Backend stdio_backend()
    {
        Backend b = null_backend();
        b.print = sb_print;
        b.log = sb_log;
        b.input = sb_input;
        b.open = sb_open;
        b.read = sb_read;
        b.write = sb_write;
        b.seek = sb_seek;
        b.tell = sb_tell;
        b.size = sb_size;
        b.close = sb_close;
        b.path_type = sb_path_type;
        b.delete_file = sb_delete_file;
        b.open_dir = sb_open_dir;
        b.next_file = sb_next_file;
        b.close_dir = sb_close_dir;
        b.create_dir = sb_create_dir;
        b.delete_dir = sb_delete_dir;
        b.change_dir = sb_change_dir;
        b.current_dir = sb_current_dir;
        b.millisecs = sb_millisecs;
        b.delay = sb_delay;
        b.get_property = sb_get_property;
        b.set_property = sb_set_property;
        b.exec = sb_exec;
        b.lib_open = sb_lib_open;
        b.lib_symbol = sb_lib_symbol;
        b.lib_close = sb_lib_close;
        return b;
    }
}
