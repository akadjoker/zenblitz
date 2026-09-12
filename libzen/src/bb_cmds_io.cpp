/*
** bb_cmds_io.cpp — Blitz Basic file, stream, directory and bank commands.
**
** Files and directories are integer handles over stdio; banks are ObjBuffer
** byte buffers kept alive in the "__bbBanks" global array (index = handle-1).
*/
#include "bb_runtime.h"
#include "object.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

using namespace zen;

namespace bb
{
    /* ================= file handles ================= */
    static std::map<long long, FILE *> files;
    static long long next_file = 0;

    static FILE *file_of(Value v)
    {
        std::map<long long, FILE *>::iterator it = files.find(bb_arg_int(v));
        return it == files.end() ? nullptr : it->second;
    }

    static int open_mode(VM *vm, Value *args, const char *mode)
    {
        FILE *f = fopen(bb_arg_cstr(args[0]), mode);
        if (!f) { args[0] = val_int(0); return 1; }
        files[++next_file] = f;
        args[0] = val_int(next_file);
        (void)vm;
        return 1;
    }

    static int c_OpenFile(VM *vm, Value *args, int) { return open_mode(vm, args, "r+b"); }
    static int c_ReadFile(VM *vm, Value *args, int) { return open_mode(vm, args, "rb"); }
    static int c_WriteFile(VM *vm, Value *args, int) { return open_mode(vm, args, "w+b"); }

    static int c_CloseFile(VM *, Value *args, int)
    {
        long long h = bb_arg_int(args[0]);
        std::map<long long, FILE *>::iterator it = files.find(h);
        if (it != files.end()) { fclose(it->second); files.erase(it); }
        return 0;
    }

    static int c_FilePos(VM *, Value *args, int)
    {
        FILE *f = file_of(args[0]);
        args[0] = val_int(f ? (long long)ftell(f) : 0);
        return 1;
    }

    static int c_SeekFile(VM *, Value *args, int)
    {
        FILE *f = file_of(args[0]);
        long long pos = bb_arg_int(args[1]);
        if (f) { fseek(f, (long)pos, SEEK_SET); pos = ftell(f); } else pos = 0;
        args[0] = val_int(pos);
        return 1;
    }

    /* ================= streams ================= */
    static int c_Eof(VM *, Value *args, int)
    {
        FILE *f = file_of(args[0]);
        if (!f) { args[0] = val_int(-1); return 1; }
        int c = fgetc(f);
        if (c == EOF) { args[0] = val_int(1); return 1; }
        ungetc(c, f);
        args[0] = val_int(0);
        return 1;
    }

    static int c_ReadAvail(VM *, Value *args, int)
    {
        FILE *f = file_of(args[0]);
        if (!f) { args[0] = val_int(0); return 1; }
        long cur = ftell(f);
        fseek(f, 0, SEEK_END);
        long end = ftell(f);
        fseek(f, cur, SEEK_SET);
        args[0] = val_int(end - cur);
        return 1;
    }

    static bool read_raw(Value v, void *buf, size_t n)
    {
        FILE *f = file_of(v);
        if (!f) return false;
        memset(buf, 0, n);
        return fread(buf, 1, n, f) == n;
    }

    static int c_ReadByte(VM *, Value *args, int)
    {
        unsigned char b = 0;
        read_raw(args[0], &b, 1);
        args[0] = val_int(b);
        return 1;
    }
    static int c_ReadShort(VM *, Value *args, int)
    {
        unsigned short s = 0;
        read_raw(args[0], &s, 2);
        args[0] = val_int(s);
        return 1;
    }
    static int c_ReadInt(VM *, Value *args, int)
    {
        int i = 0;
        read_raw(args[0], &i, 4);
        args[0] = val_int(i);
        return 1;
    }
    static int c_ReadFloat(VM *, Value *args, int)
    {
        float fl = 0;
        read_raw(args[0], &fl, 4);
        args[0] = val_float(fl);
        return 1;
    }
    static int c_ReadString(VM *vm, Value *args, int)
    {
        FILE *f = file_of(args[0]);
        int n = 0;
        if (!f || fread(&n, 1, 4, f) != 4 || n < 0) { args[0] = bb_ret_str(vm, ""); return 1; }
        std::string s((size_t)n, '\0');
        size_t got = n ? fread(&s[0], 1, (size_t)n, f) : 0;
        s.resize(got);
        args[0] = bb_ret_str(vm, s);
        return 1;
    }
    static int c_ReadLine(VM *vm, Value *args, int)
    {
        FILE *f = file_of(args[0]);
        std::string s;
        if (f)
        {
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n') s += (char)c;
            if (s.size() && s[s.size() - 1] == '\r') s.resize(s.size() - 1);
        }
        args[0] = bb_ret_str(vm, s);
        return 1;
    }

