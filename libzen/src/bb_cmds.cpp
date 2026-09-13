/*
** bb_cmds.cpp — Blitz Basic core commands: console, strings, maths, system.
** Signatures use the original Blitz runtime format so the compiler sees the
** exact same parameter lists and defaults as Blitz3D.
*/
#include "bb_runtime.h"
#include "bb_handles.h"
#include "object.h"
#include <cctype>
#include <ctime>

using namespace zen;

namespace bb
{
    /* ================= console ================= */
    static int c_Print(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        string s = bb_arg_str(args[0]);
        s += "\n";
        bb_print(vm, s.data(), (int)s.size());
        return 0;
    }

    static int c_Write(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        bb_print(vm, bb_arg_cstr(args[0]), bb_arg_len(args[0]));
        return 0;
    }

    static int c_Input(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        bb_print(vm, bb_arg_cstr(args[0]), bb_arg_len(args[0]));
        const Backend &b = vm->backend();
        char line[4096];
        int n = b.input ? b.input(line, sizeof(line), b.userdata) : -1;
        if (n > 0 && line[n - 1] == '\r') --n;
        args[0] = bb_ret_str(vm, line, n < 0 ? 0 : n);
        return 1;
    }

    static int c_Locate(VM *vm, Value *args, int nargs) { (void)vm; (void)args; (void)nargs; return 0; }

    /* ================= strings ================= */
    static int c_String(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        string s = bb_arg_str(args[0]), t;
        long long n = bb_arg_int(args[1]);
        while (n-- > 0) t += s;
        args[0] = bb_ret_str(vm, t);
        return 1;
    }

    static int c_Left(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        long long n = bb_arg_int(args[1]);
        if (n < 0) { vm->runtime_error("parameter must be positive"); return -1; }
        string s = bb_arg_str(args[0]);
        args[0] = bb_ret_str(vm, s.substr(0, (size_t)n));
        return 1;
    }

    static int c_Right(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        long long n = bb_arg_int(args[1]);
        if (n < 0) { vm->runtime_error("parameter must be positive"); return -1; }
        string s = bb_arg_str(args[0]);
        long long p = (long long)s.size() - n;
        if (p < 0) p = 0;
        args[0] = bb_ret_str(vm, s.substr((size_t)p));
        return 1;
    }

    static int c_Replace(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        string s = bb_arg_str(args[0]), from = bb_arg_str(args[1]), to = bb_arg_str(args[2]);
        if (from.size())
        {
            size_t n = 0;
            while (n < s.size() && (n = s.find(from, n)) != string::npos)
            {
                s.replace(n, from.size(), to);
                n += to.size();
            }
        }
        args[0] = bb_ret_str(vm, s);
        return 1;
    }

    static int c_Instr(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        long long from = bb_arg_int(args[2]);
        if (from <= 0) { vm->runtime_error("parameter must be greater than 0"); return -1; }
        --from;
        string s = bb_arg_str(args[0]), t = bb_arg_str(args[1]);
        size_t n = (size_t)from <= s.size() ? s.find(t, (size_t)from) : string::npos;
        args[0] = val_int(n == string::npos ? 0 : (long long)n + 1);
        return 1;
    }

    static int c_Mid(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        long long o = bb_arg_int(args[1]);
        long long n = bb_arg_int(args[2]);
        if (o <= 0) { vm->runtime_error("parameter must be greater than 0"); return -1; }
        --o;
        string s = bb_arg_str(args[0]);
        if ((size_t)o > s.size()) o = (long long)s.size();
        if (n >= 0) s = s.substr((size_t)o, (size_t)n);
        else s = s.substr((size_t)o);
        args[0] = bb_ret_str(vm, s);
        return 1;
    }

    static int c_Upper(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        args[0] = bb_ret_str(vm, bb_toupper(bb_arg_str(args[0])));
        return 1;
    }

    static int c_Lower(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        args[0] = bb_ret_str(vm, bb_tolower(bb_arg_str(args[0])));
        return 1;
    }

    static int c_Trim(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        string s = bb_arg_str(args[0]);
        size_t n = 0, p = s.size();
        while (n < s.size() && !isgraph((unsigned char)s[n])) ++n;
        while (p > n && !isgraph((unsigned char)s[p - 1])) --p;
        args[0] = bb_ret_str(vm, s.substr(n, p - n));
        return 1;
    }

