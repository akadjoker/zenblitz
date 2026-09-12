#ifdef ZEN_ENABLE_CRYPTO

/* =========================================================
** builtin_crypto.cpp — "crypto" module for Zen
**
** Hashing, encoding, UUID — all pure C, zero deps.
**
** Usage:
**   import crypto;
**   print(crypto.md5("hello"));
**   print(crypto.sha256("hello"));
**   print(crypto.base64_encode("hello"));
**   print(crypto.base64_decode("aGVsbG8="));
**   print(crypto.hex_encode("hello"));
**   print(crypto.hex_decode("68656c6c6f"));
**   print(crypto.uuid4());
** ========================================================= */

#include "module.h"
#include "vm.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <ctime>

namespace zen
{

    /* =========================================================
    ** MD5 (RFC 1321) — compact implementation
    ** ========================================================= */

    static void md5_transform(uint32_t state[4], const uint8_t block[64])
    {
        static const uint32_t K[64] = {
            0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
            0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
            0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
            0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
            0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
            0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
            0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
            0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
        };
        static const uint8_t S[64] = {
            7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
            5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
            4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
            6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
        };
        static const uint8_t G[64] = {
            0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
            1,6,11,0,5,10,15,4,9,14,3,8,13,2,7,12,
            5,8,11,14,1,4,7,10,13,0,3,6,9,12,15,2,
            0,7,14,5,12,3,10,1,8,15,6,13,4,11,2,9
        };

        uint32_t M[16];
        for (int i = 0; i < 16; i++)
            M[i] = (uint32_t)block[i*4] | ((uint32_t)block[i*4+1]<<8) |
                    ((uint32_t)block[i*4+2]<<16) | ((uint32_t)block[i*4+3]<<24);

        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];

