#ifndef ZEN_BACKEND_H
#define ZEN_BACKEND_H

/*
** backend.h — the platform services a running program needs: text output,
** logging, console input, files, directories, time.
**
** libzen only defines the interface. Whoever starts the VM plugs an
** implementation with VM::set_backend() before install_runtime(): the
** console runtime uses stdio (cli/backend_stdio.cpp), the engine uses SDL,
** the web build the browser. The Blitz commands never touch the OS
** themselves — every function here may be nullptr, in which case the
** command that needs it returns 0 / "" and logs "not supported".
*/

#include <cstdint>

namespace zen
{
    enum { LOG_INFO = 0, LOG_WARN = 1, LOG_ERROR = 2 };
    enum { FILE_READ = 0, FILE_WRITE = 1, FILE_READWRITE = 2 }; /* open() modes */
    enum { PATH_NONE = 0, PATH_FILE = 1, PATH_DIR = 2 };        /* path_type() results */

    typedef void *ZenFile; /* opaque handles owned by the backend */
    typedef void *ZenDir;

    struct Backend
    {
        void *userdata; /* passed back to every call */

        /* text */
        void (*print)(const char *s, int n, void *ud);      /* Print / Write */
        void (*log)(int level, const char *msg, void *ud);  /* runtime errors, DebugLog, compiler errors */
        int (*input)(char *buf, int cap, void *ud);         /* Input$: one line without '\n'; bytes read, -1 = no console */

        /* files */
        ZenFile (*open)(const char *path, int mode, void *ud);            /* nullptr on failure */
        int64_t (*read)(ZenFile f, void *buf, int64_t n, void *ud);       /* bytes read */
        int64_t (*write)(ZenFile f, const void *buf, int64_t n, void *ud); /* bytes written */
        int64_t (*seek)(ZenFile f, int64_t pos, void *ud);                /* absolute; new position or -1 */
        int64_t (*tell)(ZenFile f, void *ud);
        int64_t (*size)(ZenFile f, void *ud);
        void (*close)(ZenFile f, void *ud);

        /* filesystem */
        int (*path_type)(const char *path, int64_t *size, void *ud); /* PATH_*; size may be nullptr */
        int (*delete_file)(const char *path, void *ud);              /* 1 ok, 0 failed */
        ZenDir (*open_dir)(const char *path, void *ud);
        const char *(*next_file)(ZenDir d, void *ud);                /* valid until the next call; nullptr at end */
        void (*close_dir)(ZenDir d, void *ud);
        int (*create_dir)(const char *path, void *ud);
        int (*delete_dir)(const char *path, void *ud);
        int (*change_dir)(const char *path, void *ud);
        const char *(*current_dir)(void *ud);                        /* valid until the next call */

        /* time */
        int64_t (*millisecs)(void *ud);
        void (*delay)(int64_t ms, void *ud);

        /* environment: GetEnv$/SetEnv/SystemProperty$. Names are what the
           program asks for ("os", "cpu", "PATH", ...); nullptr or "" when
           the platform has no such notion. Result valid until the next call. */
        const char *(*get_property)(const char *name, void *ud);
        void (*set_property)(const char *name, const char *value, void *ud);

        /* ExecFile: run a command line, returning its exit code (-1 if it
           could not be started). Desktop only — a browser or Android
           backend leaves this nullptr. */
        int (*exec)(const char *command, void *ud);

        /* Dynamic libraries, for Blitz3D-style userlibs (.decls). Desktop
           only: a browser or Android backend leaves these nullptr and the
           declared functions then report the library as unavailable. */
        void *(*lib_open)(const char *path, void *ud);       /* nullptr if not found */
        void *(*lib_symbol)(void *lib, const char *name, void *ud);
        void (*lib_close)(void *lib, void *ud);
    };

    /* print/log through zenconf.h's zen_write/zen_writeerr, everything else
       nullptr. What a VM starts with. */
    Backend null_backend();

    inline void backend_print(const Backend &b, const char *s, int n)
    {
        if (b.print) b.print(s, n, b.userdata);
    }
    inline void backend_log(const Backend &b, int level, const char *msg)
    {
        if (b.log) b.log(level, msg, b.userdata);
    }
}

#endif /* ZEN_BACKEND_H */