    static int c_LSet(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        long long n = bb_arg_int(args[1]);
        if (n < 0) { vm->runtime_error("parameter must be positive"); return -1; }
        string s = bb_arg_str(args[0]);
        if ((long long)s.size() > n) s = s.substr(0, (size_t)n);
        else while ((long long)s.size() < n) s += ' ';
        args[0] = bb_ret_str(vm, s);
        return 1;
    }

    static int c_RSet(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        long long n = bb_arg_int(args[1]);
        if (n < 0) { vm->runtime_error("parameter must be positive"); return -1; }
        string s = bb_arg_str(args[0]);
        if ((long long)s.size() > n) s = s.substr(s.size() - (size_t)n);
        else while ((long long)s.size() < n) s = ' ' + s;
        args[0] = bb_ret_str(vm, s);
        return 1;
    }

    static int c_Chr(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        char c = (char)bb_arg_int(args[0]);
        args[0] = bb_ret_str(vm, &c, 1);
        return 1;
    }

    static int c_Asc(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        int n = bb_arg_len(args[0]) ? (unsigned char)bb_arg_cstr(args[0])[0] : -1;
        args[0] = val_int(n);
        return 1;
    }

    static int c_Len(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        args[0] = val_int(bb_arg_len(args[0]));
        return 1;
    }

    static int c_Hex(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        unsigned int n = (unsigned int)bb_arg_int(args[0]);
        char buf[16];
        snprintf(buf, sizeof(buf), "%08X", n);
        args[0] = bb_ret_str(vm, buf, 8);
        return 1;
    }

    static int c_Bin(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        unsigned int n = (unsigned int)bb_arg_int(args[0]);
        char buf[40];
        for (int k = 31; k >= 0; n >>= 1, --k) buf[k] = (n & 1) ? '1' : '0';
        buf[32] = 0;
        args[0] = bb_ret_str(vm, buf, 32);
        return 1;
    }

    static int c_CurrentDate(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        time_t t;
        time(&t);
        char buf[256];
        strftime(buf, sizeof(buf), "%d %b %Y", localtime(&t));
        args[0] = bb_ret_str(vm, buf, (int)strlen(buf));
        return 1;
    }

    static int c_CurrentTime(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        time_t t;
        time(&t);
        char buf[256];
        strftime(buf, sizeof(buf), "%H:%M:%S", localtime(&t));
        args[0] = bb_ret_str(vm, buf, (int)strlen(buf));
        return 1;
    }

    /* ================= maths ================= */
    static const double dtor = 0.0174532925199432957692369076848861;
    static const double rtod = 57.2957795130823208767981548141052;

#define BB_MATH1(NAME, EXPR)                                  \
    static int c_##NAME(VM *vm, Value *args, int nargs)        \
    {                                                          \
        (void)vm; (void)nargs;                                 \
        double n = bb_arg_float(args[0]);                      \
        args[0] = val_float((double)(EXPR));                   \
        return 1;                                              \
    }

    BB_MATH1(Sin, sin(n * dtor))
    BB_MATH1(Cos, cos(n * dtor))
    BB_MATH1(Tan, tan(n * dtor))
    BB_MATH1(ASin, asin(n) * rtod)
    BB_MATH1(ACos, acos(n) * rtod)
    BB_MATH1(ATan, atan(n) * rtod)
    BB_MATH1(Sqr, sqrt(n))
    BB_MATH1(Floor, floor(n))
    BB_MATH1(Ceil, ceil(n))
    BB_MATH1(Exp, exp(n))
    BB_MATH1(Log, log(n))
    BB_MATH1(Log10, log10(n))

    static int c_ATan2(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        args[0] = val_float(atan2(bb_arg_float(args[0]), bb_arg_float(args[1])) * rtod);
        return 1;
    }

    /* Blitz's own LCG so Rnd/Rand sequences match the original */
    static int rnd_state = 0x1234;
    static const int RND_A = 48271;
    static const int RND_M = 2147483647;
    static const int RND_Q = 44488;
    static const int RND_R = 3399;

    static inline float rnd()
    {
        rnd_state = RND_A * (rnd_state % RND_Q) - RND_R * (rnd_state / RND_Q);
        if (rnd_state < 0) rnd_state += RND_M;
        return (rnd_state & 65535) / 65536.0f + (.5f / 65536.0f);
    }

