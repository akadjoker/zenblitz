#ifdef ZEN_ENABLE_ZIP

/* =========================================================
** builtin_zip.cpp — "zip" module for Zen
**
** ZIP archive manipulation via miniz (vendor/).
**
** Usage:
**   import zip;
**   var entries = zip.list("archive.zip");
**   var data = zip.read("archive.zip", "file.txt");
**   zip.extract("archive.zip", "./output/");
**   zip.create("out.zip", ["file1.txt", "file2.bin"], 6);
** ========================================================= */

#include "module.h"
#include "vm.h"
#include "miniz.h"
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#ifdef _WIN32
#include <direct.h>
#define zen_mkdir(path, mode) _mkdir(path)
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#else
#include <unistd.h>
#define zen_mkdir(path, mode) mkdir((path), (mode))
#endif

namespace zen
{

    /* =========================================================
    ** Helpers
    ** ========================================================= */

    static bool ensure_dir(const char *path)
    {
        struct stat st;
        if (stat(path, &st) == 0)
            return S_ISDIR(st.st_mode);
        return zen_mkdir(path, 0755) == 0;
    }

    static bool ensure_dir_recursive(const char *path, int len)
    {
        char buf[1024];
        if (len <= 0 || len >= (int)sizeof(buf))
            return false;
        memcpy(buf, path, (size_t)len);
        buf[len] = '\0';

        for (int i = 1; i < len; i++)
        {
            if (buf[i] == '/')
            {
                buf[i] = '\0';
                ensure_dir(buf);
                buf[i] = '/';
            }
        }
        return ensure_dir(buf);
    }

    static bool is_unsafe_entry(const char *name)
    {
        if (!name || name[0] == '\0' || name[0] == '/')
            return true;
        /* Check for ".." path traversal */
        const char *p = name;
        while (*p)
        {
            if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0'))
                return true;
            /* Skip to next component */
            while (*p && *p != '/')
                p++;
            if (*p == '/')
                p++;
        }
        return false;
    }

