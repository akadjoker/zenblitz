/*
** bb_userlibs.cpp — Blitz3D-style user libraries (see bb_userlibs.h).
**
** Two halves:
**   - the .decls parser, which turns each declaration into the same
**     signature string bb_cmds.cpp uses, so BBRuntime::registerCommands
**     type-checks userlib calls exactly like built-in commands;
**   - a thunk that converts the VM's Values into C arguments and calls the
**     exported symbol.
**
** The thunk is the part that cannot be written portably: C has no way to
** build an argument list at run time. It is implemented for the x86-64 and
** AArch64 calling conventions, where integer/pointer arguments go in
** registers and floats in their own; anything else declines the call rather
** than corrupting the stack, so an unsupported target loses userlibs but
** still runs every ordinary program.
*/
#include "bb_userlibs.h"
#include "bb_runtime.h"
#include "object.h"
#include <cstdio>
#include <cstdarg>

using namespace zen;

namespace bb
{
    /* =========================================================
    ** Calling a C function with a run-time argument list
    ** ========================================================= */

    /* Blitz passes at most 8 integer-ish and 8 float arguments, which is
       what both supported ABIs carry in registers; a declaration wanting
       more is rejected by the parser. */
    enum { MAX_ARGS = 8 };

    struct CallArgs
    {
        int64_t ints[MAX_ARGS];  /* ints, pointers and strings */
        double floats[MAX_ARGS];
        int nints, nfloats;
    };

#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64)
#define BB_HAVE_USERLIB_THUNK 1

    /* Calling through a prototype with the right number and kind of
       parameters lets the compiler place each argument per the ABI. Integer
       and floating-point arguments use separate register banks on both
       supported ABIs, so a call is picked by how many of each it takes. */
    typedef int64_t (*FnI)(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t);
    typedef double (*FnF)(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t);
    typedef int64_t (*FnID)(double, double, double, double, double, double, double, double,
                            int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t);
    typedef double (*FnFD)(double, double, double, double, double, double, double, double,
                           int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t);

    /* Passing all 8 (or 16) slots regardless of how many the callee declares
       is what makes one prototype serve every arity: the extra registers are
       simply ignored by the callee, and neither ABI has the caller clean up
       register arguments. */
    static int64_t call_int(void *fn, const CallArgs &a)
    {
        const int64_t *i = a.ints;
        if (a.nfloats == 0)
            return ((FnI)fn)(i[0], i[1], i[2], i[3], i[4], i[5], i[6], i[7]);
        const double *f = a.floats;
        return ((FnID)fn)(f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7],
                          i[0], i[1], i[2], i[3], i[4], i[5], i[6], i[7]);
    }

    static double call_float(void *fn, const CallArgs &a)
    {
        const int64_t *i = a.ints;
        if (a.nfloats == 0)
            return ((FnF)fn)(i[0], i[1], i[2], i[3], i[4], i[5], i[6], i[7]);
        const double *f = a.floats;
        return ((FnFD)fn)(f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7],
                          i[0], i[1], i[2], i[3], i[4], i[5], i[6], i[7]);
    }