    static int c_Rnd(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        float from = (float)bb_arg_float(args[0]), to = (float)bb_arg_float(args[1]);
        args[0] = val_float(rnd() * (to - from) + from);
        return 1;
    }

    static int c_Rand(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        long long from = bb_arg_int(args[0]), to = bb_arg_int(args[1]);
        if (to < from) std::swap(from, to);
        args[0] = val_int((long long)(rnd() * (float)(to - from + 1)) + from);
        return 1;
    }

    static int c_SeedRnd(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        int seed = (int)bb_arg_int(args[0]) & 0x7fffffff;
        rnd_state = seed ? seed : 1;
        return 0;
    }

    static int c_RndSeed(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        args[0] = val_int(rnd_state);
        return 1;
    }

    /* ================= system ================= */
    static int c_End(VM *vm, Value *args, int nargs) { (void)vm; (void)args; (void)nargs; return 0; } /* emitted inline as HALT */
    static int c_Stop(VM *vm, Value *args, int nargs) { (void)vm; (void)args; (void)nargs; return 0; }

    static int c_AppTitle(VM *vm, Value *args, int nargs) { (void)vm; (void)args; (void)nargs; return 0; }

    static int c_RuntimeError(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        vm->runtime_error("%s", bb_arg_cstr(args[0]));
        return -1;
    }

    /* bbExecFile returns nothing - the original launched the command and
       reported failure through a runtime error, so a script never had a
       value to test. */
    static int c_ExecFile(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        const Backend &b = vm->backend();
        if (!b.exec)
        {
            backend_log(b, LOG_WARN, "ExecFile: not supported");
            return 0;
        }
        b.exec(bb_arg_cstr(args[0]), b.userdata);
        return 0;
    }

    static int64_t millisecs(VM *vm)
    {
        const Backend &b = vm->backend();
        return b.millisecs ? b.millisecs(b.userdata) : 0;
    }
    /* Delay never sleeps itself: it hands the host a wake-up deadline via
       request_suspend() and returns. The suspend takes effect after this
       native, so the program continues at the statement after Delay once
       the host calls resume() — a console loop blocks until the deadline
       (see rt_main), while a browser or Android host must not block its
       main thread and schedules resume() for wake_at() instead. */
    static int c_Delay(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        int64_t ms = bb_arg_int(args[0]);
        if (ms > 0) vm->request_suspend(millisecs(vm) + ms);
        return 0;
    }

    static int c_MilliSecs(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        args[0] = val_int((int)millisecs(vm)); /* wraps like Blitz's 32-bit MilliSecs */
        return 1;
    }

    static string g_cmdline;
    void bb_set_command_line(const string &s) { g_cmdline = s; }

    static int c_CommandLine(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        string s = g_cmdline;
        if (!s.size())
        {
            /* the CLI installs script arguments in the "args" global */
            int gi = vm->find_global("args");
            if (gi >= 0 && is_array(vm->get_global(gi)))
            {
                ObjArray *a = as_array(vm->get_global(gi));
                for (int i = 0; i < arr_count(a); ++i)
                {
                    if (i) s += " ";
                    s += bb_arg_str(a->data[i]);
                }
            }
        }
        args[0] = bb_ret_str(vm, s);
        return 1;
    }

    /* SystemProperty and GetEnv$ both ask the backend; the platform strings
       ("os", "cpu") are its business, not the runtime's. */
    static int property(VM *vm, Value *args, const char *name)
    {
        const Backend &b = vm->backend();
        const char *v = b.get_property ? b.get_property(name, b.userdata) : nullptr;
        args[0] = bb_ret_str(vm, v ? v : "");
        return 1;
    }

    static int c_SystemProperty(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        string p = bb_tolower(bb_arg_str(args[0]));
        if (p == "blitzversion") { args[0] = bb_ret_str(vm, "1.108"); return 1; }
        return property(vm, args, p.c_str());
    }

    static int c_GetEnv(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        return property(vm, args, bb_arg_cstr(args[0]));
    }

    static int c_SetEnv(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        const Backend &b = vm->backend();
        if (b.set_property) b.set_property(bb_arg_cstr(args[0]), bb_arg_cstr(args[1]), b.userdata);
        return 0;
    }

    static int c_DebugLog(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        backend_log(vm->backend(), LOG_INFO, bb_arg_cstr(args[0]));
        return 0;
    }

