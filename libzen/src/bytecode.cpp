#include "bytecode.h"
#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <ct/string.hpp>
#include <climits>

namespace zen
{
namespace
{
    enum BytecodeFlags : uint32_t
    {
        BC_STRIP_DEBUG = 1u << 0,
    };

    enum ConstantTag : uint8_t
    {
        BC_NIL = 0,
        BC_BOOL = 1,
        BC_INT64 = 2,
        BC_FLOAT64 = 3,
        BC_STRING = 4,
        BC_FUNC = 5,
        BC_STRUCT_DEF = 8,
        BC_ARRAY = 9,
    };

    static void set_error(char *err, int err_len, const char *fmt, ...)
    {
        if (!err || err_len <= 0)
            return;

        va_list args;
        va_start(args, fmt);
        vsnprintf(err, (size_t)err_len, fmt, args);
        va_end(args);
        err[err_len - 1] = '\0';
    }

/* The writer serialises a compiled program to a .zbc file, which only the
** compiler ever does — a runtime-only build (ZEN_NO_BYTECODE_WRITER, set for
** the zen_vm library) drops it along with the stdio calls it needs, leaving
** just the loader that works off a memory buffer. */
#ifndef ZEN_NO_BYTECODE_WRITER
    class BytecodeWriter
    {
    public:
        explicit BytecodeWriter(FILE *file) : file_(file), ok_(true) {}

        bool ok() const { return ok_; }

        bool write_raw(const void *data, size_t size)
        {
            if (!ok_)
                return false;
            if (size == 0)
                return true;
            if (!data || fwrite(data, 1, size, file_) != size)
            {
                ok_ = false;
                return false;
            }
            return true;
        }

        bool write_u8(uint8_t value)
        {
            return write_raw(&value, sizeof(value));
        }

        bool write_u16(uint16_t value)
        {
            uint8_t data[2];
            data[0] = (uint8_t)(value & 0xffu);
            data[1] = (uint8_t)((value >> 8u) & 0xffu);
            return write_raw(data, sizeof(data));
        }

        bool write_u32(uint32_t value)
        {
            uint8_t data[4];
            data[0] = (uint8_t)(value & 0xffu);
            data[1] = (uint8_t)((value >> 8u) & 0xffu);
            data[2] = (uint8_t)((value >> 16u) & 0xffu);
            data[3] = (uint8_t)((value >> 24u) & 0xffu);
            return write_raw(data, sizeof(data));
        }

        bool write_i32(int32_t value)
        {
            return write_u32((uint32_t)value);
        }

        bool write_i64(int64_t value)
        {
            uint64_t bits = (uint64_t)value;
            uint8_t data[8];
            for (int i = 0; i < 8; i++)
                data[i] = (uint8_t)((bits >> (i * 8)) & 0xffu);
            return write_raw(data, sizeof(data));
        }

        bool write_f64(double value)
        {
            uint64_t bits = 0;
            static_assert(sizeof(bits) == sizeof(value), "double must be 64-bit");
            memcpy(&bits, &value, sizeof(bits));
            uint8_t data[8];
            for (int i = 0; i < 8; i++)
                data[i] = (uint8_t)((bits >> (i * 8)) & 0xffu);
            return write_raw(data, sizeof(data));
        }

    private:
        FILE *file_;
        bool ok_;
    };
#endif /* ZEN_NO_BYTECODE_WRITER */

    class BytecodeReader
    {
    public:
        BytecodeReader(const uint8_t *data, size_t size)
            : data_(data), size_(size), offset_(0), ok_(true) {}

        bool ok() const { return ok_; }

        bool read_raw(void *out, size_t size)
        {
            if (!ok_)
                return false;
            if (size == 0)
                return true;
            if (!out || offset_ > size_ || size > size_ - offset_)
            {
                ok_ = false;
                return false;
            }
            memcpy(out, data_ + offset_, size);
            offset_ += size;
            return true;
        }

