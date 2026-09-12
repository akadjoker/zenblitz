/*
** bb_std.h — common includes and helpers for the Blitz Basic frontend.
**
** The frontend is a port of the original Blitz3D compiler (zlib licence,
** Blitz Research Ltd) with the x86 code generator replaced by a Zen
** bytecode emitter. Only standard C++ is used here — no Win32.
*/
#ifndef BB_STD_H
#define BB_STD_H

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <list>
#include <iostream>
#include <fstream>
#include <sstream>

namespace bb
{
    using std::string;
    using std::vector;
    using std::map;
    using std::set;
    using std::list;

    /* Compile error. pos = (row<<16)|col of the offending token. */
    struct Ex
    {
        string ex;
        int pos;
        string file;
        Ex(const string &e) : ex(e), pos(-1) {}
        Ex(const string &e, int p, const string &f) : ex(e), pos(p), file(f) {}
    };

    /* Lazy auto_ptr (from the original stdutil) */
    template <class T>
    class a_ptr
    {
    public:
        a_ptr(T *t = 0) : t(t) {}
        ~a_ptr() { delete t; }
        a_ptr &operator=(T *tt) { t = tt; return *this; }
        T &operator*() const { return *t; }
        T *operator->() const { return t; }
        operator T *() const { return t; }
        T *release() { T *tt = t; t = 0; return tt; }
    private:
        T *t;
        a_ptr(const a_ptr &);
        a_ptr &operator=(const a_ptr &);
    };

    string bb_itoa(long long n);
    string bb_ftoa(double n);       /* Blitz-style float formatting (6 significant digits) */
    string bb_tolower(const string &s);
    string bb_toupper(const string &s);
    long long bb_atoi(const string &s);
    double bb_atof(const string &s);
    long long bb_round(double d);   /* Blitz float->int: round to nearest (even) */

} /* namespace bb */

#endif