#endif

    /* =========================================================
    ** The native that stands in for each declared function
    ** ========================================================= */

    struct UserFunc
    {
        void *fn;        /* resolved symbol, null while the library is missing */
        string libname;  /* for the error message */
        string name;
        string params;   /* one tag per parameter: % # $ */
        char ret;        /* '%', '#', '$' or 0 for void */
    };

    /* Declarations outlive the VM they were registered with: the native
       carries its index here rather than a pointer, so reloading a .decls
       cannot leave a dangling callback behind. */
    static vector<UserFunc *> g_userfuncs;

    /* A native receives no identity of its own, so each declared function
       needs a distinct NativeFn to know which UserFunc it stands for. The
       index is baked into a template instantiation below; the work lives
       here, out of line, so those instantiations stay one call each
       instead of MAX_USERFUNCS copies of this function. */
    static int call_userfunc(VM *vm, Value *args, int nargs, int index)
    {
        UserFunc *uf = g_userfuncs[index];

        if (!uf->fn)
        {
            char msg[256];
            snprintf(msg, sizeof(msg), "userlib '%s': %s is not available",
                     uf->libname.c_str(), uf->name.c_str());
            vm->runtime_error("%s", msg);
            return -1;
        }

#ifndef BB_HAVE_USERLIB_THUNK
        (void)args; (void)nargs;
        vm->runtime_error("userlibs are not supported on this architecture");
        return -1;
#else
        CallArgs ca;
        memset(&ca, 0, sizeof(ca));
        int n = (int)uf->params.size();
        if (nargs < n) n = nargs;
        for (int k = 0; k < n; ++k)
        {
            switch (uf->params[k])
            {
            case '#':
                if (ca.nfloats < MAX_ARGS) ca.floats[ca.nfloats++] = bb_arg_float(args[k]);
                break;
            case '$':
                /* the callee gets the string's own storage, which stays
                   alive for the call: the Value is on the VM stack and the
                   GC cannot run while a native is executing */
                if (ca.nints < MAX_ARGS) ca.ints[ca.nints++] = (int64_t)(intptr_t)bb_arg_cstr(args[k]);
                break;
            default:
                if (ca.nints < MAX_ARGS) ca.ints[ca.nints++] = (int64_t)bb_arg_int(args[k]);
                break;
            }
        }

        switch (uf->ret)
        {
        case '#':
            args[0] = val_float(call_float(uf->fn, ca));
            return 1;
        case '$':
        {
            const char *s = (const char *)(intptr_t)call_int(uf->fn, ca);
            args[0] = bb_ret_str(vm, s ? s : "");
            return 1;
        }
        case '%':
            args[0] = val_int(call_int(uf->fn, ca));
            return 1;
        default:
            call_int(uf->fn, ca);
            return 0;
        }
#endif
    }

    /* The thunk table: userlib slot N is served by userfunc_thunk<N>. */
    enum { MAX_USERFUNCS = 256 };

    template <int N>
    static int userfunc_thunk(VM *vm, Value *args, int nargs)
    {
        return call_userfunc(vm, args, nargs, N);
    }

    template <int N>
    struct ThunkTable
    {
        static void fill(NativeFn *table)
        {
            ThunkTable<N - 1>::fill(table);
            table[N - 1] = &userfunc_thunk<N - 1>;
        }
    };
    template <>
    struct ThunkTable<0>
    {
        static void fill(NativeFn *) {}
    };

    static NativeFn thunk_for(int index)
    {
        static NativeFn table[MAX_USERFUNCS];
        static bool filled = false;
        if (!filled)
        {
            ThunkTable<MAX_USERFUNCS>::fill(table);
            filled = true;
        }
        return (index >= 0 && index < MAX_USERFUNCS) ? table[index] : 0;
    }

    /* =========================================================
    ** .decls parsing
    ** ========================================================= */

    static string trim_ws(const string &s)
    {
        size_t a = 0, b = s.size();
        while (a < b && isspace((unsigned char)s[a])) ++a;
        while (b > a && isspace((unsigned char)s[b - 1])) --b;
        return s.substr(a, b - a);
    }

    static void fail(char *err, int err_len, const char *fmt, ...)
    {
        if (!err || err_len <= 0) return;
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, (size_t)err_len, fmt, ap);
        va_end(ap);
    }

    /* A declaration line:
         Name%(a%, b#, c$) : "exported_name"
       The alias is optional; without it the Blitz name is the symbol. */
    static bool parse_decl(const string &line, UserFunc *uf, string *alias,
                           char *err, int err_len)
    {
        size_t open = line.find('(');
        if (open == string::npos)
        {
            fail(err, err_len, "expected '(' in declaration: %s", line.c_str());
            return false;
        }
        string head = trim_ws(line.substr(0, open));
        if (head.empty())
        {
            fail(err, err_len, "missing function name: %s", line.c_str());
            return false;
        }
        uf->ret = 0;
        char tag = head[head.size() - 1];
        if (tag == '%' || tag == '#' || tag == '$')
        {
            uf->ret = tag;
            head = trim_ws(head.substr(0, head.size() - 1));
        }
        uf->name = head;

        size_t close = line.find(')', open);
        if (close == string::npos)
        {
            fail(err, err_len, "expected ')' in declaration: %s", line.c_str());
            return false;
        }
        string plist = line.substr(open + 1, close - open - 1);
        size_t at = 0;
        while (at < plist.size())
        {
            size_t comma = plist.find(',', at);
            if (comma == string::npos) comma = plist.size();
            string p = trim_ws(plist.substr(at, comma - at));
            at = comma + 1;
            if (p.empty()) continue;
            char pt = p[p.size() - 1];
            uf->params += (pt == '#' || pt == '$') ? pt : '%';
        }
        if ((int)uf->params.size() > MAX_ARGS)
        {
            fail(err, err_len, "%s: more than %d parameters", uf->name.c_str(), MAX_ARGS);
            return false;
        }

        *alias = uf->name;
        size_t colon = line.find(':', close);
        if (colon != string::npos)
        {
            string a = trim_ws(line.substr(colon + 1));
            if (a.size() >= 2 && a[0] == '"' && a[a.size() - 1] == '"')
                a = a.substr(1, a.size() - 2);
            if (!a.empty()) *alias = a;
        }
        return true;
    }

    /* The signature string BBRuntime::registerCommands expects:
       return tag, name, then a tag+name per parameter. */
    static string signature_of(const UserFunc &uf)
    {
        string sig;
        if (uf.ret) sig += uf.ret;
        sig += uf.name;
        for (size_t k = 0; k < uf.params.size(); ++k)
        {
            char pname[16];
            snprintf(pname, sizeof(pname), "a%d", (int)k);
            sig += uf.params[k];
            sig += pname;
        }
        return sig;
    }

    static bool load_decls_file(VM *vm, BBRuntime *rt, const string &path,
                                char *err, int err_len)
    {
        const Backend &b = vm->backend();
        if (!b.open || !b.read || !b.size || !b.close) return true; /* no file access */

        ZenFile f = b.open(path.c_str(), FILE_READ, b.userdata);
        if (!f) return true; /* nothing to load */
        int64_t len = b.size(f, b.userdata);
        string text;
        if (len > 0)
        {
            text.resize((size_t)len);
            int64_t got = b.read(f, &text[0], len, b.userdata);
            text.resize(got > 0 ? (size_t)got : 0);
        }
        b.close(f, b.userdata);

        void *lib = 0;
        string libname;
        size_t at = 0;
        while (at <= text.size())
        {
            size_t nl = text.find('\n', at);
            if (nl == string::npos) nl = text.size();
            string line = trim_ws(text.substr(at, nl - at));
            at = nl + 1;

            if (line.empty() || line[0] == ';' || line[0] == '#') continue;

            if (line.size() > 4 && bb_tolower(line.substr(0, 4)) == ".lib")
            {
                libname = trim_ws(line.substr(4));
                if (libname.size() >= 2 && libname[0] == '"' && libname[libname.size() - 1] == '"')
                    libname = libname.substr(1, libname.size() - 2);
                /* Relative names resolve next to the .decls file, which is
                   where Blitz3D kept the DLLs too. */
                string full = libname;
                if (libname.find('/') == string::npos && libname.find('\\') == string::npos)
                {
                    size_t slash = path.find_last_of("/\\");
                    if (slash != string::npos) full = path.substr(0, slash + 1) + libname;
                }
                lib = b.lib_open ? b.lib_open(full.c_str(), b.userdata) : 0;
                if (!lib)
                {
                    char msg[512];
                    snprintf(msg, sizeof(msg), "userlib '%s' could not be loaded; its functions "
                             "will report an error when called", libname.c_str());
                    backend_log(b, LOG_WARN, msg);
                }
                continue;
            }

            if (libname.empty())
            {
                fail(err, err_len, "%s: declaration before any .lib", path.c_str());
                return false;
            }
            if ((int)g_userfuncs.size() >= MAX_USERFUNCS)
            {
                fail(err, err_len, "too many userlib functions (max %d)", MAX_USERFUNCS);
                return false;
            }

            UserFunc *uf = new UserFunc();
            string alias;
            if (!parse_decl(line, uf, &alias, err, err_len))
            {
                delete uf;
                return false;
            }
            uf->libname = libname;
            uf->fn = (lib && b.lib_symbol) ? b.lib_symbol(lib, alias.c_str(), b.userdata) : 0;
            if (lib && !uf->fn)
            {
                char msg[512];
                snprintf(msg, sizeof(msg), "userlib '%s': symbol '%s' not found",
                         libname.c_str(), alias.c_str());
                backend_log(b, LOG_WARN, msg);
            }

            int index = (int)g_userfuncs.size();
            g_userfuncs.push_back(uf);
            BBCommand cmd;
            string sig = signature_of(*uf);
            cmd.sig = sig.c_str();
            cmd.fn = thunk_for(index);
            rt->registerCommands(&cmd, 1);
        }
        return true;
    }

    bool load_userlibs(VM *vm, BBRuntime *rt, const string &dir, char *err, int err_len)
    {
        const Backend &b = vm->backend();
        if (!b.open_dir || !b.next_file || !b.close_dir) return true;

        ZenDir d = b.open_dir(dir.c_str(), b.userdata);
        if (!d) return true; /* no userlibs directory: nothing to do */

        vector<string> decls;
        for (const char *name = b.next_file(d, b.userdata); name; name = b.next_file(d, b.userdata))
        {
            string n = name;
            if (n.size() > 6 && bb_tolower(n.substr(n.size() - 6)) == ".decls")
                decls.push_back(dir + "/" + n);
        }
        b.close_dir(d, b.userdata);

        for (size_t k = 0; k < decls.size(); ++k)
            if (!load_decls_file(vm, rt, decls[k], err, err_len))
                return false;
        return true;
    }
}