        bool read_u8(uint8_t *out)
        {
            return out && read_raw(out, sizeof(*out));
        }

        bool read_u16(uint16_t *out)
        {
            uint8_t data[2];
            if (!out || !read_raw(data, sizeof(data)))
                return false;
            *out = (uint16_t)data[0] | ((uint16_t)data[1] << 8u);
            return true;
        }

        bool read_u32(uint32_t *out)
        {
            uint8_t data[4];
            if (!out || !read_raw(data, sizeof(data)))
                return false;
            *out = ((uint32_t)data[0]) |
                   ((uint32_t)data[1] << 8u) |
                   ((uint32_t)data[2] << 16u) |
                   ((uint32_t)data[3] << 24u);
            return true;
        }

        bool read_i32(int32_t *out)
        {
            uint32_t value = 0;
            if (!out || !read_u32(&value))
                return false;
            *out = (int32_t)value;
            return true;
        }

        bool read_i64(int64_t *out)
        {
            uint8_t data[8];
            if (!out || !read_raw(data, sizeof(data)))
                return false;
            uint64_t bits = 0;
            for (int i = 0; i < 8; i++)
                bits |= ((uint64_t)data[i]) << (i * 8);
            *out = (int64_t)bits;
            return true;
        }

        bool read_f64(double *out)
        {
            uint8_t data[8];
            if (!out || !read_raw(data, sizeof(data)))
                return false;
            uint64_t bits = 0;
            for (int i = 0; i < 8; i++)
                bits |= ((uint64_t)data[i]) << (i * 8);
            memcpy(out, &bits, sizeof(*out));
            return true;
        }

    private:
        const uint8_t *data_;
        size_t size_;
        size_t offset_;
        bool ok_;
    };

#ifndef ZEN_NO_BYTECODE_WRITER
    static bool write_string(BytecodeWriter &w, ObjString *str, BytecodeStats *stats, char *err, int err_len)
    {
        if (!str)
        {
            set_error(err, err_len, "missing string");
            return false;
        }
        if (str->length < 0)
        {
            set_error(err, err_len, "invalid string length");
            return false;
        }
        if (stats)
            stats->strings++;
        if (!w.write_u32((uint32_t)str->length))
            return false;
        return str->length == 0 || w.write_raw(str->chars, (size_t)str->length);
    }

    static bool write_optional_string(BytecodeWriter &w, ObjString *str, BytecodeStats *stats, char *err, int err_len)
    {
        if (!w.write_u8(str ? 1 : 0))
            return false;
        if (!str)
            return true;
        return write_string(w, str, stats, err, err_len);
    }

    static bool write_func(BytecodeWriter &w, ObjFunc *fn, bool strip_debug, BytecodeStats *stats, char *err, int err_len);
    static bool write_value(BytecodeWriter &w, Value value, bool strip_debug, BytecodeStats *stats, char *err, int err_len);

    static bool should_write_global_value(Value value)
    {
        /* Everything the compiler set up as program state: numbers, strings,
           arrays (Data), struct defs (Types). Natives and functions are not
           written: natives are re-registered by install_runtime() and
           functions are installed by the main program itself. */
        if (value.type == VAL_INT || value.type == VAL_FLOAT || value.type == VAL_BOOL)
            return true;
        if (value.type != VAL_OBJ || !value.as.obj)
            return false;
        ObjType t = value.as.obj->type;
        return t == OBJ_STRUCT_DEF || t == OBJ_STRING || t == OBJ_ARRAY;
    }