        for (int i = 0; i < 64; i++)
        {
            uint32_t f;
            if (i < 16) f = (b & c) | (~b & d);
            else if (i < 32) f = (d & b) | (~d & c);
            else if (i < 48) f = b ^ c ^ d;
            else f = c ^ (b | ~d);

            f += a + K[i] + M[G[i]];
            a = d; d = c; c = b;
            b = b + ((f << S[i]) | (f >> (32 - S[i])));
        }

        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    }

    static void md5_hash(const uint8_t *data, size_t len, uint8_t out[16])
    {
        uint32_t state[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
        size_t i;

        for (i = 0; i + 64 <= len; i += 64)
            md5_transform(state, data + i);

        uint8_t block[64];
        size_t rem = len - i;
        memcpy(block, data + i, rem);
        block[rem] = 0x80;
        memset(block + rem + 1, 0, 64 - rem - 1);

        if (rem >= 56)
        {
            md5_transform(state, block);
            memset(block, 0, 64);
        }

        uint64_t bits = (uint64_t)len * 8;
        for (int j = 0; j < 8; j++)
            block[56 + j] = (uint8_t)(bits >> (j * 8));

        md5_transform(state, block);

        for (int j = 0; j < 4; j++)
        {
            out[j*4+0] = (uint8_t)(state[j]);
            out[j*4+1] = (uint8_t)(state[j] >> 8);
            out[j*4+2] = (uint8_t)(state[j] >> 16);
            out[j*4+3] = (uint8_t)(state[j] >> 24);
        }
    }

    /* =========================================================
    ** SHA-256 (FIPS 180-4) — compact implementation
    ** ========================================================= */

    static const uint32_t sha256_k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };

    #define RR(x,n) (((x)>>(n))|((x)<<(32-(n))))
    #define CH(x,y,z) (((x)&(y))^(~(x)&(z)))
    #define MAJ(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
    #define EP0(x) (RR(x,2)^RR(x,13)^RR(x,22))
    #define EP1(x) (RR(x,6)^RR(x,11)^RR(x,25))
    #define SIG0(x) (RR(x,7)^RR(x,18)^((x)>>3))
    #define SIG1(x) (RR(x,17)^RR(x,19)^((x)>>10))

    static void sha256_transform(uint32_t state[8], const uint8_t block[64])
    {
        uint32_t w[64], a,b,c,d,e,f,g,h,t1,t2;

        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)block[i*4]<<24) | ((uint32_t)block[i*4+1]<<16) |
                   ((uint32_t)block[i*4+2]<<8) | (uint32_t)block[i*4+3];
        for (int i = 16; i < 64; i++)
            w[i] = SIG1(w[i-2]) + w[i-7] + SIG0(w[i-15]) + w[i-16];

        a=state[0]; b=state[1]; c=state[2]; d=state[3];
        e=state[4]; f=state[5]; g=state[6]; h=state[7];

        for (int i = 0; i < 64; i++)
        {
            t1 = h + EP1(e) + CH(e,f,g) + sha256_k[i] + w[i];
            t2 = EP0(a) + MAJ(a,b,c);
            h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }

        state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
        state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
    }

    static void sha256_hash(const uint8_t *data, size_t len, uint8_t out[32])
    {
        uint32_t state[8] = {
            0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
            0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
        };
        size_t i;

        for (i = 0; i + 64 <= len; i += 64)
            sha256_transform(state, data + i);

        uint8_t block[64];
        size_t rem = len - i;
        memcpy(block, data + i, rem);
        block[rem] = 0x80;
        memset(block + rem + 1, 0, 64 - rem - 1);

        if (rem >= 56)
        {
            sha256_transform(state, block);
            memset(block, 0, 64);
        }

        uint64_t bits = (uint64_t)len * 8;
        for (int j = 0; j < 8; j++)
            block[56 + j] = (uint8_t)(bits >> ((7-j) * 8));

        sha256_transform(state, block);

        for (int j = 0; j < 8; j++)
        {
            out[j*4+0] = (uint8_t)(state[j] >> 24);
            out[j*4+1] = (uint8_t)(state[j] >> 16);
            out[j*4+2] = (uint8_t)(state[j] >> 8);
            out[j*4+3] = (uint8_t)(state[j]);
        }
    }

    #undef RR
    #undef CH
    #undef MAJ
    #undef EP0
    #undef EP1
    #undef SIG0
    #undef SIG1

    /* =========================================================
    ** Base64
    ** ========================================================= */

    static const char b64_enc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    static char *base64_encode(const uint8_t *data, int len, int *out_len)
    {
        int olen = 4 * ((len + 2) / 3);
        char *out = (char *)malloc((size_t)olen + 1);
        int j = 0;
        for (int i = 0; i < len; i += 3)
        {
            uint32_t n = ((uint32_t)data[i]) << 16;
            if (i+1 < len) n |= ((uint32_t)data[i+1]) << 8;
            if (i+2 < len) n |= (uint32_t)data[i+2];
            out[j++] = b64_enc[(n >> 18) & 0x3F];
            out[j++] = b64_enc[(n >> 12) & 0x3F];
            out[j++] = (i+1 < len) ? b64_enc[(n >> 6) & 0x3F] : '=';
            out[j++] = (i+2 < len) ? b64_enc[n & 0x3F] : '=';
        }
        out[j] = '\0';
        *out_len = j;
        return out;
    }

    static uint8_t *base64_decode(const char *data, int len, int *out_len)
    {
        static const uint8_t d[128] = {
            /* 0-42: invalid */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            /* 43 '+' */ 62,
            /* 44-46 */ 0,0,0,
            /* 47 '/' */ 63,
            /* 48-57 '0'-'9' */ 52,53,54,55,56,57,58,59,60,61,
            /* 58-64 */ 0,0,0,0,0,0,0,
            /* 65-90 'A'-'Z' */ 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,
            /* 91-96 */ 0,0,0,0,0,0,
            /* 97-122 'a'-'z' */ 26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51
        };
        int olen = len / 4 * 3;
        if (len > 0 && data[len-1] == '=') olen--;
        if (len > 1 && data[len-2] == '=') olen--;
        uint8_t *out = (uint8_t *)malloc((size_t)olen + 1);
        int j = 0;
        for (int i = 0; i < len; i += 4)
        {
            uint32_t n = (d[(uint8_t)data[i] & 0x7F] << 18) | (d[(uint8_t)data[i+1] & 0x7F] << 12);
            if (i+2 < len && data[i+2] != '=') n |= d[(uint8_t)data[i+2] & 0x7F] << 6;
            if (i+3 < len && data[i+3] != '=') n |= d[(uint8_t)data[i+3] & 0x7F];
            if (j < olen) out[j++] = (uint8_t)(n >> 16);
            if (j < olen) out[j++] = (uint8_t)(n >> 8);
            if (j < olen) out[j++] = (uint8_t)n;
        }
        *out_len = olen;
        return out;
    }

    /* =========================================================
    ** Hex
    ** ========================================================= */

    static const char hex_chars[] = "0123456789abcdef";

    /* =========================================================
    ** UUID v4
    ** ========================================================= */

    static bool rng_seeded = false;

    static void seed_rng()
    {
        if (!rng_seeded)
        {
            /* Use /dev/urandom for better randomness */
            FILE *f = fopen("/dev/urandom", "rb");
            if (f)
            {
                unsigned int seed;
                if (fread(&seed, sizeof(seed), 1, f) == 1)
                    srand(seed);
                else
                    srand((unsigned)time(NULL));
                fclose(f);
            }
            else
            {
                srand((unsigned)time(NULL));
            }
            rng_seeded = true;
        }
    }

    /* =========================================================
    ** Native functions
    ** ========================================================= */

    static int nat_crypto_md5(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_string(args[0]))
        {
            vm->runtime_error("crypto.md5() expects (string).");
            return -1;
        }
        ObjString *s = as_string(args[0]);
        uint8_t hash[16];
        md5_hash((const uint8_t *)s->chars, (size_t)s->length, hash);

        char hex[33];
        for (int i = 0; i < 16; i++)
        {
            hex[i*2] = hex_chars[hash[i] >> 4];
            hex[i*2+1] = hex_chars[hash[i] & 0x0F];
        }
        hex[32] = '\0';
        args[0] = val_obj((Obj *)vm->make_string(hex, 32));
        return 1;
    }

    static int nat_crypto_sha256(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_string(args[0]))
        {
            vm->runtime_error("crypto.sha256() expects (string).");
            return -1;
        }
        ObjString *s = as_string(args[0]);
        uint8_t hash[32];
        sha256_hash((const uint8_t *)s->chars, (size_t)s->length, hash);

        char hex[65];
        for (int i = 0; i < 32; i++)
        {
            hex[i*2] = hex_chars[hash[i] >> 4];
            hex[i*2+1] = hex_chars[hash[i] & 0x0F];
        }
        hex[64] = '\0';
        args[0] = val_obj((Obj *)vm->make_string(hex, 64));
        return 1;
    }

    static int nat_crypto_base64_encode(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_string(args[0]))
        {
            vm->runtime_error("crypto.base64_encode() expects (string).");
            return -1;
        }
        ObjString *s = as_string(args[0]);
        int out_len;
        char *encoded = base64_encode((const uint8_t *)s->chars, s->length, &out_len);
        args[0] = val_obj((Obj *)vm->make_string(encoded, out_len));
        free(encoded);
        return 1;
    }

    static int nat_crypto_base64_decode(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_string(args[0]))
        {
            vm->runtime_error("crypto.base64_decode() expects (string).");
            return -1;
        }
        ObjString *s = as_string(args[0]);
        int out_len;
        uint8_t *decoded = base64_decode(s->chars, s->length, &out_len);
        args[0] = val_obj((Obj *)vm->make_string((const char *)decoded, out_len));
        free(decoded);
        return 1;
    }

    static int nat_crypto_hex_encode(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_string(args[0]))
        {
            vm->runtime_error("crypto.hex_encode() expects (string).");
            return -1;
        }
        ObjString *s = as_string(args[0]);
        int out_len = s->length * 2;
        char *hex = (char *)malloc((size_t)out_len + 1);
        for (int i = 0; i < s->length; i++)
        {
            hex[i*2] = hex_chars[(uint8_t)s->chars[i] >> 4];
            hex[i*2+1] = hex_chars[(uint8_t)s->chars[i] & 0x0F];
        }
        hex[out_len] = '\0';
        args[0] = val_obj((Obj *)vm->make_string(hex, out_len));
        free(hex);
        return 1;
    }

    static int nat_crypto_hex_decode(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_string(args[0]))
        {
            vm->runtime_error("crypto.hex_decode() expects (string).");
            return -1;
        }
        ObjString *s = as_string(args[0]);
        if (s->length % 2 != 0)
        {
            args[0] = val_nil();
            return 1;
        }
        int out_len = s->length / 2;
        char *out = (char *)malloc((size_t)out_len);
        for (int i = 0; i < out_len; i++)
        {
            auto hex_val = [](char c) -> uint8_t {
                if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
                if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
                if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
                return 0;
            };
            out[i] = (char)((hex_val(s->chars[i*2]) << 4) | hex_val(s->chars[i*2+1]));
        }
        args[0] = val_obj((Obj *)vm->make_string(out, out_len));
        free(out);
        return 1;
    }

    static int nat_crypto_uuid4(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        seed_rng();

        uint8_t bytes[16];
        for (int i = 0; i < 16; i++)
            bytes[i] = (uint8_t)(rand() & 0xFF);

        /* Set version 4 */
        bytes[6] = (bytes[6] & 0x0F) | 0x40;
        /* Set variant 1 */
        bytes[8] = (bytes[8] & 0x3F) | 0x80;

        char uuid[37];
        snprintf(uuid, sizeof(uuid),
                 "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                 bytes[0],bytes[1],bytes[2],bytes[3],
                 bytes[4],bytes[5],bytes[6],bytes[7],
                 bytes[8],bytes[9],bytes[10],bytes[11],
                 bytes[12],bytes[13],bytes[14],bytes[15]);

        args[0] = val_obj((Obj *)vm->make_string(uuid, 36));
        return 1;
    }

    /* =========================================================
    ** Registration
    ** ========================================================= */

    static const NativeReg crypto_functions[] = {
        {"md5", nat_crypto_md5, 1},
        {"sha256", nat_crypto_sha256, 1},
        {"base64_encode", nat_crypto_base64_encode, 1},
        {"base64_decode", nat_crypto_base64_decode, 1},
        {"hex_encode", nat_crypto_hex_encode, 1},
        {"hex_decode", nat_crypto_hex_decode, 1},
        {"uuid4", nat_crypto_uuid4, 0},
    };

    const NativeLib zen_lib_crypto = {
        "crypto",
        crypto_functions,
        7,
        nullptr,
        0,
    };

} /* namespace zen */

#endif /* ZEN_ENABLE_CRYPTO */
