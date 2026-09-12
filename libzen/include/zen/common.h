#ifndef ZEN_COMMON_H
#define ZEN_COMMON_H

/*
** common.h — Base types and VM configuration.
*/

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>

/* Branch hints and unreachable markers: GCC and Clang builtins, with no
** equivalent on MSVC, where they compile away to nothing (__assume(0) for
** unreachable, which is the same promise). The interpreter's hot paths are
** written with these, so they have to exist everywhere it builds. */
#if defined(__GNUC__) || defined(__clang__)
#define ZEN_LIKELY(x) __builtin_expect(!!(x), 1)
#define ZEN_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define ZEN_UNREACHABLE() __builtin_unreachable()
#elif defined(_MSC_VER)
#define ZEN_LIKELY(x) (x)
#define ZEN_UNLIKELY(x) (x)
#define ZEN_UNREACHABLE() __assume(0)
#else
#define ZEN_LIKELY(x) (x)
#define ZEN_UNLIKELY(x) (x)
#define ZEN_UNREACHABLE() ((void)0)
#endif

namespace zen
{
    /* VM limits */
    constexpr int kMaxRegs = 250; /* registers per call frame */

    /* Max call depth (frames). Set per target with -DZEN_MAX_FRAMES=N. */
#ifndef ZEN_MAX_FRAMES
  #if defined(__EMSCRIPTEN__)
    #define ZEN_MAX_FRAMES 128
  #elif defined(__ANDROID__) || defined(ZEN_PLATFORM_MOBILE)
    #define ZEN_MAX_FRAMES 256
  #else
    #define ZEN_MAX_FRAMES 512
  #endif
#endif
    constexpr int kMaxFrames = ZEN_MAX_FRAMES;
    /* Register stack of the main execution context, sized to the frame cap. */
    constexpr int kMainStackSlots = kMaxFrames * 4;
    constexpr int kMaxConstants = 65536; /* constant pool per function */
    /* Globals: dynamic array, indexed by a 16-bit Bx operand. */
    constexpr int kInitGlobalCapacity = 1024;
    constexpr int kMaxGlobalsHard = 65536;
    constexpr int MAX_GLOBALS = kInitGlobalCapacity;
    constexpr size_t kGCInitThreshold = 1024 * 256; /* first GC at 256 KB */
    constexpr float kGCGrowFactor = 2.0f;

    /* Forward declarations */
    class VM;
    struct Obj;
    struct ObjString;
    struct ObjFunc;
    struct ObjNative;
    struct ObjArray;
    struct ObjBuffer;
    struct ObjStructDef;
    struct ObjStruct;
    struct Fiber;

} /* namespace zen */

#endif /* ZEN_COMMON_H */
