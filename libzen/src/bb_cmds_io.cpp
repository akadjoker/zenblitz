/*
** bb_cmds_io.cpp — Blitz Basic file, stream, directory and bank commands.
**
** Files and directories go through the VM backend (zen/backend.h); nothing
** here touches the OS. The program sees integer handles (bb_handles.h).
** Banks are ObjBuffer byte buffers kept alive in the "__bbBanks" global
** array (index = handle-1).
*/
#include "bb_runtime.h"
#include "bb_handles.h"
#include "object.h"
#include "ct/string.hpp"
#include <cstring>
#include <cstdlib>

using namespace zen;

namespace bb
{
    static Handles<ZenFile> files;
    static Handles<ZenDir> dirs;

    /* backend calls tolerant of a missing function */
    static inline const Backend &bk(VM *vm) { return vm->backend(); }
    static inline ZenFile file_of(Value v) { return files.get(bb_arg_int(v)); }
    static inline int64_t bk_read(VM *vm, ZenFile f, void *buf, int64_t n)
    {
        const Backend &b = bk(vm);
        return (f && b.read) ? b.read(f, buf, n, b.userdata) : 0;
    }
    static inline int64_t bk_write(VM *vm, ZenFile f, const void *buf, int64_t n)
    {
        const Backend &b = bk(vm);
        return (f && b.write) ? b.write(f, buf, n, b.userdata) : 0;
    }
    static inline int64_t bk_tell(VM *vm, ZenFile f)
    {
        const Backend &b = bk(vm);
        return (f && b.tell) ? b.tell(f, b.userdata) : 0;
    }
    static inline int64_t bk_size(VM *vm, ZenFile f)
    {
        const Backend &b = bk(vm);
        return (f && b.size) ? b.size(f, b.userdata) : 0;
    }
    static inline void bk_close(VM *vm, ZenFile f)
    {
        const Backend &b = bk(vm);
        if (f && b.close) b.close(f, b.userdata);
    }

    /* ================= file handles ================= */
    static int open_mode(VM *vm, Value *args, int mode)
    {
        const Backend &b = bk(vm);
        ZenFile f = b.open ? b.open(bb_arg_cstr(args[0]), mode, b.userdata) : nullptr;
        args[0] = val_int(f ? files.add(f) : 0);
        return 1;
    }

    static int c_OpenFile(VM *vm, Value *args, int) { return open_mode(vm, args, FILE_READWRITE); }
    static int c_ReadFile(VM *vm, Value *args, int) { return open_mode(vm, args, FILE_READ); }
    static int c_WriteFile(VM *vm, Value *args, int) { return open_mode(vm, args, FILE_WRITE); }

    static int c_CloseFile(VM *vm, Value *args, int)
    {
        long long h = bb_arg_int(args[0]);
        ZenFile f = files.get(h);
        if (f)
        {
            bk_close(vm, f);
            files.remove(h);
        }
        return 0;
    }

    static int c_FilePos(VM *vm, Value *args, int)
    {
        args[0] = val_int(bk_tell(vm, file_of(args[0])));
        return 1;
    }

    static int c_SeekFile(VM *vm, Value *args, int)
    {
        const Backend &b = bk(vm);
        ZenFile f = file_of(args[0]);
        int64_t pos = (f && b.seek) ? b.seek(f, bb_arg_int(args[1]), b.userdata) : -1;
        args[0] = val_int(pos < 0 ? 0 : pos);
        return 1;
    }

    /* ================= streams ================= */
    static int c_Eof(VM *vm, Value *args, int)
    {
        ZenFile f = file_of(args[0]);
        if (!f) { args[0] = val_int(-1); return 1; }
        args[0] = val_int(bk_tell(vm, f) >= bk_size(vm, f) ? 1 : 0);
        return 1;
    }

    static int c_ReadAvail(VM *vm, Value *args, int)
    {
        ZenFile f = file_of(args[0]);
        int64_t avail = f ? bk_size(vm, f) - bk_tell(vm, f) : 0;
        args[0] = val_int(avail < 0 ? 0 : avail);
        return 1;
    }

    static bool read_raw(VM *vm, Value v, void *buf, int64_t n)
    {
        memset(buf, 0, (size_t)n);
        return bk_read(vm, file_of(v), buf, n) == n;
    }

    static int c_ReadByte(VM *vm, Value *args, int)
    {
        unsigned char b = 0;
        read_raw(vm, args[0], &b, 1);
        args[0] = val_int(b);
        return 1;
    }
    static int c_ReadShort(VM *vm, Value *args, int)
    {
        unsigned short s = 0;
        read_raw(vm, args[0], &s, 2);
        args[0] = val_int(s);
        return 1;
    }
    static int c_ReadInt(VM *vm, Value *args, int)
    {
        int i = 0;
        read_raw(vm, args[0], &i, 4);
        args[0] = val_int(i);
        return 1;
    }
    static int c_ReadFloat(VM *vm, Value *args, int)
    {
        float fl = 0;
        read_raw(vm, args[0], &fl, 4);
        args[0] = val_float(fl);
        return 1;
    }
    static int c_ReadString(VM *vm, Value *args, int)
    {
        ZenFile f = file_of(args[0]);
        int n = 0;
        if (!f || bk_read(vm, f, &n, 4) != 4 || n < 0) { args[0] = bb_ret_str(vm, "", 0); return 1; }
        char *buf = (char *)malloc((size_t)n + 1);
        int64_t got = n ? bk_read(vm, f, buf, n) : 0;
        args[0] = bb_ret_str(vm, buf, got < 0 ? 0 : (int)got);
        free(buf);
        return 1;
    }
    static int c_ReadLine(VM *vm, Value *args, int)
    {
        ZenFile f = file_of(args[0]);
        ct::String s;
        if (f)
        {
            char c;
            while (bk_read(vm, f, &c, 1) == 1 && c != '\n') s.push_back(c);
            if (s.size() && s[s.size() - 1] == '\r') s.resize(s.size() - 1);
        }
        args[0] = bb_ret_str(vm, s.data(), (int)s.size());
        return 1;
    }