    /* timers: fixed-rate timers on the backend clock (milliseconds) */
    struct BBTimer { double period; double next; };
    static Handles<BBTimer *> timers;

    static int c_CreateTimer(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        long long hz = bb_arg_int(args[0]);
        if (hz <= 0) hz = 1;
        BBTimer *t = new BBTimer;
        t->period = 1000.0 / (double)hz;
        t->next = (double)millisecs(vm) + t->period;
        args[0] = val_int(timers.add(t));
        return 1;
    }

    /* Like Delay, WaitTimer yields to the host rather than blocking: it
       suspends with the tick deadline and reports the ticks elapsed. The
       host resumes at the statement after WaitTimer, so the count is taken
       against the clock as it will be once the deadline passes. */
    static int c_WaitTimer(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        BBTimer *t = timers.get(bb_arg_int(args[0]));
        if (!t) { args[0] = val_int(0); return 1; }
        double now = (double)millisecs(vm);
        if (now < t->next) vm->request_suspend((int64_t)(t->next + 0.5));
        double reached = now < t->next ? t->next : now;
        int ticks = 0;
        while (t->next <= reached) { t->next += t->period; ++ticks; }
        args[0] = val_int(ticks);
        return 1;
    }

    static int c_FreeTimer(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        long long h = bb_arg_int(args[0]);
        delete timers.get(h);
        timers.remove(h);
        return 0;
    }

    /* ================= table ================= */
    const BBCommand bb_cmds_basic[] = {
        {"Print$string=\"\"", c_Print},
        {"Write$string", c_Write},
        {"$Input$prompt=\"\"", c_Input},
        {"Locate%x%y", c_Locate},

        {"$String$string%repeat", c_String},
        {"$Left$string%count", c_Left},
        {"$Right$string%count", c_Right},
        {"$Replace$string$from$to", c_Replace},
        {"%Instr$string$find%from=1", c_Instr},
        {"$Mid$string%start%count=-1", c_Mid},
        {"$Upper$string", c_Upper},
        {"$Lower$string", c_Lower},
        {"$Trim$string", c_Trim},
        {"$LSet$string%size", c_LSet},
        {"$RSet$string%size", c_RSet},
        {"$Chr%ascii", c_Chr},
        {"%Asc$string", c_Asc},
        {"%Len$string", c_Len},
        {"$Hex%value", c_Hex},
        {"$Bin%value", c_Bin},
        {"$CurrentDate", c_CurrentDate},
        {"$CurrentTime", c_CurrentTime},

        {"#Sin#degrees", c_Sin},
        {"#Cos#degrees", c_Cos},
        {"#Tan#degrees", c_Tan},
        {"#ASin#float", c_ASin},
        {"#ACos#float", c_ACos},
        {"#ATan#float", c_ATan},
        {"#ATan2#floata#floatb", c_ATan2},
        {"#Sqr#float", c_Sqr},
        {"#Floor#float", c_Floor},
        {"#Ceil#float", c_Ceil},
        {"#Exp#float", c_Exp},
        {"#Log#float", c_Log},
        {"#Log10#float", c_Log10},
        {"#Rnd#from#to=0", c_Rnd},
        {"%Rand%from%to=1", c_Rand},
        {"SeedRnd%seed", c_SeedRnd},
        {"%RndSeed", c_RndSeed},

        {"End", c_End},
        {"Stop", c_Stop},
        {"AppTitle$title$close_prompt=\"\"", c_AppTitle},
        {"RuntimeError$message", c_RuntimeError},
        {"ExecFile$command", c_ExecFile},
        {"Delay%millisecs", c_Delay},
        {"%MilliSecs", c_MilliSecs},
        {"$CommandLine", c_CommandLine},
        {"$SystemProperty$property", c_SystemProperty},
        {"$GetEnv$env_var", c_GetEnv},
        {"SetEnv$env_var$value", c_SetEnv},
        {"%CreateTimer%hertz", c_CreateTimer},
        {"%WaitTimer%timer", c_WaitTimer},
        {"FreeTimer%timer", c_FreeTimer},
        {"DebugLog$text", c_DebugLog},
    };
    const int bb_cmds_basic_count = (int)(sizeof(bb_cmds_basic) / sizeof(bb_cmds_basic[0]));
}
