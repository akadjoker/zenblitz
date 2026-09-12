/* =========================================================
** builtin_math.cpp — "math" module for Zen
**
** Provides: random, seed, min, max, clamp, lerp, sign, map_range
** Note: sin, cos, tan, sqrt, pow, abs, floor, ceil, etc. are OPCODES
**       (OP_SIN, OP_COS...) — zero overhead, always available.
** ========================================================= */

#include "module.h"
#include "vm.h"
#include <cstring>

namespace zen
{

    /* =========================================================
    ** xoshiro256** PRNG (from Lua 5.5, public domain)
    ** 32 bytes state, ~1.5ns/call, excellent quality
    ** ========================================================= */

    static uint64_t rng_state[4] = {
        0x12345678deadbeefULL,
        0xabcdef0123456789ULL,
        0x9876543210fedcbaULL,
        0xfedcba9876543210ULL};

    static inline uint64_t rotl64(uint64_t x, int n)
    {
        return (x << n) | (x >> (64 - n));
    }

    static uint64_t rng_next(void)
    {
        uint64_t s0 = rng_state[0], s1 = rng_state[1];
        uint64_t s2 = rng_state[2] ^ s0;
        uint64_t s3 = rng_state[3] ^ s1;
        uint64_t res = rotl64(s1 * 5, 7) * 9;
        rng_state[0] = s0 ^ s3;
        rng_state[1] = s1 ^ s2;
        rng_state[2] = s2 ^ (s1 << 17);
        rng_state[3] = rotl64(s3, 45);
        return res;
    }

    /* [0.0, 1.0) */
    static inline double rng_double(void)
    {
        return (double)(rng_next() >> 11) * (1.0 / ((uint64_t)1 << 53));
    }

    /* Seed from a single 64-bit value (SplitMix64 to fill state) */
    static void rng_seed(uint64_t seed)
    {
        for (int i = 0; i < 4; i++)
        {
            seed += 0x9e3779b97f4a7c15ULL;
            uint64_t z = seed;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
            z = z ^ (z >> 31);
            rng_state[i] = z;
        }
    }

    /* =========================================================
    ** Native functions
    ** ========================================================= */

    /* random() → [0,1)
    ** random(max) → int [0, max]
    ** random(min, max) → int [min, max] */
    static int nat_random(VM *vm, Value *args, int nargs)
    {
        if (nargs == 0)
        {
            args[0] = val_float(rng_double());
            return 1;
        }
        if (nargs == 1)
        {
            int64_t hi = to_integer(args[0]);
            if (hi <= 0)
            {
                args[0] = val_int(0);
                return 1;
            }
            args[0] = val_int((int64_t)(rng_next() % (uint64_t)(hi + 1)));
            return 1;
        }
        /* nargs >= 2 */
        int64_t lo = to_integer(args[0]);
        int64_t hi = to_integer(args[1]);
        if (lo > hi)
        {
            int64_t tmp = lo;
            lo = hi;
            hi = tmp;
        }
        uint64_t range = (uint64_t)(hi - lo) + 1;
        args[0] = val_int(lo + (int64_t)(rng_next() % range));
        return 1;
    }

    /* seed(n) — set PRNG seed */
    static int nat_seed(VM *vm, Value *args, int nargs)
    {
        if (nargs >= 1)
            rng_seed((uint64_t)to_integer(args[0]));
        return 0;
    }

    /* min(a, b) */
    static int nat_min(VM *vm, Value *args, int nargs)
    {
        if (is_int(args[0]) && is_int(args[1]))
        {
            int64_t a = to_integer(args[0]), b = to_integer(args[1]);
            args[0] = val_int(a < b ? a : b);
        }
        else
        {
            double a = to_number(args[0]), b = to_number(args[1]);
            args[0] = val_float(a < b ? a : b);
        }
        return 1;
    }

    /* max(a, b) */
    static int nat_max(VM *vm, Value *args, int nargs)
    {
        if (is_int(args[0]) && is_int(args[1]))
        {
            int64_t a = to_integer(args[0]), b = to_integer(args[1]);
            args[0] = val_int(a > b ? a : b);
        }
        else
        {
            double a = to_number(args[0]), b = to_number(args[1]);
            args[0] = val_float(a > b ? a : b);
        }
        return 1;
    }

    /* clamp(value, lo, hi) */
    static int nat_clamp(VM *vm, Value *args, int nargs)
    {
        if (is_int(args[0]) && is_int(args[1]) && is_int(args[2]))
        {
            int64_t v = to_integer(args[0]);
            int64_t lo = to_integer(args[1]);
            int64_t hi = to_integer(args[2]);
            if (v < lo) v = lo;
            if (v > hi) v = hi;
            args[0] = val_int(v);
        }
        else
        {
            double v = to_number(args[0]);
            double lo = to_number(args[1]);
            double hi = to_number(args[2]);
            if (v < lo) v = lo;
            if (v > hi) v = hi;
            args[0] = val_float(v);
        }
        return 1;
    }

    /* lerp(a, b, t) — linear interpolation */
    static int nat_lerp(VM *vm, Value *args, int nargs)
    {
        double a = to_number(args[0]);
        double b = to_number(args[1]);
        double t = to_number(args[2]);
        args[0] = val_float(a + (b - a) * t);
        return 1;
    }

    /* sign(x) → -1, 0, 1 */
    static int nat_sign(VM *vm, Value *args, int nargs)
    {
        if (is_int(args[0]))
        {
            int64_t v = to_integer(args[0]);
            args[0] = val_int(v > 0 ? 1 : (v < 0 ? -1 : 0));
        }
        else
        {
            double v = to_number(args[0]);
            args[0] = val_float(v > 0.0 ? 1.0 : (v < 0.0 ? -1.0 : 0.0));
        }
        return 1;
    }

    /* map_range(value, in_min, in_max, out_min, out_max) */
    static int nat_map_range(VM *vm, Value *args, int nargs)
    {
        double v = to_number(args[0]);
        double in_min = to_number(args[1]);
        double in_max = to_number(args[2]);
        double out_min = to_number(args[3]);
        double out_max = to_number(args[4]);
        double t = (v - in_min) / (in_max - in_min);
        args[0] = val_float(out_min + (out_max - out_min) * t);
        return 1;
    }

    /* =========================================================
    ** Module definition
    ** ========================================================= */

    static const NativeReg math_functions[] = {
        {"random", nat_random, -1},
        {"seed", nat_seed, 1},
        {"min", nat_min, 2},
        {"max", nat_max, 2},
        {"clamp", nat_clamp, 3},
        {"lerp", nat_lerp, 3},
        {"sign", nat_sign, 1},
        {"map_range", nat_map_range, 5},
        {nullptr, nullptr, 0}};

#define FVAL(x) {VAL_FLOAT, {.number = (x)}}

    static const NativeConst math_constants[] = {
        {"PI",  FVAL(3.14159265358979323846)},
        {"PI2", FVAL(1.57079632679489661923)},
        {"TAU", FVAL(6.28318530717958647692)},
        {"E",   FVAL(2.71828182845904523536)},
        {"INF", FVAL(1e308 * 10)},
        {nullptr, {VAL_NIL, {.integer = 0}}}};

#undef FVAL

    const NativeLib zen_lib_math = {
        "math",
        math_functions,
        8,
        math_constants,
        5,
        nullptr};

} /* namespace zen */