    static void write_raw(VM *vm, Value v, const void *buf, int64_t n)
    {
        bk_write(vm, file_of(v), buf, n);
    }
    static int c_WriteByte(VM *vm, Value *args, int) { unsigned char b = (unsigned char)bb_arg_int(args[1]); write_raw(vm, args[0], &b, 1); return 0; }
    static int c_WriteShort(VM *vm, Value *args, int) { unsigned short s = (unsigned short)bb_arg_int(args[1]); write_raw(vm, args[0], &s, 2); return 0; }
    static int c_WriteInt(VM *vm, Value *args, int) { int i = (int)bb_arg_int(args[1]); write_raw(vm, args[0], &i, 4); return 0; }
    static int c_WriteFloat(VM *vm, Value *args, int) { float fl = (float)bb_arg_float(args[1]); write_raw(vm, args[0], &fl, 4); return 0; }
    static int c_WriteString(VM *vm, Value *args, int)
    {
        int n = bb_arg_len(args[1]);
        write_raw(vm, args[0], &n, 4);
        write_raw(vm, args[0], bb_arg_cstr(args[1]), n);
        return 0;
    }
    static int c_WriteLine(VM *vm, Value *args, int)
    {
        write_raw(vm, args[0], bb_arg_cstr(args[1]), bb_arg_len(args[1]));
        write_raw(vm, args[0], "\n", 1);
        return 0;
    }

    /* ================= directories / filesystem ================= */
    static int c_ReadDir(VM *vm, Value *args, int)
    {
        const Backend &b = bk(vm);
        ZenDir d = b.open_dir ? b.open_dir(bb_arg_cstr(args[0]), b.userdata) : nullptr;
        args[0] = val_int(d ? dirs.add(d) : 0);
        return 1;
    }
    static int c_CloseDir(VM *vm, Value *args, int)
    {
        const Backend &b = bk(vm);
        long long h = bb_arg_int(args[0]);
        ZenDir d = dirs.get(h);
        if (d)
        {
            if (b.close_dir) b.close_dir(d, b.userdata);
            dirs.remove(h);
        }
        return 0;
    }
    static int c_NextFile(VM *vm, Value *args, int)
    {
        const Backend &b = bk(vm);
        ZenDir d = dirs.get(bb_arg_int(args[0]));
        const char *name = (d && b.next_file) ? b.next_file(d, b.userdata) : nullptr;
        args[0] = bb_ret_str(vm, name ? name : "");
        return 1;
    }
    static int c_CurrentDir(VM *vm, Value *args, int)
    {
        const Backend &b = bk(vm);
        const char *p = b.current_dir ? b.current_dir(b.userdata) : nullptr;
        args[0] = bb_ret_str(vm, p ? p : "");
        return 1;
    }
    static int c_ChangeDir(VM *vm, Value *args, int)
    {
        const Backend &b = bk(vm);
        if (b.change_dir) b.change_dir(bb_arg_cstr(args[0]), b.userdata);
        return 0;
    }
    static int c_CreateDir(VM *vm, Value *args, int)
    {
        const Backend &b = bk(vm);
        if (b.create_dir) b.create_dir(bb_arg_cstr(args[0]), b.userdata);
        return 0;
    }
    static int c_DeleteDir(VM *vm, Value *args, int)
    {
        const Backend &b = bk(vm);
        if (b.delete_dir) b.delete_dir(bb_arg_cstr(args[0]), b.userdata);
        return 0;
    }

    static int c_FileSize(VM *vm, Value *args, int)
    {
        const Backend &b = bk(vm);
        int64_t size = 0;
        int t = b.path_type ? b.path_type(bb_arg_cstr(args[0]), &size, b.userdata) : PATH_NONE;
        args[0] = val_int(t == PATH_FILE ? size : 0);
        return 1;
    }
    static int c_FileType(VM *vm, Value *args, int)
    {
        const Backend &b = bk(vm);
        args[0] = val_int(b.path_type ? b.path_type(bb_arg_cstr(args[0]), nullptr, b.userdata) : PATH_NONE);
        return 1;
    }
    static int c_CopyFile(VM *vm, Value *args, int)
    {
        const Backend &b = bk(vm);
        if (!b.open) return 0;
        ZenFile in = b.open(bb_arg_cstr(args[0]), FILE_READ, b.userdata);
        if (!in) return 0;
        ZenFile out = b.open(bb_arg_cstr(args[1]), FILE_WRITE, b.userdata);
        if (out)
        {
            char buf[65536];
            int64_t n;
            while ((n = bk_read(vm, in, buf, sizeof(buf))) > 0) bk_write(vm, out, buf, n);
            bk_close(vm, out);
        }
        bk_close(vm, in);
        return 0;
    }
    static int c_DeleteFile(VM *vm, Value *args, int)
    {
        const Backend &b = bk(vm);
        if (b.delete_file) b.delete_file(bb_arg_cstr(args[0]), b.userdata);
        return 0;
    }

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
        args[0] = val_int(bk_read(vm, file_of(args[1]), p, n));
        return 1;
    }
    static int c_WriteBytes(VM *vm, Value *args, int)
    {
        unsigned char *p;
        long long n = bb_arg_int(args[3]);
        if (n < 0 || !bank_range(vm, args[0], args[2], (int)n, &p)) return -1;
        args[0] = val_int(bk_write(vm, file_of(args[1]), p, n));
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
