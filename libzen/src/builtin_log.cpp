/* =========================================================
** builtin_log.cpp — "log" module for Zen
**
** Structured logging with levels, timestamps, and file output.
**
** Usage:
**   import log;
**
**   log.info("Server started");
**   log.warn("Low memory: {mb}MB");
**   log.error("Connection refused");
**   log.debug("x = {x}");
**
**   log.setLevel("warn");          // suppress debug/info
**   log.setFile("/tmp/app.log");   // also write to file
**   log.setFormat("simple");       // "simple" | "full" | "json"
**   log.close();
** ========================================================= */

#include "module.h"
#include "vm.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <ctime>

namespace zen
{

/* =========================================================
** State (module-level globals — single log context)
** ========================================================= */

enum LogLevel { LOG_DEBUG = 0, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_NONE };

static LogLevel  g_level   = LOG_DEBUG;
static FILE     *g_file    = nullptr;  /* extra output file (optional) */
static int       g_format  = 0;        /* 0=simple, 1=full, 2=json */

static const char *level_name(LogLevel l)
{
    switch (l)
    {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO:  return "INFO";
        case LOG_WARN:  return "WARN";
        case LOG_ERROR: return "ERROR";
        default:        return "NONE";
    }
}

static void do_log(LogLevel level, const char *msg)
{
    if (level < g_level) return;

    char timebuf[32] = "";
    if (g_format >= 1)
    {
        time_t now = time(nullptr);
        struct tm *lt = localtime(&now);
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", lt);
    }

    char line[2048];
    int n = 0;

    if (g_format == 0)
    {
        /* simple: [LEVEL] message */
        n = snprintf(line, sizeof(line), "[%s] %s\n", level_name(level), msg);
    }
    else if (g_format == 1)
    {
        /* full: timestamp [LEVEL] message */
        n = snprintf(line, sizeof(line), "%s [%s] %s\n", timebuf, level_name(level), msg);
    }
    else
    {
        /* json: {"time":"...","level":"...","msg":"..."} */
        /* Escape msg */
        char escaped[1024];
        int ei = 0;
        for (const char *c = msg; *c && ei < (int)sizeof(escaped) - 2; c++)
        {
            if (*c == '"')  { escaped[ei++] = '\\'; escaped[ei++] = '"'; }
            else if (*c == '\\') { escaped[ei++] = '\\'; escaped[ei++] = '\\'; }
            else if (*c == '\n') { escaped[ei++] = '\\'; escaped[ei++] = 'n'; }
            else            { escaped[ei++] = *c; }
        }
        escaped[ei] = '\0';
        n = snprintf(line, sizeof(line), "{\"time\":\"%s\",\"level\":\"%s\",\"msg\":\"%s\"}\n",
                     timebuf, level_name(level), escaped);
    }

    /* Write to stderr (errors) or stdout */
    FILE *out = (level >= LOG_ERROR) ? stderr : stdout;
    fwrite(line, 1, (size_t)n, out);
    fflush(out);

    /* Write to file if set */
    if (g_file)
    {
        fwrite(line, 1, (size_t)n, g_file);
        fflush(g_file);
    }
}

/* =========================================================
** log.debug / info / warn / error (message)
** ========================================================= */

static int nat_log_debug(VM *vm, Value *args, int nargs)
{
    (void)vm;
    const char *msg = (nargs >= 1 && is_string(args[0])) ? as_cstring(args[0]) : "";
    do_log(LOG_DEBUG, msg);
    args[0] = val_bool(true);
    return 1;
}

static int nat_log_info(VM *vm, Value *args, int nargs)
{
    (void)vm;
    const char *msg = (nargs >= 1 && is_string(args[0])) ? as_cstring(args[0]) : "";
    do_log(LOG_INFO, msg);
    args[0] = val_bool(true);
    return 1;
}

static int nat_log_warn(VM *vm, Value *args, int nargs)
{
    (void)vm;
    const char *msg = (nargs >= 1 && is_string(args[0])) ? as_cstring(args[0]) : "";
    do_log(LOG_WARN, msg);
    args[0] = val_bool(true);
    return 1;
}

static int nat_log_error(VM *vm, Value *args, int nargs)
{
    (void)vm;
    const char *msg = (nargs >= 1 && is_string(args[0])) ? as_cstring(args[0]) : "";
    do_log(LOG_ERROR, msg);
    args[0] = val_bool(true);
    return 1;
}

/* =========================================================
** log.setLevel(level) — "debug"|"info"|"warn"|"error"|"none"
** ========================================================= */

static int nat_log_set_level(VM *vm, Value *args, int nargs)
{
    if (nargs < 1 || !is_string(args[0]))
    {
        vm->runtime_error("log.setLevel() expects (\"debug\"|\"info\"|\"warn\"|\"error\"|\"none\")");
        return -1;
    }
    const char *s = as_cstring(args[0]);
    if      (strcmp(s, "debug") == 0) g_level = LOG_DEBUG;
    else if (strcmp(s, "info")  == 0) g_level = LOG_INFO;
    else if (strcmp(s, "warn")  == 0) g_level = LOG_WARN;
    else if (strcmp(s, "error") == 0) g_level = LOG_ERROR;
    else if (strcmp(s, "none")  == 0) g_level = LOG_NONE;
    args[0] = val_bool(true);
    return 1;
}

/* =========================================================
** log.setFormat(fmt) — "simple"|"full"|"json"
** ========================================================= */

static int nat_log_set_format(VM *vm, Value *args, int nargs)
{
    if (nargs < 1 || !is_string(args[0]))
    {
        vm->runtime_error("log.setFormat() expects (\"simple\"|\"full\"|\"json\")");
        return -1;
    }
    const char *s = as_cstring(args[0]);
    if      (strcmp(s, "simple") == 0) g_format = 0;
    else if (strcmp(s, "full")   == 0) g_format = 1;
    else if (strcmp(s, "json")   == 0) g_format = 2;
    args[0] = val_bool(true);
    return 1;
}

/* =========================================================
** log.setFile(path) — also write to file
** ========================================================= */

static int nat_log_set_file(VM *vm, Value *args, int nargs)
{
    if (nargs < 1 || !is_string(args[0]))
    {
        vm->runtime_error("log.setFile() expects (path)");
        return -1;
    }
    if (g_file) { fclose(g_file); g_file = nullptr; }
    g_file = fopen(as_cstring(args[0]), "ab");
    args[0] = val_bool(g_file != nullptr);
    return 1;
}

/* =========================================================
** log.close() — close file handle
** ========================================================= */

static int nat_log_close(VM *, Value *args, int)
{
    if (g_file) { fclose(g_file); g_file = nullptr; }
    args[0] = val_bool(true);
    return 1;
}

/* =========================================================
** log.level() → current level string
** ========================================================= */

static int nat_log_level(VM *vm, Value *args, int)
{
    const char *names[] = {"debug","info","warn","error","none"};
    args[0] = val_obj((Obj *)vm->make_string(names[(int)g_level]));
    return 1;
}

/* =========================================================
** Registration
** ========================================================= */

static const NativeReg log_functions[] = {
    {"debug",     nat_log_debug,      1},
    {"info",      nat_log_info,       1},
    {"warn",      nat_log_warn,       1},
    {"error",     nat_log_error,      1},
    {"setLevel",  nat_log_set_level,  1},
    {"setFormat", nat_log_set_format, 1},
    {"setFile",   nat_log_set_file,   1},
    {"close",     nat_log_close,      0},
    {"getLevel",    nat_log_level,      0},
};

extern const NativeLib zen_lib_log = {
    "logger",
    log_functions,
    9,
    nullptr,
    0,
    nullptr
};

} /* namespace zen */
