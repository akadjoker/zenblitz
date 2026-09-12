/* =========================================================
** builtin_file.cpp — "file" module for Zen (binary I/O)
**
** Handle-based file I/O with typed read/write operations.
** Complements "fs" (high-level) with low-level binary access.
**
** Usage:
**   import file;
**   var f = file.open("data.bin", "w");
**   file.write_int(f, 42);
**   file.write_float(f, 3.14);
**   file.write_string(f, "hello");
**   file.close(f);
** ========================================================= */

#include "module.h"
#include "vm.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace zen
{

    /* =========================================================
    ** File handle table
    ** ========================================================= */

    static const int MAX_OPEN_FILES = 64;

    struct FileSlot
    {
        FILE *fp;
        bool in_use;
    };

    static FileSlot file_table[MAX_OPEN_FILES];
    static bool file_table_init = false;

    static void init_file_table()
    {
        if (!file_table_init)
        {
            for (int i = 0; i < MAX_OPEN_FILES; i++)
            {
                file_table[i].fp = nullptr;
                file_table[i].in_use = false;
            }
            file_table_init = true;
        }
    }

    static int alloc_slot(FILE *fp)
    {
        init_file_table();
        for (int i = 0; i < MAX_OPEN_FILES; i++)
        {
            if (!file_table[i].in_use)
            {
                file_table[i].fp = fp;
                file_table[i].in_use = true;
                return i + 1; /* 1-based handle */
            }
        }
        return -1;
    }

    static FILE *get_fp(int handle)
    {
        if (handle < 1 || handle > MAX_OPEN_FILES)
            return nullptr;
        FileSlot &slot = file_table[handle - 1];
        if (!slot.in_use)
            return nullptr;
        return slot.fp;
    }

    static void free_slot(int handle)
    {
        if (handle >= 1 && handle <= MAX_OPEN_FILES)
        {
            file_table[handle - 1].fp = nullptr;
            file_table[handle - 1].in_use = false;
        }
    }

    /* =========================================================
    ** open(path, mode) → handle (int) or nil
    ** mode: passed directly to fopen (e.g. "rb", "w", "r+b", "a")
    **       default: "rb"
    ** ========================================================= */
    static int nat_file_open(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_string(args[0]))
        {
            vm->runtime_error("file.open() expects (path, mode?).");
            return -1;
        }

        ObjString *path = as_string(args[0]);
        const char *mode_str = "rb";
        if (nargs >= 2 && is_string(args[1]))
            mode_str = as_string(args[1])->chars;

        /* Always open in binary: on Windows, text mode would translate
           \n <-> \r\n and desync sizes/offsets across platforms. */
        char mode_buf[8];
        {
            size_t mi = 0;
            bool has_b = false;
            for (const char *m = mode_str; *m && mi < sizeof(mode_buf) - 2; m++)
            {
                mode_buf[mi++] = *m;
                if (*m == 'b')
                    has_b = true;
            }
            if (!has_b)
                mode_buf[mi++] = 'b';
            mode_buf[mi] = '\0';
        }

        FILE *fp = fopen(path->chars, mode_buf);
        if (!fp)
        {
            args[0] = val_nil();
            return 1;
        }

        int handle = alloc_slot(fp);
        if (handle < 0)
        {
            fclose(fp);
            vm->runtime_error("file.open(): too many open files.");
            return -1;
        }

        args[0] = val_int(handle);
        return 1;
    }

    /* =========================================================
    ** close(handle) → bool
    ** ========================================================= */
    static int nat_file_close(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_int(args[0]))
        {
            args[0] = val_bool(false);
            return 1;
        }
        int handle = (int)args[0].as.integer;
        FILE *fp = get_fp(handle);
        if (!fp)
        {
            args[0] = val_bool(false);
            return 1;
        }
        fclose(fp);
        free_slot(handle);
        args[0] = val_bool(true);
        return 1;
    }

    /* =========================================================
    ** flush(handle) → bool
    ** ========================================================= */
    static int nat_file_flush(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_int(args[0]))
        {
            args[0] = val_bool(false);
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_bool(false);
            return 1;
        }
        args[0] = val_bool(fflush(fp) == 0);
        return 1;
    }

    /* =========================================================
    ** seek(handle, pos) → bool
    ** ========================================================= */
    static int nat_file_seek(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_int(args[0]) || !is_int(args[1]))
        {
            args[0] = val_bool(false);
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_bool(false);
            return 1;
        }
        long pos = (long)args[1].as.integer;
        args[0] = val_bool(fseek(fp, pos, SEEK_SET) == 0);
        return 1;
    }

    /* =========================================================
    ** tell(handle) → int
    ** ========================================================= */
    static int nat_file_tell(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_int(args[0]))
        {
            args[0] = val_int(-1);
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_int(-1);
            return 1;
        }
        args[0] = val_int((int64_t)ftell(fp));
        return 1;
    }

    /* =========================================================
    ** size(handle) → int
    ** ========================================================= */
    static int nat_file_size(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_int(args[0]))
        {
            args[0] = val_int(-1);
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_int(-1);
            return 1;
        }
        long cur = ftell(fp);
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, cur, SEEK_SET);
        args[0] = val_int((int64_t)sz);
        return 1;
    }

    /* =========================================================
    ** WRITE operations
    ** ========================================================= */

    /* write_byte(handle, val) → bool */
    static int nat_file_write_byte(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_int(args[0]))
        {
            args[0] = val_bool(false);
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_bool(false);
            return 1;
        }
        uint8_t byte = (uint8_t)to_integer(args[1]);
        args[0] = val_bool(fwrite(&byte, 1, 1, fp) == 1);
        return 1;
    }

    /* write_short(handle, val) → bool (2 bytes, native endian) */
    static int nat_file_write_short(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_int(args[0]))
        {
            args[0] = val_bool(false);
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_bool(false);
            return 1;
        }
        int16_t val = (int16_t)to_integer(args[1]);
        args[0] = val_bool(fwrite(&val, 2, 1, fp) == 1);
        return 1;
    }

    /* write_int(handle, val) → bool (4 bytes, native endian) */
    static int nat_file_write_int(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_int(args[0]))
        {
            args[0] = val_bool(false);
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_bool(false);
            return 1;
        }
        int32_t val = (int32_t)to_integer(args[1]);
        args[0] = val_bool(fwrite(&val, 4, 1, fp) == 1);
        return 1;
    }

    /* write_long(handle, val) → bool (8 bytes, native endian) */
    static int nat_file_write_long(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_int(args[0]))
        {
            args[0] = val_bool(false);
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_bool(false);
            return 1;
        }
        int64_t val = to_integer(args[1]);
        args[0] = val_bool(fwrite(&val, 8, 1, fp) == 1);
        return 1;
    }

    /* write_float(handle, val) → bool (4 bytes) */
    static int nat_file_write_float(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_int(args[0]))
        {
            args[0] = val_bool(false);
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_bool(false);
            return 1;
        }
        float val = (float)to_number(args[1]);
        args[0] = val_bool(fwrite(&val, 4, 1, fp) == 1);
        return 1;
    }

    /* write_double(handle, val) → bool (8 bytes) */
    static int nat_file_write_double(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_int(args[0]))
        {
            args[0] = val_bool(false);
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_bool(false);
            return 1;
        }
        double val = to_number(args[1]);
        args[0] = val_bool(fwrite(&val, 8, 1, fp) == 1);
        return 1;
    }

    /* write_string(handle, str) → bool (4-byte length prefix + chars) */
    static int nat_file_write_string(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_int(args[0]) || !is_string(args[1]))
        {
            args[0] = val_bool(false);
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_bool(false);
            return 1;
        }
        ObjString *s = as_string(args[1]);
        uint32_t len = (uint32_t)s->length;
        if (fwrite(&len, 4, 1, fp) != 1)
        {
            args[0] = val_bool(false);
            return 1;
        }
        if (len > 0 && fwrite(s->chars, 1, len, fp) != len)
        {
            args[0] = val_bool(false);
            return 1;
        }
        args[0] = val_bool(true);
        return 1;
    }

    /* write_bytes(handle, str) → bool (raw bytes, no length prefix) */
    static int nat_file_write_bytes(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_int(args[0]) || !is_string(args[1]))
        {
            args[0] = val_bool(false);
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_bool(false);
            return 1;
        }
        ObjString *s = as_string(args[1]);
        if (s->length > 0)
            args[0] = val_bool(fwrite(s->chars, 1, s->length, fp) == (size_t)s->length);
        else
            args[0] = val_bool(true);
        return 1;
    }

    /* =========================================================
    ** READ operations
    ** ========================================================= */

    /* read_byte(handle) → int or nil */
    static int nat_file_read_byte(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_int(args[0]))
        {
            args[0] = val_nil();
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_nil();
            return 1;
        }
        uint8_t byte;
        if (fread(&byte, 1, 1, fp) != 1)
        {
            args[0] = val_nil();
            return 1;
        }
        args[0] = val_int(byte);
        return 1;
    }

    /* read_short(handle) → int or nil (2 bytes) */
    static int nat_file_read_short(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_int(args[0]))
        {
            args[0] = val_nil();
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_nil();
            return 1;
        }
        int16_t val;
        if (fread(&val, 2, 1, fp) != 1)
        {
            args[0] = val_nil();
            return 1;
        }
        args[0] = val_int(val);
        return 1;
    }

    /* read_int(handle) → int or nil (4 bytes) */
    static int nat_file_read_int(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_int(args[0]))
        {
            args[0] = val_nil();
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_nil();
            return 1;
        }
        int32_t val;
        if (fread(&val, 4, 1, fp) != 1)
        {
            args[0] = val_nil();
            return 1;
        }
        args[0] = val_int(val);
        return 1;
    }

    /* read_long(handle) → int or nil (8 bytes) */
    static int nat_file_read_long(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_int(args[0]))
        {
            args[0] = val_nil();
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_nil();
            return 1;
        }
        int64_t val;
        if (fread(&val, 8, 1, fp) != 1)
        {
            args[0] = val_nil();
            return 1;
        }
        args[0] = val_int(val);
        return 1;
    }

    /* read_float(handle) → float or nil (4 bytes) */
    static int nat_file_read_float(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_int(args[0]))
        {
            args[0] = val_nil();
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_nil();
            return 1;
        }
        float val;
        if (fread(&val, 4, 1, fp) != 1)
        {
            args[0] = val_nil();
            return 1;
        }
        args[0] = val_float((double)val);
        return 1;
    }

    /* read_double(handle) → float or nil (8 bytes) */
    static int nat_file_read_double(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_int(args[0]))
        {
            args[0] = val_nil();
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_nil();
            return 1;
        }
        double val;
        if (fread(&val, 8, 1, fp) != 1)
        {
            args[0] = val_nil();
            return 1;
        }
        args[0] = val_float(val);
        return 1;
    }

    /* read_string(handle) → string or nil (4-byte length prefix + chars) */
    static int nat_file_read_string(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_int(args[0]))
        {
            args[0] = val_nil();
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_nil();
            return 1;
        }
        uint32_t len;
        if (fread(&len, 4, 1, fp) != 1)
        {
            args[0] = val_nil();
            return 1;
        }
        /* Sanity check */
        if (len > 16 * 1024 * 1024)
        {
            args[0] = val_nil();
            return 1;
        }
        if (len == 0)
        {
            args[0] = val_obj((Obj *)vm->make_string("", 0));
            return 1;
        }
        char *buf = (char *)malloc(len);
        if (!buf)
        {
            args[0] = val_nil();
            return 1;
        }
        if (fread(buf, 1, len, fp) != len)
        {
            free(buf);
            args[0] = val_nil();
            return 1;
        }
        args[0] = val_obj((Obj *)vm->make_string(buf, (int)len));
        free(buf);
        return 1;
    }

    /* read_bytes(handle, count) → string or nil (raw bytes) */
    static int nat_file_read_bytes(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_int(args[0]) || !is_int(args[1]))
        {
            args[0] = val_nil();
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_nil();
            return 1;
        }
        int64_t count = args[1].as.integer;
        if (count <= 0 || count > 16 * 1024 * 1024)
        {
            args[0] = val_nil();
            return 1;
        }
        char *buf = (char *)malloc((size_t)count);
        if (!buf)
        {
            args[0] = val_nil();
            return 1;
        }
        size_t got = fread(buf, 1, (size_t)count, fp);
        if (got == 0)
        {
            free(buf);
            args[0] = val_nil();
            return 1;
        }
        args[0] = val_obj((Obj *)vm->make_string(buf, (int)got));
        free(buf);
        return 1;
    }

    /* read_all(handle) → string or nil (reads from current pos to end) */
    static int nat_file_read_all(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_int(args[0]))
        {
            args[0] = val_nil();
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_nil();
            return 1;
        }
        long cur = ftell(fp);
        fseek(fp, 0, SEEK_END);
        long end = ftell(fp);
        fseek(fp, cur, SEEK_SET);

        long remaining = end - cur;
        if (remaining <= 0)
        {
            args[0] = val_obj((Obj *)vm->make_string("", 0));
            return 1;
        }
        if (remaining > 64 * 1024 * 1024)
        {
            vm->runtime_error("file.read_all(): file too large (%ld bytes).", remaining);
            return -1;
        }
        char *buf = (char *)malloc((size_t)remaining);
        if (!buf)
        {
            args[0] = val_nil();
            return 1;
        }
        size_t got = fread(buf, 1, (size_t)remaining, fp);
        args[0] = val_obj((Obj *)vm->make_string(buf, (int)got));
        free(buf);
        return 1;
    }

    /* =========================================================
    ** eof(handle) → bool
    ** ========================================================= */
    static int nat_file_eof(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_int(args[0]))
        {
            args[0] = val_bool(true);
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_bool(true);
            return 1;
        }
        args[0] = val_bool(feof(fp) != 0);
        return 1;
    }

    /* =========================================================
    ** write_buffer(handle, buffer) → bool
    ** Writes the raw bytes of a Buffer to file
    ** ========================================================= */
    static int nat_file_write_buffer(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_int(args[0]) || !is_buffer(args[1]))
        {
            args[0] = val_bool(false);
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_bool(false);
            return 1;
        }
        ObjBuffer *buf = as_buffer(args[1]);
        size_t byte_size = (size_t)buf->count * buffer_elem_size[buf->btype];
        if (byte_size == 0)
        {
            args[0] = val_bool(true);
            return 1;
        }
        args[0] = val_bool(fwrite(buf->data, 1, byte_size, fp) == byte_size);
        return 1;
    }

    /* =========================================================
    ** read_buffer(handle, buffer) → int (bytes read)
    ** Reads raw bytes from file into existing buffer
    ** ========================================================= */
    static int nat_file_read_buffer(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_int(args[0]) || !is_buffer(args[1]))
        {
            args[0] = val_int(0);
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_int(0);
            return 1;
        }
        ObjBuffer *buf = as_buffer(args[1]);
        size_t byte_size = (size_t)buf->count * buffer_elem_size[buf->btype];
        if (byte_size == 0)
        {
            args[0] = val_int(0);
            return 1;
        }
        size_t got = fread(buf->data, 1, byte_size, fp);
        args[0] = val_int((int64_t)got);
        return 1;
    }

    /* =========================================================
    ** read_line(handle) → string or nil
    ** Reads one line (up to \n or EOF)
    ** ========================================================= */
    static int nat_file_read_line(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_int(args[0]))
        {
            args[0] = val_nil();
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_nil();
            return 1;
        }
        char buf[4096];
        if (!fgets(buf, sizeof(buf), fp))
        {
            args[0] = val_nil();
            return 1;
        }
        int len = (int)strlen(buf);
        /* Strip trailing newline */
        if (len > 0 && buf[len - 1] == '\n')
            len--;
        if (len > 0 && buf[len - 1] == '\r')
            len--;
        args[0] = val_obj((Obj *)vm->make_string(buf, len));
        return 1;
    }

    /* =========================================================
    ** write_line(handle, str) → bool
    ** Writes string + newline
    ** ========================================================= */
    static int nat_file_write_line(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_int(args[0]) || !is_string(args[1]))
        {
            args[0] = val_bool(false);
            return 1;
        }
        FILE *fp = get_fp((int)args[0].as.integer);
        if (!fp)
        {
            args[0] = val_bool(false);
            return 1;
        }
        ObjString *s = as_string(args[1]);
        if (s->length > 0)
            fwrite(s->chars, 1, s->length, fp);
        fputc('\n', fp);
        args[0] = val_bool(true);
        return 1;
    }

    /* =========================================================
    ** Registration
    ** ========================================================= */

    static const NativeReg file_functions[] = {
        {"open", nat_file_open, -1},
        {"close", nat_file_close, 1},
        {"flush", nat_file_flush, 1},
        {"seek", nat_file_seek, 2},
        {"tell", nat_file_tell, 1},
        {"size", nat_file_size, 1},
        {"eof", nat_file_eof, 1},
        {"read_line", nat_file_read_line, 1},
        {"write_line", nat_file_write_line, 2},
        {"write_byte", nat_file_write_byte, 2},
        {"write_short", nat_file_write_short, 2},
        {"write_int", nat_file_write_int, 2},
        {"write_long", nat_file_write_long, 2},
        {"write_float", nat_file_write_float, 2},
        {"write_double", nat_file_write_double, 2},
        {"write_string", nat_file_write_string, 2},
        {"write_bytes", nat_file_write_bytes, 2},
        {"write_buffer", nat_file_write_buffer, 2},
        {"read_byte", nat_file_read_byte, 1},
        {"read_short", nat_file_read_short, 1},
        {"read_int", nat_file_read_int, 1},
        {"read_long", nat_file_read_long, 1},
        {"read_float", nat_file_read_float, 1},
        {"read_double", nat_file_read_double, 1},
        {"read_string", nat_file_read_string, 1},
        {"read_bytes", nat_file_read_bytes, 2},
        {"read_buffer", nat_file_read_buffer, 2},
        {"read_all", nat_file_read_all, 1},
    };

    const NativeLib zen_lib_file = {
        "file",
        file_functions,
        28, /* num_functions */
        nullptr,
        0,
    };

} /* namespace zen */