    static void write_raw(Value v, const void *buf, size_t n)
    {
        FILE *f = file_of(v);
        if (f) fwrite(buf, 1, n, f);
    }
    static int c_WriteByte(VM *, Value *args, int) { unsigned char b = (unsigned char)bb_arg_int(args[1]); write_raw(args[0], &b, 1); return 0; }
    static int c_WriteShort(VM *, Value *args, int) { unsigned short s = (unsigned short)bb_arg_int(args[1]); write_raw(args[0], &s, 2); return 0; }
    static int c_WriteInt(VM *, Value *args, int) { int i = (int)bb_arg_int(args[1]); write_raw(args[0], &i, 4); return 0; }
    static int c_WriteFloat(VM *, Value *args, int) { float fl = (float)bb_arg_float(args[1]); write_raw(args[0], &fl, 4); return 0; }
    static int c_WriteString(VM *, Value *args, int)
    {
        int n = bb_arg_len(args[1]);
        write_raw(args[0], &n, 4);
        write_raw(args[0], bb_arg_cstr(args[1]), (size_t)n);
        return 0;
    }
    static int c_WriteLine(VM *, Value *args, int)
    {
        write_raw(args[0], bb_arg_cstr(args[1]), (size_t)bb_arg_len(args[1]));
        write_raw(args[0], "\n", 1);
        return 0;
    }

    /* ================= directories / filesystem ================= */
    static std::map<long long, DIR *> dirs;
    static long long next_dir = 0;

    static int c_ReadDir(VM *, Value *args, int)
    {
        DIR *d = opendir(bb_arg_cstr(args[0]));
        if (!d) { args[0] = val_int(0); return 1; }
        dirs[++next_dir] = d;
        args[0] = val_int(next_dir);
        return 1;
    }
    static int c_CloseDir(VM *, Value *args, int)
    {
        std::map<long long, DIR *>::iterator it = dirs.find(bb_arg_int(args[0]));
        if (it != dirs.end()) { closedir(it->second); dirs.erase(it); }
        return 0;
    }
    static int c_NextFile(VM *vm, Value *args, int)
    {
        std::map<long long, DIR *>::iterator it = dirs.find(bb_arg_int(args[0]));
        const char *name = "";
        if (it != dirs.end())
        {
            struct dirent *e = readdir(it->second);
            if (e) name = e->d_name;
        }
        args[0] = bb_ret_str(vm, name);
        return 1;
    }
    static int c_CurrentDir(VM *vm, Value *args, int)
    {
        char buf[4096];
        const char *p = getcwd(buf, sizeof(buf));
        args[0] = bb_ret_str(vm, p ? p : "");
        return 1;
    }
    static int c_ChangeDir(VM *, Value *args, int) { if (chdir(bb_arg_cstr(args[0])) != 0) {} return 0; }
    static int c_CreateDir(VM *, Value *args, int) { if (mkdir(bb_arg_cstr(args[0]), 0777) != 0) {} return 0; }
    static int c_DeleteDir(VM *, Value *args, int) { if (rmdir(bb_arg_cstr(args[0])) != 0) {} return 0; }

    static int c_FileSize(VM *, Value *args, int)
    {
        struct stat st;
        args[0] = val_int(stat(bb_arg_cstr(args[0]), &st) == 0 ? (long long)st.st_size : 0);
        return 1;
    }
    static int c_FileType(VM *, Value *args, int)
    {
        struct stat st;
        int t = 0;
        if (stat(bb_arg_cstr(args[0]), &st) == 0) t = S_ISDIR(st.st_mode) ? 2 : 1;
        args[0] = val_int(t);
        return 1;
    }
    static int c_CopyFile(VM *, Value *args, int)
    {
        FILE *in = fopen(bb_arg_cstr(args[0]), "rb");
        if (!in) return 0;
        FILE *out = fopen(bb_arg_cstr(args[1]), "wb");
        if (!out) { fclose(in); return 0; }
        char buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
        fclose(in);
        fclose(out);
        return 0;
    }
    static int c_DeleteFile(VM *, Value *args, int) { remove(bb_arg_cstr(args[0])); return 0; }

    /* ================= banks ================= */
    static int g_banks = -1; /* global index of the __bbBanks array */

    static ObjArray *bank_table(VM *vm)
    {
        if (g_banks < 0)
        {
            g_banks = vm->find_global("__bbBanks");
            if (g_banks < 0) g_banks = vm->def_global("__bbBanks", val_obj((Obj *)new_array(&vm->get_gc())));
        }
        Value v = vm->get_global(g_banks);
        if (!is_array(v))
        {
            v = val_obj((Obj *)new_array(&vm->get_gc()));
            vm->set_global(g_banks, v);
        }
        return as_array(v);
    }

