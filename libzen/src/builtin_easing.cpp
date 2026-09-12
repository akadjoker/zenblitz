/* =========================================================
** builtin_easing.cpp — "easing" module for Zen
**
** All 31 standard easing functions from https://easings.net/
** Each takes a float t in [0,1] and returns float [0,1].
**
** Usage: import easing; var y = easing.out_bounce(0.5);
** ========================================================= */

#include "module.h"
#include "vm.h"
#include <cmath>

namespace zen
{

    static const double kPi = 3.14159265358979323846;

    /* ===== Pure math easing functions ===== */

    static double ease_linear(double x) { return x; }

    static double ease_in_sine(double x) { return 1.0 - cos(x * kPi / 2.0); }
    static double ease_out_sine(double x) { return sin(x * kPi / 2.0); }
    static double ease_in_out_sine(double x) { return -(cos(kPi * x) - 1.0) / 2.0; }

    static double ease_in_quad(double x) { return x * x; }
    static double ease_out_quad(double x) { return 1.0 - (1.0 - x) * (1.0 - x); }
    static double ease_in_out_quad(double x)
    {
        return x < 0.5 ? 2.0 * x * x : 1.0 - pow(-2.0 * x + 2.0, 2.0) / 2.0;
    }

    static double ease_in_cubic(double x) { return x * x * x; }
    static double ease_out_cubic(double x) { return 1.0 - pow(1.0 - x, 3.0); }
    static double ease_in_out_cubic(double x)
    {
        return x < 0.5 ? 4.0 * x * x * x : 1.0 - pow(-2.0 * x + 2.0, 3.0) / 2.0;
    }

    static double ease_in_quart(double x) { return pow(x, 4.0); }
    static double ease_out_quart(double x) { return 1.0 - pow(1.0 - x, 4.0); }
    static double ease_in_out_quart(double x)
    {
        return x < 0.5 ? 8.0 * pow(x, 4.0) : 1.0 - pow(-2.0 * x + 2.0, 4.0) / 2.0;
    }

    static double ease_in_quint(double x) { return pow(x, 5.0); }
    static double ease_out_quint(double x) { return 1.0 - pow(1.0 - x, 5.0); }
    static double ease_in_out_quint(double x)
    {
        return x < 0.5 ? 16.0 * pow(x, 5.0) : 1.0 - pow(-2.0 * x + 2.0, 5.0) / 2.0;
    }

    static double ease_in_expo(double x)
    {
        return x == 0.0 ? 0.0 : pow(2.0, 10.0 * x - 10.0);
    }
    static double ease_out_expo(double x)
    {
        return x == 1.0 ? 1.0 : 1.0 - pow(2.0, -10.0 * x);
    }
    static double ease_in_out_expo(double x)
    {
        if (x == 0.0) return 0.0;
        if (x == 1.0) return 1.0;
        return x < 0.5 ? pow(2.0, 20.0 * x - 10.0) / 2.0
                       : (2.0 - pow(2.0, -20.0 * x + 10.0)) / 2.0;
    }

    static double ease_in_circ(double x) { return 1.0 - sqrt(1.0 - x * x); }
    static double ease_out_circ(double x) { return sqrt(1.0 - (x - 1.0) * (x - 1.0)); }
    static double ease_in_out_circ(double x)
    {
        return x < 0.5
            ? (1.0 - sqrt(1.0 - pow(2.0 * x, 2.0))) / 2.0
            : (sqrt(1.0 - pow(-2.0 * x + 2.0, 2.0)) + 1.0) / 2.0;
    }

    static double ease_in_back(double x)
    {
        const double c1 = 1.70158;
        const double c3 = c1 + 1.0;
        return c3 * x * x * x - c1 * x * x;
    }
    static double ease_out_back(double x)
    {
        const double c1 = 1.70158;
        const double c3 = c1 + 1.0;
        return 1.0 + c3 * pow(x - 1.0, 3.0) + c1 * pow(x - 1.0, 2.0);
    }
    static double ease_in_out_back(double x)
    {
        const double c1 = 1.70158;
        const double c2 = c1 * 1.525;
        return x < 0.5
            ? (pow(2.0 * x, 2.0) * ((c2 + 1.0) * 2.0 * x - c2)) / 2.0
            : (pow(2.0 * x - 2.0, 2.0) * ((c2 + 1.0) * (x * 2.0 - 2.0) + c2) + 2.0) / 2.0;
    }

    static double ease_in_elastic(double x)
    {
        const double c4 = (2.0 * kPi) / 3.0;
        if (x == 0.0) return 0.0;
        if (x == 1.0) return 1.0;
        return -pow(2.0, 10.0 * x - 10.0) * sin((x * 10.0 - 10.75) * c4);
    }
    static double ease_out_elastic(double x)
    {
        const double c4 = (2.0 * kPi) / 3.0;
        if (x == 0.0) return 0.0;
        if (x == 1.0) return 1.0;
        return pow(2.0, -10.0 * x) * sin((x * 10.0 - 0.75) * c4) + 1.0;
    }
    static double ease_in_out_elastic(double x)
    {
        const double c5 = (2.0 * kPi) / 4.5;
        if (x == 0.0) return 0.0;
        if (x == 1.0) return 1.0;
        return x < 0.5
            ? -(pow(2.0, 20.0 * x - 10.0) * sin((20.0 * x - 11.125) * c5)) / 2.0
            : (pow(2.0, -20.0 * x + 10.0) * sin((20.0 * x - 11.125) * c5)) / 2.0 + 1.0;
    }