    static bool write_global_names(BytecodeWriter &w, VM *vm, bool strip_debug, BytecodeStats *stats, char *err, int err_len)
    {
        uint32_t count = vm ? (uint32_t)vm->num_globals() : 0u;
        if (stats)
            stats->globals = count;
        if (!w.write_u32(count))
            return false;

        for (uint32_t i = 0; i < count; i++)
        {
            const char *name = vm->global_name((int)i);
            if (!w.write_u8(name ? 1 : 0))
                return false;
            if (!name)
            {
                if (!w.write_u8(0))
                    return false;
                continue;
            }

            size_t len = strlen(name);
            if (len > (size_t)UINT32_MAX)
            {
                set_error(err, err_len, "global name is too large");
                return false;
            }
            if (!w.write_u32((uint32_t)len) ||
                (len > 0 && !w.write_raw(name, len)))
                return false;

            Value value = vm->get_global((int)i);
            bool has_value = should_write_global_value(value);
            if (!w.write_u8(has_value ? 1 : 0))
                return false;
            if (has_value && !write_value(w, value, strip_debug, stats, err, err_len))
                return false;
        }

        return true;
    }


    static bool write_value(BytecodeWriter &w, Value value, bool strip_debug, BytecodeStats *stats, char *err, int err_len)
    {
        if (stats)
            stats->constants++;
        switch (value.type)
        {
        case VAL_NIL:
            return w.write_u8(BC_NIL);
        case VAL_BOOL:
            return w.write_u8(BC_BOOL) && w.write_u8(value.as.boolean ? 1 : 0);
        case VAL_INT:
            return w.write_u8(BC_INT64) && w.write_i64(value.as.integer);
        case VAL_FLOAT:
            return w.write_u8(BC_FLOAT64) && w.write_f64(value.as.number);
        case VAL_OBJ:
            if (!value.as.obj)
            {
                set_error(err, err_len, "null object constant");
                return false;
            }
            if (value.as.obj->type == OBJ_STRING)
                return w.write_u8(BC_STRING) && write_string(w, (ObjString *)value.as.obj, stats, err, err_len);
            if (value.as.obj->type == OBJ_FUNC)
                return w.write_u8(BC_FUNC) && write_func(w, (ObjFunc *)value.as.obj, strip_debug, stats, err, err_len);
            if (value.as.obj->type == OBJ_ARRAY)
            {
                ObjArray *arr = (ObjArray *)value.as.obj;
                int32_t n = arr_count(arr);
                if (!w.write_u8(BC_ARRAY) || !w.write_u32((uint32_t)n))
                    return false;
                for (int32_t i = 0; i < n; i++)
                    if (!write_value(w, arr->data[i], strip_debug, stats, err, err_len))
                        return false;
                return true;
            }
            if (value.as.obj->type == OBJ_STRUCT_DEF)
            {
                ObjStructDef *sd = (ObjStructDef *)value.as.obj;
                if (!w.write_u8(BC_STRUCT_DEF))
                    return false;
                if (!write_string(w, sd->name, stats, err, err_len))
                    return false;
                if (!w.write_u32((uint32_t)sd->num_fields))
                    return false;
                for (int i = 0; i < sd->num_fields; i++)
                {
                    if (!write_string(w, sd->field_names[i], stats, err, err_len))
                        return false;
                }
                return true;
            }
            set_error(err, err_len, "unsupported object constant type %d", (int)value.as.obj->type);
            return false;
        case VAL_PTR:
            set_error(err, err_len, "cannot dump raw pointer constant");
            return false;
        }

        set_error(err, err_len, "unsupported constant value type %d", (int)value.type);
        return false;
    }

