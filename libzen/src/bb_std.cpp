#include "bb_std.h"
#include <cctype>
#include <cfenv>

namespace bb
{
    string bb_itoa(long long n)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lld", n);
        return buf;
    }

    /*
    ** Port of Blitz3D's ftoa(): 6 significant digits, decimal notation for
    ** exponents in [-3, 8], trailing zeros trimmed but at least one decimal
    ** digit kept ("10.0", "1.5", "0.333333").
    */
    string bb_ftoa(double n)
    {
        if (std::isnan(n)) return "NaN";
        if (std::isinf(n)) return n > 0 ? "Infinity" : "-Infinity";

        const int digits = 6;
        const int eNeg = -4, ePos = 8;

        char buf[64];
        string t;
        int dec;
        int sign = n < 0;
        double a = std::fabs(n);

        if (a == 0.0)
        {
            t = "000000";
            dec = 0;
        }
        else
        {
            snprintf(buf, sizeof(buf), "%.*e", digits - 1, a); /* d.ddddde±XX */
            t += buf[0];
            int i = 2;
            for (; buf[i] && buf[i] != 'e'; ++i) t += buf[i];
            int e = atoi(buf + i + 1);
            dec = e + 1;
        }

        if (dec <= eNeg + 1 || dec > ePos)
        {
            snprintf(buf, sizeof(buf), "%.*g", digits, n);
            return buf;
        }

        if (dec <= 0)
        {
            t = "0." + string(-dec, '0') + t;
            dec = 1;
        }
        else if (dec < digits)
        {
            t = t.substr(0, dec) + "." + t.substr(dec);
        }
        else
        {
            t = t + string(dec - digits, '0') + ".0";
            dec += dec - digits;
        }

        int dp1 = dec + 1, p = (int)t.length();
        while (--p > dp1 && t[p] == '0') {}
        t = string(t, 0, ++p);
        return sign ? "-" + t : t;
    }

    string bb_tolower(const string &s)
    {
        string t = s;
        for (size_t k = 0; k < t.size(); ++k) t[k] = (char)tolower((unsigned char)t[k]);
        return t;
    }

    string bb_toupper(const string &s)
    {
        string t = s;
        for (size_t k = 0; k < t.size(); ++k) t[k] = (char)toupper((unsigned char)t[k]);
        return t;
    }

    long long bb_atoi(const string &s)
    {
        return strtoll(s.c_str(), nullptr, 10);
    }

    double bb_atof(const string &s)
    {
        return strtod(s.c_str(), nullptr);
    }

    long long bb_round(double d)
    {
        /* x86 fistp with round-to-nearest: ties go to even */
        return (long long)std::nearbyint(d);
    }
}