    /* =========================================================
    ** list(archive_path) → array of entry names, or nil
    ** ========================================================= */
    static int nat_zip_list(VM *vm, Value *args, int nargs)
    {
        if (nargs < 1 || !is_string(args[0]))
        {
            vm->runtime_error("zip.list() expects (archive_path).");
            return -1;
        }

        mz_zip_archive zip;
        memset(&zip, 0, sizeof(zip));

        if (!mz_zip_reader_init_file(&zip, as_cstring(args[0]), 0))
        {
            args[0] = val_nil();
            return 1;
        }

        ObjArray *arr = new_array(&vm->get_gc());
        mz_uint n = mz_zip_reader_get_num_files(&zip);
        for (mz_uint i = 0; i < n; i++)
        {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&zip, i, &st))
                continue;
            Value name = val_obj((Obj *)vm->make_string(st.m_filename, (int)strlen(st.m_filename)));
            array_push(&vm->get_gc(), arr, name);
        }

        mz_zip_reader_end(&zip);
        args[0] = val_obj((Obj *)arr);
        return 1;
    }

    /* =========================================================
    ** read(archive_path, entry_name) → string or nil
    ** ========================================================= */
    static int nat_zip_read(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_string(args[0]) || !is_string(args[1]))
        {
            vm->runtime_error("zip.read() expects (archive, entry).");
            return -1;
        }

        mz_zip_archive zip;
        memset(&zip, 0, sizeof(zip));

        if (!mz_zip_reader_init_file(&zip, as_cstring(args[0]), 0))
        {
            args[0] = val_nil();
            return 1;
        }

        size_t size = 0;
        void *data = mz_zip_reader_extract_file_to_heap(&zip, as_cstring(args[1]), &size, 0);
        if (!data)
        {
            mz_zip_reader_end(&zip);
            args[0] = val_nil();
            return 1;
        }

        args[0] = val_obj((Obj *)vm->make_string((const char *)data, (int)size));
        mz_free(data);
        mz_zip_reader_end(&zip);
        return 1;
    }

    /* =========================================================
    ** read_buffer(archive_path, entry_name) → buffer or nil
    ** ========================================================= */
    static int nat_zip_read_buffer(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_string(args[0]) || !is_string(args[1]))
        {
            vm->runtime_error("zip.read_buffer() expects (archive, entry).");
            return -1;
        }

        mz_zip_archive zip;
        memset(&zip, 0, sizeof(zip));

        if (!mz_zip_reader_init_file(&zip, as_cstring(args[0]), 0))
        {
            args[0] = val_nil();
            return 1;
        }

        size_t size = 0;
        void *data = mz_zip_reader_extract_file_to_heap(&zip, as_cstring(args[1]), &size, 0);
        if (!data)
        {
            mz_zip_reader_end(&zip);
            args[0] = val_nil();
            return 1;
        }

        /* Create Uint8 buffer */
        ObjBuffer *buf = new_buffer(&vm->get_gc(), BUF_UINT8, (int32_t)size);
        if (size > 0)
            memcpy(buf->data, data, size);

        mz_free(data);
        mz_zip_reader_end(&zip);
        args[0] = val_obj((Obj *)buf);
        return 1;
    }

    /* =========================================================
    ** extract(archive_path, output_dir) → bool
    ** ========================================================= */
    static int nat_zip_extract(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_string(args[0]) || !is_string(args[1]))
        {
            vm->runtime_error("zip.extract() expects (archive, output_dir).");
            return -1;
        }

        const char *out_dir = as_cstring(args[1]);
        int out_dir_len = (int)strlen(out_dir);

        mz_zip_archive zip;
        memset(&zip, 0, sizeof(zip));

        if (!mz_zip_reader_init_file(&zip, as_cstring(args[0]), 0))
        {
            args[0] = val_bool(false);
            return 1;
        }

        ensure_dir_recursive(out_dir, out_dir_len);

        bool ok = true;
        mz_uint n = mz_zip_reader_get_num_files(&zip);
        char path_buf[2048];

        for (mz_uint i = 0; i < n && ok; i++)
        {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&zip, i, &st))
            {
                ok = false;
                break;
            }

            if (is_unsafe_entry(st.m_filename))
            {
                ok = false;
                break;
            }

            int entry_len = (int)strlen(st.m_filename);
            if (out_dir_len + 1 + entry_len >= (int)sizeof(path_buf))
            {
                ok = false;
                break;
            }

            memcpy(path_buf, out_dir, (size_t)out_dir_len);
            path_buf[out_dir_len] = '/';
            memcpy(path_buf + out_dir_len + 1, st.m_filename, (size_t)entry_len + 1);

            if (st.m_is_directory)
            {
                ensure_dir_recursive(path_buf, out_dir_len + 1 + entry_len);
                continue;
            }

            /* Ensure parent dir exists */
            int last_slash = -1;
            for (int j = 0; path_buf[j]; j++)
                if (path_buf[j] == '/')
                    last_slash = j;
            if (last_slash > 0)
                ensure_dir_recursive(path_buf, last_slash);

            if (!mz_zip_reader_extract_to_file(&zip, i, path_buf, 0))
                ok = false;
        }

        mz_zip_reader_end(&zip);
        args[0] = val_bool(ok);
        return 1;
    }

    /* =========================================================
    ** create(archive_path, files_array, level?) → bool
    ** files_array: array of file paths to add
    ** level: compression 0-10 (default 6)
    ** ========================================================= */
    static int nat_zip_create(VM *vm, Value *args, int nargs)
    {
        if (nargs < 2 || !is_string(args[0]) || !is_array(args[1]))
        {
            vm->runtime_error("zip.create() expects (archive, files_array, level?).");
            return -1;
        }

        int level = 6;
        if (nargs >= 3 && is_int(args[2]))
        {
            level = (int)args[2].as.integer;
            if (level < 0) level = 0;
            if (level > 10) level = 10;
        }

        ObjArray *files = as_array(args[1]);
        int file_count = arr_count(files);

        mz_zip_archive zip;
        memset(&zip, 0, sizeof(zip));

        if (!mz_zip_writer_init_file(&zip, as_cstring(args[0]), 0))
        {
            args[0] = val_bool(false);
            return 1;
        }

        bool ok = true;
        for (int i = 0; i < file_count && ok; i++)
        {
            Value item = files->data[i];
            if (!is_string(item))
            {
                ok = false;
                break;
            }
            const char *src_path = as_cstring(item);

            /* Use basename as archive entry name */
            const char *base = src_path;
            for (const char *p = src_path; *p; p++)
                if (*p == '/')
                    base = p + 1;

            if (!mz_zip_writer_add_file(&zip, base, src_path, NULL, 0, (mz_uint)level))
                ok = false;
        }

        if (ok && !mz_zip_writer_finalize_archive(&zip))
            ok = false;

        mz_zip_writer_end(&zip);
        args[0] = val_bool(ok);
        return 1;
    }

    /* =========================================================
    ** Registration
    ** ========================================================= */

    static const NativeReg zip_functions[] = {
        {"list", nat_zip_list, 1},
        {"read", nat_zip_read, 2},
        {"read_buffer", nat_zip_read_buffer, 2},
        {"extract", nat_zip_extract, 2},
        {"create", nat_zip_create, -1},
    };

    const NativeLib zen_lib_zip = {
        "zip",
        zip_functions,
        5,
        nullptr,
        0,
    };

} /* namespace zen */

#endif /* ZEN_ENABLE_ZIP */