    static bool write_func(BytecodeWriter &w, ObjFunc *fn, bool strip_debug, BytecodeStats *stats, char *err, int err_len)
    {
        if (!fn)
        {
            set_error(err, err_len, "null function");
            return false;
        }

        if (stats)
        {
            stats->functions++;
            stats->instructions += fn->code_count > 0 ? (uint32_t)fn->code_count : 0u;
        }

        if (!w.write_i32(fn->arity) ||
            !w.write_i32(fn->num_regs) ||
            !w.write_i32(fn->code_count) ||
            !w.write_i32(strip_debug ? 0 : fn->code_count) ||
            !w.write_i32(fn->const_count))
            return false;

        for (int32_t i = 0; i < fn->code_count; i++)
        {
            if (!w.write_u32(fn->code ? fn->code[i] : 0))
                return false;
        }

        if (!strip_debug)
        {
            for (int32_t i = 0; i < fn->code_count; i++)
            {
                if (!w.write_i32(fn->lines ? fn->lines[i] : 0))
                    return false;
            }
        }

        for (int32_t i = 0; i < fn->const_count; i++)
        {
            if (!write_value(w, fn->constants[i], strip_debug, stats, err, err_len))
                return false;
        }


        if (!write_optional_string(w, strip_debug ? nullptr : fn->name, stats, err, err_len) ||
            !write_optional_string(w, strip_debug ? nullptr : fn->source, stats, err, err_len))
            return false;

        return true;
    }
#endif /* ZEN_NO_BYTECODE_WRITER */



    static bool read_string(VM *vm, BytecodeReader &r, ObjString **out, char *err, int err_len)
    {
        uint32_t len = 0;
        if (!r.read_u32(&len))
        {
            set_error(err, err_len, "failed to read string length");
            return false;
        }
        if (len > (uint32_t)INT32_MAX)
        {
            set_error(err, err_len, "string is too large");
            return false;
        }

        ct::String buffer;
        buffer.resize((size_t)len);
        if (len > 0 && !r.read_raw(&buffer[0], (size_t)len))
        {
            set_error(err, err_len, "truncated string");
            return false;
        }

        *out = vm->make_string(len > 0 ? buffer.data() : "", (int)len);
        return *out != nullptr;
    }

    static bool read_optional_string(VM *vm, BytecodeReader &r, ObjString **out, char *err, int err_len)
    {
        uint8_t present = 0;
        if (!r.read_u8(&present))
        {
            set_error(err, err_len, "failed to read optional string flag");
            return false;
        }
        if (present == 0)
        {
            *out = nullptr;
            return true;
        }
        if (present != 1)
        {
            set_error(err, err_len, "invalid optional string flag %u", (unsigned)present);
            return false;
        }
        return read_string(vm, r, out, err, err_len);
    }

    static ObjFunc *read_func(VM *vm, BytecodeReader &r, uint16_t minor, char *err, int err_len);
    static bool read_value(VM *vm, BytecodeReader &r, uint16_t minor, Value *out, char *err, int err_len);


    static bool read_global_names(VM *vm, BytecodeReader &r, uint16_t minor, char *err, int err_len)
    {
        (void)minor;
        uint32_t count = 0;
        if (!r.read_u32(&count))
        {
            set_error(err, err_len, "truncated global name table");
            return false;
        }

        if (count > (uint32_t)kMaxGlobalsHard)
        {
            set_error(err, err_len, "too many globals in bytecode");
            return false;
        }

        for (uint32_t i = 0; i < count; i++)
        {
            uint8_t present = 0;
            if (!r.read_u8(&present))
            {
                set_error(err, err_len, "truncated global name flag");
                return false;
            }

            ObjString *name = nullptr;
            if (present == 1)
            {
                if (!read_string(vm, r, &name, err, err_len))
                    return false;
            }
            else if (present != 0)
            {
                set_error(err, err_len, "invalid global name flag %u", (unsigned)present);
                return false;
            }

            if (i < (uint32_t)vm->num_globals())
            {
                const char *existing = vm->global_name((int)i);
                if ((existing || name) && (!existing || !name || strcmp(existing, name->chars) != 0))
                {
                    set_error(err, err_len, "global mismatch at slot %u", (unsigned)i);
                    return false;
                }
            }
            else
            {
                if (!name)
                {
                    set_error(err, err_len, "missing global name at new slot %u", (unsigned)i);
                    return false;
                }

                int idx = vm->def_global(name->chars, val_nil());
                if (idx != (int)i)
                {
                    set_error(err, err_len, "global slot mismatch for '%s'", name->chars);
                    return false;
                }
            }

            uint8_t has_value = 0;
            if (!r.read_u8(&has_value))
            {
                set_error(err, err_len, "truncated global value flag");
                return false;
            }
            if (has_value == 1)
            {
                Value value = val_nil();
                if (!read_value(vm, r, minor, &value, err, err_len))
                    return false;
                vm->set_global((int)i, value);
            }
            else if (has_value != 0)
            {
                set_error(err, err_len, "invalid global value flag %u", (unsigned)has_value);
                return false;
            }
        }

        return true;
    }