    static double ease_out_bounce(double x)
    {
        const double n1 = 7.5625;
        const double d1 = 2.75;
        if (x < 1.0 / d1)
            return n1 * x * x;
        if (x < 2.0 / d1) { x -= 1.5 / d1; return n1 * x * x + 0.75; }
        if (x < 2.5 / d1) { x -= 2.25 / d1; return n1 * x * x + 0.9375; }
        x -= 2.625 / d1;
        return n1 * x * x + 0.984375;
    }
    static double ease_in_bounce(double x) { return 1.0 - ease_out_bounce(1.0 - x); }
    static double ease_in_out_bounce(double x)
    {
        return x < 0.5
            ? (1.0 - ease_out_bounce(1.0 - 2.0 * x)) / 2.0
            : (1.0 + ease_out_bounce(2.0 * x - 1.0)) / 2.0;
    }

    /* ===== Native wrappers (macro for brevity) ===== */

#define DEF_EASE(zen_name, fn)                                       \
    static int nat_ease_##zen_name(VM *vm, Value *args, int nargs)   \
    {                                                                \
        (void)vm; (void)nargs;                                       \
        double t = is_float(args[0]) ? args[0].as.number             \
                 : is_int(args[0])   ? (double)args[0].as.integer    \
                 : 0.0;                                              \
        args[0] = val_float(fn(t));                                  \
        return 1;                                                    \
    }

    DEF_EASE(linear, ease_linear)
    DEF_EASE(in_sine, ease_in_sine)
    DEF_EASE(out_sine, ease_out_sine)
    DEF_EASE(in_out_sine, ease_in_out_sine)
    DEF_EASE(in_quad, ease_in_quad)
    DEF_EASE(out_quad, ease_out_quad)
    DEF_EASE(in_out_quad, ease_in_out_quad)
    DEF_EASE(in_cubic, ease_in_cubic)
    DEF_EASE(out_cubic, ease_out_cubic)
    DEF_EASE(in_out_cubic, ease_in_out_cubic)
    DEF_EASE(in_quart, ease_in_quart)
    DEF_EASE(out_quart, ease_out_quart)
    DEF_EASE(in_out_quart, ease_in_out_quart)
    DEF_EASE(in_quint, ease_in_quint)
    DEF_EASE(out_quint, ease_out_quint)
    DEF_EASE(in_out_quint, ease_in_out_quint)
    DEF_EASE(in_expo, ease_in_expo)
    DEF_EASE(out_expo, ease_out_expo)
    DEF_EASE(in_out_expo, ease_in_out_expo)
    DEF_EASE(in_circ, ease_in_circ)
    DEF_EASE(out_circ, ease_out_circ)
    DEF_EASE(in_out_circ, ease_in_out_circ)
    DEF_EASE(in_back, ease_in_back)
    DEF_EASE(out_back, ease_out_back)
    DEF_EASE(in_out_back, ease_in_out_back)
    DEF_EASE(in_elastic, ease_in_elastic)
    DEF_EASE(out_elastic, ease_out_elastic)
    DEF_EASE(in_out_elastic, ease_in_out_elastic)
    DEF_EASE(in_bounce, ease_in_bounce)
    DEF_EASE(out_bounce, ease_out_bounce)
    DEF_EASE(in_out_bounce, ease_in_out_bounce)

#undef DEF_EASE

    /* ===== Registration ===== */

    static const NativeReg easing_functions[] = {
        {"linear", nat_ease_linear, 1},
        {"in_sine", nat_ease_in_sine, 1},
        {"out_sine", nat_ease_out_sine, 1},
        {"in_out_sine", nat_ease_in_out_sine, 1},
        {"in_quad", nat_ease_in_quad, 1},
        {"out_quad", nat_ease_out_quad, 1},
        {"in_out_quad", nat_ease_in_out_quad, 1},
        {"in_cubic", nat_ease_in_cubic, 1},
        {"out_cubic", nat_ease_out_cubic, 1},
        {"in_out_cubic", nat_ease_in_out_cubic, 1},
        {"in_quart", nat_ease_in_quart, 1},
        {"out_quart", nat_ease_out_quart, 1},
        {"in_out_quart", nat_ease_in_out_quart, 1},
        {"in_quint", nat_ease_in_quint, 1},
        {"out_quint", nat_ease_out_quint, 1},
        {"in_out_quint", nat_ease_in_out_quint, 1},
        {"in_expo", nat_ease_in_expo, 1},
        {"out_expo", nat_ease_out_expo, 1},
        {"in_out_expo", nat_ease_in_out_expo, 1},
        {"in_circ", nat_ease_in_circ, 1},
        {"out_circ", nat_ease_out_circ, 1},
        {"in_out_circ", nat_ease_in_out_circ, 1},
        {"in_back", nat_ease_in_back, 1},
        {"out_back", nat_ease_out_back, 1},
        {"in_out_back", nat_ease_in_out_back, 1},
        {"in_elastic", nat_ease_in_elastic, 1},
        {"out_elastic", nat_ease_out_elastic, 1},
        {"in_out_elastic", nat_ease_in_out_elastic, 1},
        {"in_bounce", nat_ease_in_bounce, 1},
        {"out_bounce", nat_ease_out_bounce, 1},
        {"in_out_bounce", nat_ease_in_out_bounce, 1},
    };

    const NativeLib zen_lib_easing = {
        "easing",
        easing_functions,
        31,
        nullptr,
        0,
        nullptr
    };

} /* namespace zen */
