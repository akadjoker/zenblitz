/* =========================================================
** builtin_time.cpp — "time" module for Zen
**
** Provides: now, now_ms, clock, sleep, sleep_ms, date
** (uses POSIX/Win32 APIs)
** ========================================================= */

#include "module.h"
#include "vm.h"
#include <ctime>
#include <cstring>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif

namespace zen
{

    /* =========================================================
    ** Native functions
    ** ========================================================= */

    /* now() → epoch seconds (int) */
    static int nat_now(VM *vm, Value *args, int nargs)
    {
        args[0] = val_int((int64_t)time(nullptr));
        return 1;
    }

    /* now_ms() → epoch milliseconds (int) */
    static int nat_now_ms(VM *vm, Value *args, int nargs)
    {
#ifdef _WIN32
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        /* Windows FILETIME is 100ns intervals since 1601-01-01 */
        t -= 116444736000000000ULL; /* offset to unix epoch */
        args[0] = val_int((int64_t)(t / 10000));
#else
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        args[0] = val_int((int64_t)tv.tv_sec * 1000 + (int64_t)tv.tv_usec / 1000);
#endif
        return 1;
    }

    /* clock() → high-resolution seconds (float) */
    static int nat_clock(VM *vm, Value *args, int nargs)
    {
#ifdef _WIN32
        LARGE_INTEGER freq, count;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&count);
        args[0] = val_float((double)count.QuadPart / (double)freq.QuadPart);
#else
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        args[0] = val_float((double)ts.tv_sec + (double)ts.tv_nsec / 1e9);
#endif
        return 1;
    }

    /* sleep(seconds) — pause execution */
    static int nat_sleep(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1)
            return 0;
        double secs = to_number(args[0]);
        if (secs <= 0.0)
            return 0;
#ifdef _WIN32
        Sleep((DWORD)(secs * 1000.0));
#elif defined(__EMSCRIPTEN__)
        /* no-op on emscripten */
        (void)secs;
#else
        struct timespec req;
        req.tv_sec = (time_t)secs;
        req.tv_nsec = (long)((secs - (double)req.tv_sec) * 1e9);
        nanosleep(&req, nullptr);
#endif
        return 0;
    }

    /* sleep_ms(ms) — pause in milliseconds */
    static int nat_sleep_ms(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1)
            return 0;
        int64_t ms = to_integer(args[0]);
        if (ms <= 0)
            return 0;
#ifdef _WIN32
        Sleep((DWORD)ms);
#elif defined(__EMSCRIPTEN__)
        (void)ms;
#else
        struct timespec req;
        req.tv_sec = (time_t)(ms / 1000);
        req.tv_nsec = (long)((ms % 1000) * 1000000L);
        nanosleep(&req, nullptr);
#endif
        return 0;
    }

    /* date(timestamp?) → formatted string "YYYY-MM-DD HH:MM:SS" */
    static int nat_date(VM *vm, Value *args, int nargs)
    {
        time_t t;
        if (nargs >= 1 && is_int(args[0]))
            t = (time_t)to_integer(args[0]);
        else
            t = time(nullptr);

        struct tm *tm_info = localtime(&t);
        if (!tm_info)
        {
            args[0] = val_nil();
            return 1;
        }
        char buf[64];
        int len = (int)strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
        args[0] = val_obj((Obj*)vm->make_string(buf, len));
        return 1;
    }

    /* ftime(timestamp, format) → formatted string */
    static int nat_ftime(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2)
        {
            args[0] = val_nil();
            return 1;
        }
        time_t t = (time_t)to_integer(args[0]);
        if (!is_obj(args[1]))
        {
            args[0] = val_nil();
            return 1;
        }
        ObjString *fmt = as_string(args[1]);

        struct tm *tm_info = localtime(&t);
        if (!tm_info)
        {
            args[0] = val_nil();
            return 1;
        }
        char buf[256];
        int len = (int)strftime(buf, sizeof(buf), fmt->chars, tm_info);
        args[0] = val_obj((Obj*)vm->make_string(buf, len));
        return 1;
    }

    /* diff(t1, t2) → seconds between two timestamps */
    static int nat_diff(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2)
        {
            args[0] = val_int(0);
            return 1;
        }
        int64_t t1 = to_integer(args[0]);
        int64_t t2 = to_integer(args[1]);
        args[0] = val_int(t1 - t2);
        return 1;
    }

    /* =========================================================
    ** Module definition
    ** ========================================================= */

    static const NativeReg time_functions[] = {
        {"now", nat_now, 0},
        {"now_ms", nat_now_ms, 0},
        {"clock", nat_clock, 0},
        {"sleep", nat_sleep, 1},
        {"sleep_ms", nat_sleep_ms, 1},
        {"date", nat_date, -1},
        {"ftime", nat_ftime, 2},
        {"diff", nat_diff, 2},
        {nullptr, nullptr, 0}};

    const NativeLib zen_lib_time = {
        "time",
        time_functions,
        8,
        nullptr,
        0,
        nullptr};

} /* namespace zen */
