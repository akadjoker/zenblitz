/*
** xfile_compat.h — the small, Windows-free substitute for the handful of
** Windows types and error codes xfile_parse.c/xfile_mszip.c need. See
** VERSION for what this replaces and why.
**
** Included from both the vendored .c files (compiled as C, matching
** upstream) and this engine's C++ LoaderX.cpp, so every helper here has
** to compile as either.
*/
#ifndef XFILE_COMPAT_H
#define XFILE_COMPAT_H

#ifdef __cplusplus
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cctype>
#define XFILE_BOOL_TYPE int
#else
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#define XFILE_BOOL_TYPE int
#endif

typedef XFILE_BOOL_TYPE BOOL;
typedef unsigned char BYTE, *LPBYTE;
typedef unsigned short WORD;
typedef uint32_t DWORD;
typedef unsigned long ULONG;
typedef long LONG;
typedef void *LPVOID;
typedef const char *LPCSTR;
typedef char *LPSTR;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

typedef struct
{
    DWORD Data1;
    WORD Data2;
    WORD Data3;
    BYTE Data4[8];
} GUID;

static inline int xfile_guid_equal(const GUID *a, const GUID *b)
{
    return memcmp(a, b, sizeof(GUID)) == 0;
}

#ifdef __cplusplus
inline bool operator==(const GUID &a, const GUID &b) { return xfile_guid_equal(&a, &b) != 0; }
inline bool operator!=(const GUID &a, const GUID &b) { return !(a == b); }
#endif

/* parsing.c returns HRESULT from parse_header only; the four codes it
   actually produces (see VERSION). Success is XFILE_OK, never 0 by
   accident - callers check against it explicitly, same as SUCCEEDED(hr). */
typedef int XFILE_HRESULT;
enum
{
    XFILE_OK = 0,
    XFILE_ERR_BADALLOC,
    XFILE_ERR_BADFILETYPE,
    XFILE_ERR_BADFILEVERSION,
    XFILE_ERR_BADFILEFLOATSIZE
};

/* case-insensitive compare: portable stand-ins for Windows' stricmp/
   _strnicmp (strcasecmp/strncasecmp are POSIX, not standard C or C++) */
static inline int xfile_stricmp(const char *a, const char *b)
{
    while (*a && *b)
    {
        const int ca = tolower((unsigned char)*a);
        const int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        ++a; ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static inline int xfile_strnicmp(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i)
    {
        const int ca = tolower((unsigned char)a[i]);
        const int cb = tolower((unsigned char)b[i]);
        if (ca != cb) return ca - cb;
        if (a[i] == '\0') break;
    }
    return 0;
}

#endif
