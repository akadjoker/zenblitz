#ifndef ZEN_MODULE_H
#define ZEN_MODULE_H

#include "value.h"

namespace zen
{
    class VM; /* forward */

    typedef int (*NativeFn)(VM *vm, Value *args, int nargs);

    struct NativeReg
    {
        const char *name;
        NativeFn fn;
        int arity; /* -1 = variadic */
    };

    struct NativeConst
    {
        const char *name;
        Value value;
    };

    /* Called once when a lib is opened — use to register native structs/classes.
    ** Called before functions/constants are put in globals, so types are available
    ** when init_fn returns. VM is fully alive at this point. */
    typedef void (*NativeLibInitFn)(VM *vm);

    struct NativeLib
    {
        const char *name;            /* "math", "os" */
        const NativeReg *functions;  /* null-terminated array */
        int num_functions;
        const NativeConst *constants; /* null-terminated array (can be nullptr) */
        int num_constants;
        NativeLibInitFn init_fn;     /* optional — register structs/classes here */
    };

    /* Builtin library openers */
    extern const NativeLib zen_lib_base;
    extern const NativeLib zen_lib_math;
    extern const NativeLib zen_lib_os;
    extern const NativeLib zen_lib_time;
    extern const NativeLib zen_lib_fs;
    extern const NativeLib zen_lib_path;
    extern const NativeLib zen_lib_file;
#ifdef ZEN_ENABLE_REGEX
    extern const NativeLib zen_lib_re;
#endif
#ifdef ZEN_ENABLE_ZIP
    extern const NativeLib zen_lib_zip;
#endif
#ifdef ZEN_ENABLE_NET
    extern const NativeLib zen_lib_net;
#endif
#ifdef ZEN_ENABLE_HTTP
    extern const NativeLib zen_lib_http;
#endif
#ifdef ZEN_ENABLE_CRYPTO
    extern const NativeLib zen_lib_crypto;
#endif
#ifdef ZEN_ENABLE_JSON
    extern const NativeLib zen_lib_json;
#endif
#ifdef ZEN_ENABLE_UTF8
    extern const NativeLib zen_lib_utf8;
#endif
    extern const NativeLib zen_lib_easing;
    extern const NativeLib zen_lib_base64;
    extern const NativeLib zen_lib_csv;
    extern const NativeLib zen_lib_xml;
    extern const NativeLib zen_lib_ini;
    extern const NativeLib zen_lib_log;
#ifdef ZEN_ENABLE_GIF
    extern const NativeLib zen_lib_gif;
#endif
#ifdef ZEN_ENABLE_CANVAS
    extern const NativeLib zen_lib_canvas;
#endif

} /* namespace zen */

#endif /* ZEN_MODULE_H */
