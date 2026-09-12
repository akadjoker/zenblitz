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

namespace zen
{

    /* VM limits */
    constexpr int kMaxRegs = 250;                   /* registers per call frame */

    /* Max call depth (frames per fiber). Bigger = deeper recursion allowed, but
    ** costs memory on EVERY fiber/process (frame array + register stack). Set
    ** per target at build time with -DZEN_MAX_FRAMES=N; otherwise a sensible
    ** platform default is used (web small, mobile medium, desktop large). */
#ifndef ZEN_MAX_FRAMES
  #if defined(__EMSCRIPTEN__)
    #define ZEN_MAX_FRAMES 128          /* web: small stacks */
  #elif defined(__ANDROID__) || defined(ZEN_PLATFORM_MOBILE)
    #define ZEN_MAX_FRAMES 256          /* mobile */
  #else
    #define ZEN_MAX_FRAMES 512          /* desktop */
  #endif
#endif
    constexpr int kMaxFrames = ZEN_MAX_FRAMES;
    /* Register stack for the main/script fiber, sized to the frame cap so the
    ** full call depth is usable for register-heavy frames. */
    constexpr int kMainStackSlots = kMaxFrames * 4;
    constexpr int kMaxFiberDepth = 64;              /* max nested fiber resumes (C stack) */
    constexpr int kMaxParseDepth = 400;             /* max nested expr/stmt (guards parser C stack) */
    constexpr int kMaxFuncNesting = 64;             /* max nested function defs: each level holds a
                                                       ~25KB CompilerState frame on the C stack */
    constexpr int kMaxConstants = 65536;            /* constant pool per function */
    constexpr int kOperatorSlotCount = 15;          /* fixed object operator slots */
    /* Globals are stored in a dynamic array (grows on demand).
    ** kInitGlobalCapacity = how big the table is allocated up front.
    ** kMaxGlobalsHard   = absolute ceiling (OP_GETGLOBAL/SETGLOBAL encode
    **                     the index as a 16-bit Bx field). */
    constexpr int kInitGlobalCapacity = 1024;
    constexpr int kMaxGlobalsHard = 65536;
    /* Backwards-compat alias: previously this was the hard cap. Kept so
    ** external code that referenced it still compiles. New code should use
    ** kMaxGlobalsHard for the ceiling, kInitGlobalCapacity for sizing. */
    constexpr int MAX_GLOBALS = kInitGlobalCapacity;
    constexpr size_t kGCInitThreshold = 1024 * 256; /* first GC at 256 KB */
    constexpr float kGCGrowFactor = 2.0f;

    /* Forward declarations */
    class VM;
    struct Obj;
    struct ObjString;
    struct ObjFunc;
    struct ObjUpvalue;
    struct ObjClosure;
    struct ObjFiber;
    struct ObjArray;
    struct ObjMap;
    struct ObjSet;
    struct ObjStructDef;
    struct ObjStruct;
    struct ObjClass;
    struct ObjInstance;
    struct ObjProcess;

} /* namespace zen */

#endif /* ZEN_COMMON_H */