    static bool read_value(VM *vm, BytecodeReader &r, uint16_t minor, Value *out, char *err, int err_len)
    {
        uint8_t tag = 0;
        if (!r.read_u8(&tag))
        {
            set_error(err, err_len, "failed to read constant tag");
            return false;
        }

        switch (tag)
        {
        case BC_NIL:
            *out = val_nil();
            return true;
        case BC_BOOL:
        {
            uint8_t b = 0;
            if (!r.read_u8(&b))
                return false;
            *out = val_bool(b != 0);
            return true;
        }
        case BC_INT64:
        {
            int64_t value = 0;
            if (!r.read_i64(&value))
                return false;
            *out = val_int(value);
            return true;
        }
        case BC_FLOAT64:
        {
            double value = 0.0;
            if (!r.read_f64(&value))
                return false;
            *out = val_float(value);
            return true;
        }
        case BC_STRING:
        {
            ObjString *str = nullptr;
            if (!read_string(vm, r, &str, err, err_len))
                return false;
            *out = val_obj((Obj *)str);
            return true;
        }
        case BC_FUNC:
        {
            ObjFunc *fn = read_func(vm, r, minor, err, err_len);
            if (!fn)
                return false;
            *out = val_obj((Obj *)fn);
            return true;
        }
        case BC_ARRAY:
        {
            uint32_t n = 0;
            if (!r.read_u32(&n))
            {
                set_error(err, err_len, "truncated array constant");
                return false;
            }
            GC *gc = &vm->get_gc();
            ObjArray *arr = new_array(gc);
            *out = val_obj((Obj *)arr);
            array_reserve(gc, arr, (int32_t)n);
            for (uint32_t i = 0; i < n; i++)
            {
                Value v = val_nil();
                if (!read_value(vm, r, minor, &v, err, err_len))
                    return false;
                array_push(gc, arr, v);
            }
            return true;
        }
        case BC_STRUCT_DEF:
        {
            GC *gc = &vm->get_gc();
            ObjString *name = nullptr;
            if (!read_string(vm, r, &name, err, err_len))
                return false;
            uint32_t num_fields = 0;
            if (!r.read_u32(&num_fields))
            {
                set_error(err, err_len, "truncated struct_def field count");
                return false;
            }
            ObjStructDef *sd = new_struct_def(gc, name);
            sd->num_fields = (int32_t)num_fields;
            sd->field_names = (ObjString **)zen_alloc(gc, sizeof(ObjString *) * num_fields);
            for (uint32_t i = 0; i < num_fields; i++)
            {
                if (!read_string(vm, r, &sd->field_names[i], err, err_len))
                    return false;
            }
            *out = val_obj((Obj *)sd);
            return true;
        }
        default:
            set_error(err, err_len, "unsupported constant tag %u", (unsigned)tag);
            return false;
        }
    }

    static bool valid_non_negative_count(int32_t value, const char *what, char *err, int err_len)
    {
        if (value < 0)
        {
            set_error(err, err_len, "invalid negative %s", what);
            return false;
        }
        return true;
    }

