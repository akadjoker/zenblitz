/* =========================================================
** builtin_base64.cpp — "base64" module for Zen
**
** Functions:
**   base64.encode(str) → base64-encoded string
**   base64.decode(str) → decoded string
.
** ========================================================= */

#include "module.h"
#include "vm.h"
#include <cstdlib>
#include <cstring>

namespace zen
{

    /* ===== Encoding table ===== */
    static const char b64_enc[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    /* ===== Decoding table (255 = invalid) ===== */
    static const unsigned char b64_dec[128] = {
        255,255,255,255,255,255,255,255, 255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255, 255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255, 255,255,255, 62,255,255,255, 63,
         52, 53, 54, 55, 56, 57, 58, 59,  60, 61,255,255,255,255,255,255,
        255,  0,  1,  2,  3,  4,  5,  6,   7,  8,  9, 10, 11, 12, 13, 14,
         15, 16, 17, 18, 19, 20, 21, 22,  23, 24, 25,255,255,255,255,255,
        255, 26, 27, 28, 29, 30, 31, 32,  33, 34, 35, 36, 37, 38, 39, 40,
         41, 42, 43, 44, 45, 46, 47, 48,  49, 50, 51,255,255,255,255,255,
    };

    /* ===== base64.encode(str) → string ===== */
    static int nat_b64_encode(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_string(args[0]))
        {
            vm->runtime_error("base64.encode() expects a string.");
            return -1;
        }
        ObjString *src = as_string(args[0]);
        const unsigned char *in = (const unsigned char *)src->chars;
        int inlen = src->length;

        /* Output size: ceil(inlen/3)*4 + 1 */
        int outlen = ((inlen + 2) / 3) * 4;
        char *out = (char *)malloc((size_t)outlen + 1);

        int j = 0;
        for (int i = 0; i < inlen; i += 3)
        {
            unsigned int b = (unsigned int)in[i] << 16;
            if (i + 1 < inlen) b |= (unsigned int)in[i + 1] << 8;
            if (i + 2 < inlen) b |= (unsigned int)in[i + 2];

            out[j++] = b64_enc[(b >> 18) & 0x3F];
            out[j++] = b64_enc[(b >> 12) & 0x3F];
            out[j++] = (i + 1 < inlen) ? b64_enc[(b >> 6) & 0x3F] : '=';
            out[j++] = (i + 2 < inlen) ? b64_enc[b & 0x3F] : '=';
        }
        out[j] = '\0';

        args[0] = val_obj((Obj *)vm->make_string(out, j));
        free(out);
        return 1;
    }

    /* ===== base64.decode(str) → string ===== */
    static int nat_b64_decode(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_string(args[0]))
        {
            vm->runtime_error("base64.decode() expects a string.");
            return -1;
        }
        ObjString *src = as_string(args[0]);
        const char *in = src->chars;
        int inlen = src->length;

        /* Skip trailing whitespace/newlines */
        while (inlen > 0 && (in[inlen - 1] == '\n' || in[inlen - 1] == '\r' || in[inlen - 1] == ' '))
            inlen--;

        if (inlen == 0)
        {
            args[0] = val_obj((Obj *)vm->make_string("", 0));
            return 1;
        }

        if (inlen & 3)
        {
            vm->runtime_error("base64.decode(): invalid input length.");
            return -1;
        }

        /* Output size: at most inlen*3/4 */
        int outmax = (inlen * 3) / 4;
        unsigned char *out = (unsigned char *)malloc((size_t)outmax);
        int j = 0;

        for (int i = 0; i < inlen; i += 4)
        {
            unsigned int sextet[4];
            int pad = 0;

            for (int k = 0; k < 4; k++)
            {
                unsigned char c = (unsigned char)in[i + k];
                if (c == '=')
                {
                    sextet[k] = 0;
                    pad++;
                }
                else if (c >= 128 || b64_dec[c] == 255)
                {
                    free(out);
                    vm->runtime_error("base64.decode(): invalid character at position %d.", i + k);
                    return -1;
                }
                else
                {
                    sextet[k] = b64_dec[c];
                }
            }

            unsigned int triple = (sextet[0] << 18) | (sextet[1] << 12)
                                | (sextet[2] << 6) | sextet[3];

            out[j++] = (unsigned char)((triple >> 16) & 0xFF);
            if (pad < 2) out[j++] = (unsigned char)((triple >> 8) & 0xFF);
            if (pad < 1) out[j++] = (unsigned char)(triple & 0xFF);
        }

        args[0] = val_obj((Obj *)vm->make_string((const char *)out, j));
        free(out);
        return 1;
    }

    /* ===== Registration ===== */

    static const NativeReg base64_functions[] = {
        {"encode", nat_b64_encode, 1},
        {"decode", nat_b64_decode, 1},
    };

    const NativeLib zen_lib_base64 = {
        "base64",
        base64_functions,
        2,
        nullptr,
        0,
    };

} /* namespace zen */