    static ObjBuffer *bank_of(VM *vm, Value h)
    {
        long long idx = bb_arg_int(h) - 1;
        ObjArray *t = bank_table(vm);
        if (idx < 0 || idx >= arr_count(t)) return nullptr;
        Value v = t->data[idx];
        return is_buffer(v) ? as_buffer(v) : nullptr;
    }

    static int c_CreateBank(VM *vm, Value *args, int)
    {
        long long size = bb_arg_int(args[0]);
        if (size < 0) size = 0;
        ObjArray *t = bank_table(vm);
        ObjBuffer *buf = new_buffer(&vm->get_gc(), BUF_UINT8, (int32_t)size);
        /* reuse a freed slot if any */
        for (int32_t i = 0; i < arr_count(t); ++i)
        {
            if (is_nil(t->data[i]))
            {
                array_set(&vm->get_gc(), t, i, val_obj((Obj *)buf));
                args[0] = val_int(i + 1);
                return 1;
            }
        }
        array_push(&vm->get_gc(), t, val_obj((Obj *)buf));
        args[0] = val_int(arr_count(t));
        return 1;
    }
    static int c_FreeBank(VM *vm, Value *args, int)
    {
        long long idx = bb_arg_int(args[0]) - 1;
        ObjArray *t = bank_table(vm);
        if (idx >= 0 && idx < arr_count(t)) t->data[idx] = val_nil();
        return 0;
    }
    static int c_BankSize(VM *vm, Value *args, int)
    {
        ObjBuffer *b = bank_of(vm, args[0]);
        args[0] = val_int(b ? b->count : 0);
        return 1;
    }
    static int c_ResizeBank(VM *vm, Value *args, int)
    {
        ObjBuffer *b = bank_of(vm, args[0]);
        long long size = bb_arg_int(args[1]);
        if (!b || size < 0) return 0;
        ObjBuffer *nb = new_buffer(&vm->get_gc(), BUF_UINT8, (int32_t)size);
        memcpy(nb->data, b->data, (size_t)(size < b->count ? size : b->count));
        long long idx = bb_arg_int(args[0]) - 1;
        array_set(&vm->get_gc(), bank_table(vm), (int32_t)idx, val_obj((Obj *)nb));
        return 0;
    }
    static int c_CopyBank(VM *vm, Value *args, int)
    {
        ObjBuffer *src = bank_of(vm, args[0]);
        long long so = bb_arg_int(args[1]);
        ObjBuffer *dst = bank_of(vm, args[2]);
        long long dof = bb_arg_int(args[3]);
        long long n = bb_arg_int(args[4]);
        if (!src || !dst) { vm->runtime_error("Bank does not exist"); return -1; }
        if (so < 0 || dof < 0 || n < 0 || so + n > src->count || dof + n > dst->count) { vm->runtime_error("Bank offset out of range"); return -1; }
        memmove(dst->data + dof, src->data + so, (size_t)n);
        return 0;
    }

    static bool bank_range(VM *vm, Value h, Value off, int size, unsigned char **p)
    {
        ObjBuffer *b = bank_of(vm, h);
        long long o = bb_arg_int(off);
        if (!b) { vm->runtime_error("Bank does not exist"); return false; }
        if (o < 0 || o + size > b->count) { vm->runtime_error("Bank offset out of range"); return false; }
        *p = b->data + o;
        return true;
    }
    static int c_PeekByte(VM *vm, Value *args, int) { unsigned char *p; if (!bank_range(vm, args[0], args[1], 1, &p)) return -1; args[0] = val_int(*p); return 1; }
    static int c_PeekShort(VM *vm, Value *args, int) { unsigned char *p; if (!bank_range(vm, args[0], args[1], 2, &p)) return -1; unsigned short s; memcpy(&s, p, 2); args[0] = val_int(s); return 1; }
    static int c_PeekInt(VM *vm, Value *args, int) { unsigned char *p; if (!bank_range(vm, args[0], args[1], 4, &p)) return -1; int i; memcpy(&i, p, 4); args[0] = val_int(i); return 1; }
    static int c_PeekFloat(VM *vm, Value *args, int) { unsigned char *p; if (!bank_range(vm, args[0], args[1], 4, &p)) return -1; float f; memcpy(&f, p, 4); args[0] = val_float(f); return 1; }
    static int c_PokeByte(VM *vm, Value *args, int) { unsigned char *p; if (!bank_range(vm, args[0], args[1], 1, &p)) return -1; *p = (unsigned char)bb_arg_int(args[2]); return 0; }
    static int c_PokeShort(VM *vm, Value *args, int) { unsigned char *p; if (!bank_range(vm, args[0], args[1], 2, &p)) return -1; unsigned short s = (unsigned short)bb_arg_int(args[2]); memcpy(p, &s, 2); return 0; }
    static int c_PokeInt(VM *vm, Value *args, int) { unsigned char *p; if (!bank_range(vm, args[0], args[1], 4, &p)) return -1; int i = (int)bb_arg_int(args[2]); memcpy(p, &i, 4); return 0; }
    static int c_PokeFloat(VM *vm, Value *args, int) { unsigned char *p; if (!bank_range(vm, args[0], args[1], 4, &p)) return -1; float f = (float)bb_arg_float(args[2]); memcpy(p, &f, 4); return 0; }