    static ObjFunc *read_func(VM *vm, BytecodeReader &r, uint16_t minor, char *err, int err_len)
    {
        GC *gc = &vm->get_gc();

        int32_t arity = 0;
        int32_t num_regs = 0;
        int32_t code_count = 0;
        int32_t line_count = 0;
        int32_t const_count = 0;

        if (!r.read_i32(&arity) ||
            !r.read_i32(&num_regs) ||
            !r.read_i32(&code_count) ||
            !r.read_i32(&line_count) ||
            !r.read_i32(&const_count))
        {
            set_error(err, err_len, "truncated function header");
            return nullptr;
        }

        if (!valid_non_negative_count(code_count, "code count", err, err_len) ||
            !valid_non_negative_count(line_count, "line count", err, err_len) ||
            !valid_non_negative_count(const_count, "constant count", err, err_len))
            return nullptr;

        if (line_count != 0 && line_count != code_count)
        {
            set_error(err, err_len, "line table count mismatch");
            return nullptr;
        }

        ObjFunc *fn = new_func(gc);
        fn->arity = arity;
        fn->num_regs = num_regs;

        fn->code_count = code_count;
        fn->code_capacity = code_count;
        if (code_count > 0)
        {
            fn->code = (Instruction *)zen_alloc(gc, sizeof(Instruction) * (size_t)code_count);
            fn->lines = (int32_t *)zen_alloc(gc, sizeof(int32_t) * (size_t)code_count);
        }

        for (int32_t i = 0; i < code_count; i++)
        {
            uint32_t instr = 0;
            if (!r.read_u32(&instr))
            {
                set_error(err, err_len, "truncated code");
                return nullptr;
            }
            fn->code[i] = instr;
            fn->lines[i] = 0;
        }

        for (int32_t i = 0; i < line_count; i++)
        {
            if (!r.read_i32(&fn->lines[i]))
            {
                set_error(err, err_len, "truncated line table");
                return nullptr;
            }
        }

        fn->const_count = const_count;
        fn->const_capacity = const_count;
        if (const_count > 0)
        {
            fn->constants = (Value *)zen_alloc(gc, sizeof(Value) * (size_t)const_count);
            for (int32_t i = 0; i < const_count; i++)
                fn->constants[i] = val_nil();
        }

        for (int32_t i = 0; i < const_count; i++)
        {
            if (!read_value(vm, r, minor, &fn->constants[i], err, err_len))
                return nullptr;
        }

        if (!read_optional_string(vm, r, &fn->name, err, err_len) ||
            !read_optional_string(vm, r, &fn->source, err, err_len))
            return nullptr;

        return fn;
    }


} /* namespace */

bool is_bytecode_buffer(const uint8_t *data, size_t size)
{
    return data && size >= sizeof(ZEN_BYTECODE_MAGIC) &&
           memcmp(data, ZEN_BYTECODE_MAGIC, sizeof(ZEN_BYTECODE_MAGIC)) == 0;
}

#ifndef ZEN_NO_BYTECODE_WRITER
bool dump_bytecode_file(ObjFunc *func, const char *path, bool strip_debug, char *err, int err_len)
{
    return dump_bytecode_file(nullptr, func, path, strip_debug, err, err_len);
}

bool dump_bytecode_file(VM *vm, ObjFunc *func, const char *path, bool strip_debug, char *err, int err_len)
{
    return dump_bytecode_file(vm, func, path, strip_debug, nullptr, err, err_len);
}

bool dump_bytecode_file(VM *vm, ObjFunc *func, const char *path, bool strip_debug,
                        BytecodeStats *stats, char *err, int err_len)
{
    if (!func)
    {
        set_error(err, err_len, "null function");
        return false;
    }
    if (!path || path[0] == '\0')
    {
        set_error(err, err_len, "invalid output path");
        return false;
    }

    ct::String temp_path(path);
    temp_path += ".tmp";
    FILE *file = fopen(temp_path.c_str(), "wb");
    if (!file)
    {
        set_error(err, err_len, "cannot open '%s' for writing", temp_path.c_str());
        return false;
    }

    BytecodeWriter w(file);
    if (stats)
        *stats = BytecodeStats{};
    uint32_t flags = strip_debug ? BC_STRIP_DEBUG : 0u;
    bool ok = w.write_raw(ZEN_BYTECODE_MAGIC, sizeof(ZEN_BYTECODE_MAGIC)) &&
              w.write_u16(ZEN_BYTECODE_VERSION_MAJOR) &&
              w.write_u16(ZEN_BYTECODE_VERSION_MINOR) &&
              w.write_u32(flags) &&
              write_global_names(w, vm, strip_debug, stats, err, err_len) &&
              write_func(w, func, strip_debug, stats, err, err_len);

    if (!ok || !w.ok())
    {
        if (err && err[0] == '\0')
            set_error(err, err_len, "failed to write bytecode");
        fclose(file);
        remove(temp_path.c_str());
        return false;
    }

    if (fflush(file) != 0 || fclose(file) != 0)
    {
        set_error(err, err_len, "failed to flush bytecode file");
        remove(temp_path.c_str());
        return false;
    }

    if (rename(temp_path.c_str(), path) != 0)
    {
#ifdef _WIN32
        remove(path);
        if (rename(temp_path.c_str(), path) == 0)
            return true;
#endif
        set_error(err, err_len, "failed to replace bytecode file");
        remove(temp_path.c_str());
        return false;
    }

    if (stats)
    {
        FILE *in = fopen(path, "rb");
        if (in)
        {
            fseek(in, 0, SEEK_END);
            long size = ftell(in);
            fclose(in);
            stats->bytes = size > 0 ? (size_t)size : 0u;
        }
    }

    return true;
}
#endif /* ZEN_NO_BYTECODE_WRITER */

ObjFunc *load_bytecode_buffer(VM *vm, const uint8_t *data, size_t size, char *err, int err_len)
{
    if (!vm || !data)
    {
        set_error(err, err_len, "invalid bytecode input");
        return nullptr;
    }

    BytecodeReader r(data, size);
    uint8_t magic[sizeof(ZEN_BYTECODE_MAGIC)];
    uint16_t major = 0;
    uint16_t minor = 0;
    uint32_t flags = 0;

    if (!r.read_raw(magic, sizeof(magic)) ||
        memcmp(magic, ZEN_BYTECODE_MAGIC, sizeof(ZEN_BYTECODE_MAGIC)) != 0)
    {
        set_error(err, err_len, "invalid bytecode magic");
        return nullptr;
    }

    if (!r.read_u16(&major) || !r.read_u16(&minor) || !r.read_u32(&flags))
    {
        set_error(err, err_len, "truncated bytecode header");
        return nullptr;
    }

    if (major != ZEN_BYTECODE_VERSION_MAJOR ||
        minor > ZEN_BYTECODE_VERSION_MINOR)
    {
        set_error(err, err_len, "unsupported bytecode version %u.%u", (unsigned)major, (unsigned)minor);
        return nullptr;
    }

    if ((flags & ~BC_STRIP_DEBUG) != 0)
    {
        set_error(err, err_len, "unsupported bytecode flags 0x%x", (unsigned)flags);
        return nullptr;
    }

    /* Disable GC for the entire load: objects created during read_global_names /
       read_func are not yet reachable from any GC root, so a
       collection triggered by an allocation would sweep them prematurely.      */
    GC &gc = vm->get_gc();
    void *saved_vm = gc.vm;
    gc.vm = nullptr;

    if (!read_global_names(vm, r, minor, err, err_len))
    {
        gc.vm = saved_vm;
        return nullptr;
    }

    ObjFunc *fn = read_func(vm, r, minor, err, err_len);
    gc.vm = saved_vm;

    if (!fn)
    {
        if (err && err[0] == '\0')
            set_error(err, err_len, "failed to load bytecode");
        return nullptr;
    }

    return fn;
}

} /* namespace zen */