    static int c_ReadBytes(VM *vm, Value *args, int)
    {
        unsigned char *p;
        long long n = bb_arg_int(args[3]);
        if (n < 0 || !bank_range(vm, args[0], args[2], (int)n, &p)) return -1;
        FILE *f = file_of(args[1]);
        args[0] = val_int(f ? (long long)fread(p, 1, (size_t)n, f) : 0);
        return 1;
    }
    static int c_WriteBytes(VM *vm, Value *args, int)
    {
        unsigned char *p;
        long long n = bb_arg_int(args[3]);
        if (n < 0 || !bank_range(vm, args[0], args[2], (int)n, &p)) return -1;
        FILE *f = file_of(args[1]);
        args[0] = val_int(f ? (long long)fwrite(p, 1, (size_t)n, f) : 0);
        return 1;
    }

    /* ================= table ================= */
    const BBCommand bb_cmds_io[] = {
        {"%OpenFile$filename", c_OpenFile},
        {"%ReadFile$filename", c_ReadFile},
        {"%WriteFile$filename", c_WriteFile},
        {"CloseFile%file_stream", c_CloseFile},
        {"%FilePos%file_stream", c_FilePos},
        {"%SeekFile%file_stream%pos", c_SeekFile},

        {"%Eof%stream", c_Eof},
        {"%ReadAvail%stream", c_ReadAvail},
        {"%ReadByte%stream", c_ReadByte},
        {"%ReadShort%stream", c_ReadShort},
        {"%ReadInt%stream", c_ReadInt},
        {"#ReadFloat%stream", c_ReadFloat},
        {"$ReadString%stream", c_ReadString},
        {"$ReadLine%stream", c_ReadLine},
        {"WriteByte%stream%byte", c_WriteByte},
        {"WriteShort%stream%short", c_WriteShort},
        {"WriteInt%stream%int", c_WriteInt},
        {"WriteFloat%stream#float", c_WriteFloat},
        {"WriteString%stream$string", c_WriteString},
        {"WriteLine%stream$string", c_WriteLine},

        {"%ReadDir$dirname", c_ReadDir},
        {"CloseDir%dir", c_CloseDir},
        {"$NextFile%dir", c_NextFile},
        {"$CurrentDir", c_CurrentDir},
        {"ChangeDir$dir", c_ChangeDir},
        {"CreateDir$dir", c_CreateDir},
        {"DeleteDir$dir", c_DeleteDir},
        {"%FileSize$file", c_FileSize},
        {"%FileType$file", c_FileType},
        {"CopyFile$file$to", c_CopyFile},
        {"DeleteFile$file", c_DeleteFile},

        {"%CreateBank%size=0", c_CreateBank},
        {"FreeBank%bank", c_FreeBank},
        {"%BankSize%bank", c_BankSize},
        {"ResizeBank%bank%size", c_ResizeBank},
        {"CopyBank%src_bank%src_offset%dest_bank%dest_offset%count", c_CopyBank},
        {"%PeekByte%bank%offset", c_PeekByte},
        {"%PeekShort%bank%offset", c_PeekShort},
        {"%PeekInt%bank%offset", c_PeekInt},
        {"#PeekFloat%bank%offset", c_PeekFloat},
        {"PokeByte%bank%offset%value", c_PokeByte},
        {"PokeShort%bank%offset%value", c_PokeShort},
        {"PokeInt%bank%offset%value", c_PokeInt},
        {"PokeFloat%bank%offset#value", c_PokeFloat},
        {"%ReadBytes%bank%file%offset%count", c_ReadBytes},
        {"%WriteBytes%bank%file%offset%count", c_WriteBytes},
    };
    const int bb_cmds_io_count = (int)(sizeof(bb_cmds_io) / sizeof(bb_cmds_io[0]));
}
