#include "memory.h"
#include "vm.h"
#include "zen_arena.h"
#include <cmath>
#include <cstdlib>

namespace zen
{
    /* Forward declare for GC trigger */
    void gc_collect(VM *vm);

    /* =========================================================
    ** Allocator base — wrapper fino sobre malloc/realloc/free.
    ** Contabiliza bytes para trigger do GC.
    ** ========================================================= */

    /* Build with -DZEN_ARENA_BYPASS to route every GC allocation straight to
       the system allocator instead of the pool arena. Used to A/B the arena's
       value vs plain malloc. The GC-trigger accounting is identical either way. */
#ifdef ZEN_ARENA_BYPASS
    void *zen_alloc(GC *gc, size_t size)
    {
#ifdef ZEN_DEBUG_STRESS_GC
        if (gc->pause_depth == 0 && gc->vm)
            gc_collect((VM *)gc->vm);
#else
        if (gc->pause_depth == 0 && gc->vm && gc->bytes_allocated > gc->next_gc)
            gc_collect((VM *)gc->vm);
#endif
        gc->bytes_allocated += size;
        return malloc(size);
    }
    void *zen_alloc_now(GC *gc, size_t size)
    {
        gc->bytes_allocated += size;
        return malloc(size);
    }
    void *zen_realloc(GC *gc, void *ptr, size_t old_size, size_t new_size)
    {
#ifdef ZEN_DEBUG_STRESS_GC
        if (gc->pause_depth == 0 && gc->vm)
            gc_collect((VM *)gc->vm);
#else
        if (gc->pause_depth == 0 && gc->vm && gc->bytes_allocated > gc->next_gc)
            gc_collect((VM *)gc->vm);
#endif
        gc->bytes_allocated += (new_size > old_size) ? (new_size - old_size) : 0;
        gc->bytes_allocated -= (old_size > new_size) ? (old_size - new_size) : 0;
        return realloc(ptr, new_size);
    }
    void zen_free(GC *gc, void *ptr, size_t size)
    {
        gc->bytes_allocated -= size;
        free(ptr);
    }
#else
    void *zen_alloc(GC *gc, size_t size)
    {
        /* Trigger GC BEFORE allocation (so new objects won't be swept) */
#ifdef ZEN_DEBUG_STRESS_GC
        if (gc->pause_depth == 0 && gc->vm)
            gc_collect((VM *)gc->vm);
#else
        if (gc->pause_depth == 0 && gc->vm && gc->bytes_allocated > gc->next_gc)
            gc_collect((VM *)gc->vm);
#endif
        gc->bytes_allocated += size;
        return arena_alloc(&gc->arena, size);
    }
    void *zen_alloc_now(GC *gc, size_t size)
    {
        gc->bytes_allocated += size;
        return arena_alloc(&gc->arena, size);
    }

    void *zen_realloc(GC *gc, void *ptr, size_t old_size, size_t new_size)
    {
#ifdef ZEN_DEBUG_STRESS_GC
        if (gc->pause_depth == 0 && gc->vm)
            gc_collect((VM *)gc->vm);
#else
        if (gc->pause_depth == 0 && gc->vm && gc->bytes_allocated > gc->next_gc)
            gc_collect((VM *)gc->vm);
#endif
        gc->bytes_allocated += (new_size > old_size) ? (new_size - old_size) : 0;
        gc->bytes_allocated -= (old_size > new_size) ? (old_size - new_size) : 0;
        return arena_realloc(&gc->arena, ptr, old_size, new_size);
    }

    void zen_free(GC *gc, void *ptr, size_t size)
    {
        gc->bytes_allocated -= size;
        arena_free(&gc->arena, ptr, size);
    }
#endif

    bool objects_equal(Obj *a, Obj *b)
    {
        if (a == b) return true;
        if (!a || !b || a->type != b->type) return false;
        if (a->type == OBJ_STRING) {
            ObjString *sa = (ObjString *)a;
            ObjString *sb = (ObjString *)b;
            return sa->length == sb->length &&
                   memcmp(sa->chars, sb->chars, (size_t)sa->length) == 0;
        }
        return false;
    }

    /* =========================================================
    ** GC Init
    ** ========================================================= */

    void gc_init(GC *gc)
    {
        gc->objects = nullptr;
        gc->gray_list = nullptr;
        gc->gray_count = 0;
        gc->gray_capacity = 0;
        gc->bytes_allocated = 0;
        gc->next_gc = kGCInitThreshold;
        gc->pause_saved_next_gc = kGCInitThreshold;
        gc->pause_depth = 0;
        gc->vm = nullptr;
        gc->white_val = GC_WHITE;
        gc->black_val = GC_BLACK;
        gc->first_old = nullptr;

        arena_init(&gc->arena);

        /* String interning table — começa com 64 slots */
        gc->string_capacity = 64;
        gc->string_count = 0;
        gc->strings = (ObjString **)calloc(gc->string_capacity, sizeof(ObjString *));
        gc->bytes_allocated += sizeof(ObjString *) * gc->string_capacity;
    }

    void gc_pause(GC *gc)
    {
        if (!gc)
            return;
        if (gc->pause_depth++ == 0)
        {
            gc->pause_saved_next_gc = gc->next_gc;
            gc->next_gc = (size_t)-1;
        }
    }

    void gc_resume(GC *gc)
    {
        if (!gc || gc->pause_depth <= 0)
            return;
        if (--gc->pause_depth == 0)
            gc->next_gc = gc->pause_saved_next_gc;
    }

    /* =========================================================
    ** Alocação de objectos — regista na linked list do GC.
    ** ========================================================= */

    static Obj *alloc_obj(GC *gc, size_t size, ObjType type)
    {
        Obj *obj = (Obj *)zen_alloc(gc, size);
        obj->type = type;
        obj->color = gc->black_val; /* born black: survives current GC cycle */
        obj->interned = 0;
        obj->hash = 0;
        obj->gc_next = gc->objects;
        gc->objects = obj;
        return obj;
    }
    static Obj *alloc_obj_now(GC *gc, size_t size, ObjType type)
    {
        Obj *obj = (Obj *)zen_alloc_now(gc, size);
        obj->type = type;
        obj->color = gc->black_val;
        obj->interned = 0;
        obj->hash = 0;
        obj->gc_next = gc->objects;
        gc->objects = obj;
        return obj;
    }

    /* =========================================================
    ** Strings — interned, hash pré-calculado, buffer inline.
    ** ========================================================= */

    ObjString *find_interned(GC *gc, const char *chars, int length, uint32_t hash)
    {
        if (gc->string_count == 0)
            return nullptr;

        uint32_t idx = hash & (gc->string_capacity - 1);
        for (;;)
        {
            ObjString *s = gc->strings[idx];
            if (s == nullptr)
                return nullptr;
            if (s->obj.hash == hash && s->length == length &&
                memcmp(s->chars, chars, length) == 0)
            {
                return s;
            }
            idx = (idx + 1) & (gc->string_capacity - 1);
        }
    }

    static void intern_table_grow(GC *gc)
    {
        int new_cap = gc->string_capacity * 2;
        size_t old_bytes = sizeof(ObjString *) * gc->string_capacity;
        size_t new_bytes = sizeof(ObjString *) * new_cap;
        ObjString **new_table = (ObjString **)calloc(new_cap, sizeof(ObjString *));
        gc->bytes_allocated += new_bytes;

        /* re-hash tudo */
        for (int i = 0; i < gc->string_capacity; i++)
        {
            ObjString *s = gc->strings[i];
            if (s == nullptr)
                continue;
            uint32_t idx = s->obj.hash & (new_cap - 1);
            while (new_table[idx] != nullptr)
                idx = (idx + 1) & (new_cap - 1);
            new_table[idx] = s;
        }

        gc->bytes_allocated -= old_bytes;
        free(gc->strings);
        gc->strings = new_table;
        gc->string_capacity = new_cap;
    }

    static ObjString *alloc_string_raw(GC *gc, const char *chars, int length, uint32_t hash)
    {
        size_t total = sizeof(ObjString) + length + 1;
        ObjString *str = (ObjString *)alloc_obj(gc, total, OBJ_STRING);
        str->length = length;
        str->capacity = 0; /* tight allocation: obj_size uses length + 1 */
        memcpy(str->chars, chars, length);
        str->chars[length] = '\0';
        str->obj.hash = hash;
        return str;
    }

    ObjString *intern_string(GC *gc, const char *chars, int length, uint32_t hash)
    {
        /* Já existe? */
        ObjString *existing = find_interned(gc, chars, length, hash);
        if (existing)
            return existing;

        /* Grow se >75% — multiplication avoids division */
        if ((gc->string_count + 1) * 4 > gc->string_capacity * 3)
        {
            intern_table_grow(gc);
        }

        ObjString *str = alloc_string_raw(gc, chars, length, hash);
        str->obj.interned = 1;

        /* Insere na tabela */
        uint32_t idx = hash & (gc->string_capacity - 1);
        while (gc->strings[idx] != nullptr)
            idx = (idx + 1) & (gc->string_capacity - 1);
        gc->strings[idx] = str;
        gc->string_count++;

        return str;
    }

    ObjString *new_string_uninit(GC *gc, int length)
    {
        size_t total = sizeof(ObjString) + length + 1;
        ObjString *str = (ObjString *)alloc_obj(gc, total, OBJ_STRING);
        str->length = length;
        str->capacity = 0;
        str->chars[length] = '\0';
        str->obj.hash = 0; /* caller defines later */
        return str;
    }

    ObjString *new_string(GC *gc, const char *chars, int length)
    {
        uint32_t hash = hash_string(chars, length);
        return intern_string(gc, chars, length, hash);
    }

    ObjString *create_string(GC *gc, const char *chars, int length)
    {
        /* Hash lazily (see string_hash): most method results (split pieces,
           upper/lower, sub, …) are never used as map keys, so don't pay
           FNV over every byte up front. */
        return alloc_string_raw(gc, chars, length, 0);
    }

    ObjString *new_string_concat(GC *gc, ObjString *a, ObjString *b)
    {
        if (a->length == 0) return b;
        if (b->length == 0) return a;

        int length = a->length + b->length;

        /* Concatenation results are not interned. Interning them would cost
        ** a hash over every byte plus a table insertion, and every distinct
        ** result would stay in the intern table for the GC to walk — a loop
        ** building 200k distinct strings grows that table to 200k entries.
        ** Nothing needs it: objects_equal() compares strings by content, so
        ** "=" is correct either way, and literals (new_string) are still
        ** interned, which is what makes comparing them cheap. */
        int new_cap = (length + 1) * 2;
        new_cap = (new_cap + 7) & ~7;

        size_t total = sizeof(ObjString) + new_cap;
        ObjString *str = (ObjString *)alloc_obj_now(gc, total, OBJ_STRING);
        str->length = length;
        str->capacity = new_cap;
        memcpy(str->chars, a->chars, a->length);
        memcpy(str->chars + a->length, b->chars, b->length);
        str->chars[length] = '\0';
        str->obj.hash = 0; /* lazy — see string_hash() */
        return str;
    }

    ObjString *string_append_inplace(GC *gc, ObjString *a, ObjString *b)
    {
        if (a->obj.interned || a->length == 0)
            return new_string_concat(gc, a, b);
        if (b->length == 0)
            return a;

        int new_len = a->length + b->length;
        int needed = new_len + 1;

        if (a->capacity >= needed)
        {
            /* Fast path — espaço suficiente, zero alloc */
            memcpy(a->chars + a->length, b->chars, b->length);
            a->length = new_len;
            a->chars[new_len] = '\0';
            a->obj.hash = 0;
            return a;
        }

        /* Sem realloc — aloca nova string com capacidade generosa (2x needed) */
        /* Evita o patch O(n) da GC list completamente */
        int new_cap = needed * 2;
        new_cap = (new_cap + 7) & ~7;

        size_t total = sizeof(ObjString) + new_cap;
        ObjString *result = (ObjString *)alloc_obj(gc, total, OBJ_STRING);
        result->length = new_len;
        result->capacity = new_cap;
        memcpy(result->chars, a->chars, a->length);
        memcpy(result->chars + a->length, b->chars, b->length);
        result->chars[new_len] = '\0';
        result->obj.hash = 0;
        return result;
        /* 'a' fica orphan — GC trata dela no próximo sweep */
    }

    /* =========================================================
    ** Criação de objectos compostos
    ** ========================================================= */

    ObjFunc *new_func(GC *gc)
    {
        ObjFunc *fn = (ObjFunc *)alloc_obj(gc, sizeof(ObjFunc), OBJ_FUNC);
        fn->arity = 0;
        fn->num_regs = 0;
        fn->code_count = 0;
        fn->code_capacity = 0;
        fn->const_count = 0;
        fn->const_capacity = 0;
        fn->code = nullptr;
        fn->lines = nullptr;
        fn->constants = nullptr;
        fn->name = nullptr;
        fn->source = nullptr;
        return fn;
    }

    ObjNative *new_native(GC *gc, NativeFn fn, int arity, ObjString *name)
    {
        ObjNative *nat = (ObjNative *)alloc_obj(gc, sizeof(ObjNative), OBJ_NATIVE);
        nat->fn = fn;
        nat->arity = arity;
        nat->name = name;
        return nat;
    }


    ObjArray *new_array(GC *gc)
    {
        ObjArray *arr = (ObjArray *)alloc_obj(gc, sizeof(ObjArray), OBJ_ARRAY);
        arr->data = nullptr;
        arr->end = nullptr;
        arr->cap_end = nullptr;
        return arr;
    }

    ObjStructDef *new_struct_def(GC *gc, ObjString *name)
    {
        ObjStructDef *def = (ObjStructDef *)alloc_obj(gc, sizeof(ObjStructDef), OBJ_STRUCT_DEF);
        def->name = name;
        def->num_fields = 0;
        def->field_names = nullptr;
        return def;
    }

    ObjStruct *new_struct(GC *gc, ObjStructDef *def)
    {
        /* the field array allocation below could trigger a collection while
           the struct is not yet reachable — keep the GC off until it is set up */
        gc_pause(gc);
        ObjStruct *s = (ObjStruct *)alloc_obj(gc, sizeof(ObjStruct), OBJ_STRUCT);
        s->def = def;
        s->fields = def->num_fields > 0 ? (Value *)zen_alloc(gc, sizeof(Value) * (size_t)def->num_fields) : nullptr;
        for (int32_t i = 0; i < def->num_fields; i++)
            s->fields[i] = val_nil();
        gc_resume(gc);
        return s;
    }


    /* =========================================================
    ** Array Operations — end-pointer layout for minimal push cost.
    ** Growth: power-of-2, initial cap 8, doubles each time.
    ** ========================================================= */

    static inline int32_t grow_capacity(int32_t capacity, int32_t min_cap)
    {
        int32_t cap = capacity < min_cap ? min_cap : capacity;
        if (cap < 8)
            return 8;
        cap--;
        cap |= cap >> 1;
        cap |= cap >> 2;
        cap |= cap >> 4;
        cap |= cap >> 8;
        cap |= cap >> 16;
        cap++;
        return cap;
    }

    static inline void array_ensure_cap(GC *gc, ObjArray *arr, int32_t needed)
    {
        int32_t cur_cap = arr_capacity(arr);
        if (needed <= cur_cap)
            return;
        int32_t count = arr_count(arr);
        int32_t new_cap = grow_capacity(cur_cap, needed);
        arr->data = (Value *)zen_realloc(gc, arr->data,
                                         sizeof(Value) * cur_cap,
                                         sizeof(Value) * new_cap);
        arr->end = arr->data + count;
        arr->cap_end = arr->data + new_cap;
    }

    void array_push_slow(GC *gc, ObjArray *arr, Value val)
    {
        int32_t count = arr_count(arr);
        int32_t old_cap = arr_capacity(arr);
        int32_t new_cap = grow_capacity(old_cap, count + 1);
        arr->data = (Value *)zen_realloc(gc, arr->data,
                                         sizeof(Value) * old_cap,
                                         sizeof(Value) * new_cap);
        arr->end = arr->data + count;
        arr->cap_end = arr->data + new_cap;
        *arr->end++ = val;
        if (__builtin_expect(val.type == VAL_OBJ, 0))
            gc_write_barrier(gc, (Obj *)arr, val.as.obj);
    }

    void array_set(GC *gc, ObjArray *arr, int32_t index, Value val)
    {
        if ((uint32_t)index >= (uint32_t)arr_count(arr))
            return;
        arr->data[index] = val;
        if (is_obj(val))
            gc_write_barrier(gc, (Obj *)arr, val.as.obj);
    }

    Value array_pop(ObjArray *arr)
    {
        if (arr->end == arr->data)
            return val_nil();
        return *--arr->end;
    }

    void array_insert(GC *gc, ObjArray *arr, int32_t index, Value val)
    {
        int32_t count = arr_count(arr);
        if (index < 0)
            index = 0;
        if (index > count)
            index = count;
        array_ensure_cap(gc, arr, count + 1);
        /* memmove is overlap-safe */
        if (index < count)
        {
            memmove(arr->data + index + 1, arr->data + index,
                    (size_t)(count - index) * sizeof(Value));
        }
        arr->data[index] = val;
        arr->end++;
        if (is_obj(val))
            gc_write_barrier(gc, (Obj *)arr, val.as.obj);
    }

    void array_remove(ObjArray *arr, int32_t index)
    {
        int32_t count = arr_count(arr);
        if ((uint32_t)index >= (uint32_t)count)
            return;
        int32_t remaining = count - index - 1;
        if (remaining > 0)
        {
            memmove(arr->data + index, arr->data + index + 1,
                    (size_t)remaining * sizeof(Value));
        }
        arr->end--;
    }

    void array_clear(ObjArray *arr)
    {
        arr->end = arr->data;
    }

    void array_reserve(GC *gc, ObjArray *arr, int32_t cap)
    {
        int32_t cur_cap = arr_capacity(arr);
        if (cap <= cur_cap)
            return;
        int32_t count = arr_count(arr);
        arr->data = (Value *)zen_realloc(gc, arr->data,
                                         sizeof(Value) * cur_cap,
                                         sizeof(Value) * cap);
        arr->end = arr->data + count;
        arr->cap_end = arr->data + cap;
    }

    int32_t array_find(ObjArray *arr, Value val)
    {
        int32_t count = arr_count(arr);
        for (int32_t i = 0; i < count; i++)
        {
            if (values_equal(arr->data[i], val))
                return i;
        }
        return -1;
    }

    int32_t array_find_int(ObjArray *arr, int32_t target)
    {
        const Value *data = arr->data;
        const int32_t n = arr_count(arr);
        int32_t i = 0;
        for (; i + 8 <= n; i += 8)
        {
            __builtin_prefetch(data + i + 16, 0, 1);
            if (data[i + 0].as.integer == target)
                return i + 0;
            if (data[i + 1].as.integer == target)
                return i + 1;
            if (data[i + 2].as.integer == target)
                return i + 2;
            if (data[i + 3].as.integer == target)
                return i + 3;
            if (data[i + 4].as.integer == target)
                return i + 4;
            if (data[i + 5].as.integer == target)
                return i + 5;
            if (data[i + 6].as.integer == target)
                return i + 6;
            if (data[i + 7].as.integer == target)
                return i + 7;
        }
        for (; i < n; i++)
            if (data[i].as.integer == target)
                return i;
        return -1;
    }

    bool array_contains(ObjArray *arr, Value val)
    {
        return array_find(arr, val) >= 0;
    }

    void array_reverse(ObjArray *arr)
    {
        int32_t count = arr_count(arr);
        if (count < 2)
            return;
        Value *lo = arr->data;
        Value *hi = arr->end - 1;
        while (lo + 4 <= hi)
        {
            Value t0 = lo[0];
            lo[0] = hi[0];
            hi[0] = t0;
            Value t1 = lo[1];
            lo[1] = hi[-1];
            hi[-1] = t1;
            lo += 2;
            hi -= 2;
        }
        while (lo < hi)
        {
            Value t = *lo;
            *lo = *hi;
            *hi = t;
            lo++;
            hi--;
        }
    }

    /* Introsort: insertion sort for small partitions, quicksort for large */
    static void isort_int(Value *data, int32_t n)
    {
        for (int32_t i = 1; i < n; i++)
        {
            Value key = data[i];
            int32_t j = i - 1;
            while (j >= 0 && data[j].as.integer > key.as.integer)
            {
                data[j + 1] = data[j];
                j--;
            }
            data[j + 1] = key;
        }
    }

    static void qsort_int(Value *data, int32_t lo, int32_t hi)
    {
        while (lo < hi)
        {
            if (hi - lo < 16)
            {
                isort_int(data + lo, hi - lo + 1);
                return;
            }
            /* median-of-three pivot */
            int32_t mid = lo + (hi - lo) / 2;
            if (data[mid].as.integer < data[lo].as.integer)
            {
                Value t = data[lo];
                data[lo] = data[mid];
                data[mid] = t;
            }
            if (data[hi].as.integer < data[lo].as.integer)
            {
                Value t = data[lo];
                data[lo] = data[hi];
                data[hi] = t;
            }
            if (data[mid].as.integer < data[hi].as.integer)
            {
                Value t = data[mid];
                data[mid] = data[hi];
                data[hi] = t;
            }
            int32_t pivot = data[hi].as.integer;
            int32_t i = lo - 1, j = hi;
            for (;;)
            {
                do
                {
                    i++;
                } while (data[i].as.integer < pivot);
                do
                {
                    j--;
                } while (j > lo && data[j].as.integer > pivot);
                if (i >= j)
                    break;
                Value t = data[i];
                data[i] = data[j];
                data[j] = t;
            }
            Value t = data[i];
            data[i] = data[hi];
            data[hi] = t;
            /* Recurse smaller partition, iterate larger */
            if (i - lo < hi - i)
            {
                qsort_int(data, lo, i - 1);
                lo = i + 1;
            }
            else
            {
                qsort_int(data, i + 1, hi);
                hi = i - 1;
            }
        }
    }

    void array_sort_int(ObjArray *arr)
    {
        int32_t count = arr_count(arr);
        if (count < 2)
            return;
        qsort_int(arr->data, 0, count - 1);
    }

    void array_copy(GC *gc, ObjArray *dst, ObjArray *src)
    {
        int32_t n = arr_count(src);
        array_reserve(gc, dst, n);
        memcpy(dst->data, src->data, (size_t)n * sizeof(Value));
        dst->end = dst->data + n;
    }

    void array_push_n(GC *gc, ObjArray *arr, const Value *vals, int32_t n)
    {
        int32_t count = arr_count(arr);
        int32_t new_count = count + n;
        int32_t cur_cap = arr_capacity(arr);
        if (__builtin_expect(new_count > cur_cap, 0))
        {
            int32_t new_cap = grow_capacity(cur_cap, new_count);
            arr->data = (Value *)zen_realloc(gc, arr->data,
                                             sizeof(Value) * cur_cap,
                                             sizeof(Value) * new_cap);
            arr->end = arr->data + count;
            arr->cap_end = arr->data + new_cap;
        }
        memcpy(arr->end, vals, (size_t)n * sizeof(Value));
        arr->end += n;
        for (int32_t i = 0; i < n; i++)
            if (__builtin_expect(vals[i].type == VAL_OBJ, 0))
                gc_write_barrier(gc, (Obj *)arr, vals[i].as.obj);
    }

    void array_append(GC *gc, ObjArray *dst, ObjArray *src)
    {
        array_push_n(gc, dst, src->data, arr_count(src));
    }

    /* =========================================================
    ** ObjBuffer — Typed buffer operations
    ** ========================================================= */

    ObjBuffer *new_buffer(GC *gc, BufferType btype, int32_t count)
    {
        ObjBuffer *buf = (ObjBuffer *)alloc_obj(gc, sizeof(ObjBuffer), OBJ_BUFFER);
        buf->btype = btype;
        buf->count = count;
        buf->capacity = count;
        int elem_sz = buffer_elem_size[btype];
        size_t bytes = (size_t)count * elem_sz;
        buf->data = (uint8_t *)zen_alloc(gc, bytes);
        memset(buf->data, 0, bytes);
        return buf;
    }

    void buffer_set(ObjBuffer *buf, int32_t index, double val)
    {
        uint8_t *p = buf->data + (size_t)index * buffer_elem_size[buf->btype];
        auto wrap_unsigned = [](double v, double mod) -> uint64_t {
            if (!zen_isfinite(v))
                return 0;
            double wrapped = fmod(trunc(v), mod);
            if (wrapped < 0)
                wrapped += mod;
            return (uint64_t)wrapped;
        };
        auto wrap_signed = [&](double v, int bits) -> int64_t {
            double mod = ldexp(1.0, bits);
            uint64_t wrapped = wrap_unsigned(v, mod);
            uint64_t sign_bit = (uint64_t)1 << (bits - 1);
            if (wrapped >= sign_bit)
                return (int64_t)wrapped - (int64_t)((uint64_t)1 << bits);
            return (int64_t)wrapped;
        };
        switch (buf->btype) {
            case BUF_INT8:    *(int8_t *)p   = (int8_t)wrap_signed(val, 8);       break;
            case BUF_INT16:   *(int16_t *)p  = (int16_t)wrap_signed(val, 16);      break;
            case BUF_INT32:   *(int32_t *)p  = (int32_t)wrap_signed(val, 32);      break;
            case BUF_UINT8:   *(uint8_t *)p  = (uint8_t)wrap_unsigned(val, 256.0); break;
            case BUF_UINT16:  *(uint16_t *)p = (uint16_t)wrap_unsigned(val, 65536.0); break;
            case BUF_UINT32:  *(uint32_t *)p = (uint32_t)wrap_unsigned(val, 4294967296.0); break;
            case BUF_FLOAT32: *(float *)p    = (float)val;    break;
            case BUF_FLOAT64: *(double *)p   = val;           break;
        }
    }

    double buffer_get(ObjBuffer *buf, int32_t index)
    {
        uint8_t *p = buf->data + (size_t)index * buffer_elem_size[buf->btype];
        switch (buf->btype) {
            case BUF_INT8:    return (double)*(int8_t *)p;
            case BUF_INT16:   return (double)*(int16_t *)p;
            case BUF_INT32:   return (double)*(int32_t *)p;
            case BUF_UINT8:   return (double)*(uint8_t *)p;
            case BUF_UINT16:  return (double)*(uint16_t *)p;
            case BUF_UINT32:  return (double)*(uint32_t *)p;
            case BUF_FLOAT32: return (double)*(float *)p;
            case BUF_FLOAT64: return *(double *)p;
        }
        return 0.0;
    }

    void buffer_fill(ObjBuffer *buf, double val)
    {
        for (int32_t i = 0; i < buf->count; i++)
            buffer_set(buf, i, val);
    }

    /* =========================================================
    ** GC — Tri-color Mark & Sweep
    ** ========================================================= */

    void gc_mark_obj(GC *gc, Obj *obj)
    {
        if (obj == nullptr)
            return;
        if (obj->color != gc->white_val)
            return; /* já visitado */

        obj->color = GC_GRAY;

        /* Adiciona ao gray list */
        if (gc->gray_count >= gc->gray_capacity)
        {
            gc->gray_capacity = gc->gray_capacity < 8 ? 8 : gc->gray_capacity * 2;
            gc->gray_list = (Obj **)realloc(gc->gray_list,
                                            sizeof(Obj *) * gc->gray_capacity);
        }
        gc->gray_list[gc->gray_count++] = obj;
    }

    void gc_mark_value(GC *gc, Value v)
    {
        if (is_obj(v))
            gc_mark_obj(gc, v.as.obj);
    }

    /* Blacken a gray object: mark its children */
    static void gc_blacken(GC *gc, Obj *obj)
    {
        obj->color = gc->black_val;

        switch (obj->type)
        {
        case OBJ_STRING:
        case OBJ_BUFFER:
        case OBJ_STRUCT_DEF:
            /* no child references (field names are interned strings) */
            break;

        case OBJ_FUNC:
        {
            ObjFunc *fn = (ObjFunc *)obj;
            gc_mark_obj(gc, (Obj *)fn->name);
            gc_mark_obj(gc, (Obj *)fn->source);
            for (int i = 0; i < fn->const_count; i++)
                gc_mark_value(gc, fn->constants[i]);
            break;
        }

        case OBJ_NATIVE:
            gc_mark_obj(gc, (Obj *)((ObjNative *)obj)->name);
            break;

        case OBJ_ARRAY:
        {
            ObjArray *arr = (ObjArray *)obj;
            int32_t n = arr_count(arr);
            for (int i = 0; i < n; i++)
                gc_mark_value(gc, arr->data[i]);
            break;
        }

        case OBJ_STRUCT:
        {
            ObjStruct *s = (ObjStruct *)obj;
            gc_mark_obj(gc, (Obj *)s->def);
            for (int32_t i = 0; i < s->def->num_fields; i++)
                gc_mark_value(gc, s->fields[i]);
            break;
        }
        }
    }

    /* Trace: processa todos os grays */
    static void gc_trace_refs(GC *gc)
    {
        while (gc->gray_count > 0)
        {
            Obj *obj = gc->gray_list[--gc->gray_count];
            gc_blacken(gc, obj);
        }
    }

    /* Calcula tamanho de um objecto para desalocar */
    static size_t obj_size(Obj *obj)
    {
        switch (obj->type)
        {
        case OBJ_STRING:
        {
            ObjString *s = (ObjString *)obj;
            int32_t cap = s->capacity > 0 ? s->capacity : s->length + 1;
            return sizeof(ObjString) + cap;
        }
        case OBJ_FUNC:
            return sizeof(ObjFunc);
        case OBJ_NATIVE:
            return sizeof(ObjNative);
        case OBJ_ARRAY:
            return sizeof(ObjArray);
        case OBJ_BUFFER:
            return sizeof(ObjBuffer);
        case OBJ_STRUCT_DEF:
            return sizeof(ObjStructDef);
        case OBJ_STRUCT:
            return sizeof(ObjStruct);
        }
        return 0;
    }

    /* Free an object and its internal buffers */
    static void free_obj(GC *gc, Obj *obj)
    {
        switch (obj->type)
        {
        case OBJ_STRING:
        case OBJ_NATIVE:
            break;
        case OBJ_FUNC:
        {
            ObjFunc *fn = (ObjFunc *)obj;
            if (fn->code)
                zen_free(gc, fn->code, sizeof(Instruction) * fn->code_count);
            if (fn->lines)
                zen_free(gc, fn->lines, sizeof(int32_t) * fn->code_count);
            if (fn->constants)
                zen_free(gc, fn->constants, sizeof(Value) * fn->const_count);
            break;
        }
        case OBJ_ARRAY:
        {
            ObjArray *arr = (ObjArray *)obj;
            if (arr->data)
                zen_free(gc, arr->data, sizeof(Value) * arr_capacity(arr));
            break;
        }
        case OBJ_BUFFER:
        {
            ObjBuffer *buf = (ObjBuffer *)obj;
            if (buf->data)
                zen_free(gc, buf->data, (size_t)buf->capacity * buffer_elem_size[buf->btype]);
            break;
        }
        case OBJ_STRUCT_DEF:
        {
            ObjStructDef *def = (ObjStructDef *)obj;
            if (def->field_names)
                zen_free(gc, def->field_names, sizeof(ObjString *) * def->num_fields);
            break;
        }
        case OBJ_STRUCT:
        {
            ObjStruct *s = (ObjStruct *)obj;
            if (s->fields)
                zen_free(gc, s->fields, sizeof(Value) * s->def->num_fields);
            break;
        }
        }
        zen_free(gc, obj, obj_size(obj));
    }

    /* Sweep: percorre TODOS os objectos, free os WHITE */
    static void gc_rebuild_intern_table(GC *gc)
    {
        ObjString **old_table = gc->strings;
        int old_capacity = gc->string_capacity;
        ObjString **new_table = (ObjString **)calloc((size_t)old_capacity, sizeof(ObjString *));
        int new_count = 0;

        for (int i = 0; i < old_capacity; i++)
        {
            ObjString *s = old_table[i];
            if (!s || s->obj.color == gc->white_val)
                continue;

            uint32_t idx = s->obj.hash & (uint32_t)(old_capacity - 1);
            while (new_table[idx] != nullptr)
                idx = (idx + 1) & (uint32_t)(old_capacity - 1);
            new_table[idx] = s;
            new_count++;
        }

        free(old_table);
        gc->strings = new_table;
        gc->string_count = new_count;
    }

    static void gc_sweep(GC *gc)
    {
        gc_rebuild_intern_table(gc);

        Obj **ptr = &gc->objects;
        while (*ptr)
        {
            if ((*ptr)->color == gc->white_val)
            {
                Obj *dead = *ptr;
                *ptr = dead->gc_next;
                /* Dead interned strings were already dropped from the intern
                   table by gc_rebuild_intern_table() above. */
                free_obj(gc, dead);
            }
            else
            {
                /* Survivor keeps its color — the white/black flip at the end
                   of gc_collect() makes it white for the next cycle. */
                ptr = &(*ptr)->gc_next;
            }
        }
    }

    /* GC constants */
    static const size_t kGCMinThreshold = 1024 * 64;  /* 64 KB */
    static const size_t kGCMaxThreshold = 1024 * 1024 * 256; /* 256 MB */

    /* gc_sweep_all — free ALL objects unconditionally (used in VM destructor) */
    void gc_sweep_all(GC *gc)
    {
        Obj *obj = gc->objects;
        while (obj)
        {
            Obj *next = obj->gc_next;
            if (obj->type == OBJ_STRING)
            {
                ObjString *s = (ObjString *)obj;
                uint32_t idx = s->obj.hash & (gc->string_capacity - 1);
                while (gc->strings[idx] != s)
                {
                    if (gc->strings[idx] == nullptr)
                        break;
                    idx = (idx + 1) & (gc->string_capacity - 1);
                }
                if (gc->strings[idx] == s)
                {
                    gc->strings[idx] = nullptr;
                    gc->string_count--;
                }
            }
            free_obj(gc, obj);
            obj = next;
        }
        gc->objects = nullptr;
    }

    /* gc_collect — chamado pelo VM quando bytes_allocated > next_gc */
    void gc_collect(VM *vm)
    {
        GC *gc = &vm->get_gc();

        /* Prevent re-entrant GC */
        void *saved_vm = gc->vm;
        gc->vm = nullptr;

#ifdef ZEN_DEBUG_GC
        size_t before = gc->bytes_allocated;
#endif

        /* Reset gray list */
        gc->gray_count = 0;

        /* Whiten ONLY the newborn prefix: every object allocated since the
        ** last collection sits before first_old in the list (allocation
        ** always prepends) and was born black. It must be whitened so the
        ** mark phase traces through it — a black object is never traced, and
        ** skipping one would leave its white children unmarked (and swept
        ** while still referenced). Survivors behind first_old are already
        ** white thanks to the epoch flip below — no walk needed for them. */
        for (Obj *o = gc->objects; o && o != gc->first_old; o = o->gc_next)
            o->color = gc->white_val;

        /* Mark roots */
        vm->gc_mark_roots();

        /* Trace references */
        gc_trace_refs(gc);

        /* Sweep dead objects */
        gc_sweep(gc);

        /* Epoch flip: survivors (black_val) and this cycle's newborns all
        ** read as white next cycle; the next cycle's allocations get the new
        ** black_val. Equivalent to repainting every object white, in O(1). */
        {
            GCColor tmp = gc->white_val;
            gc->white_val = gc->black_val;
            gc->black_val = tmp;
        }
        gc->first_old = gc->objects; /* everything left is now "old" */

        /* Adjust next threshold — ensure next_gc is always above bytes_allocated
        ** so GC never thrashes when the live set is large. */
        gc->next_gc = (size_t)(gc->bytes_allocated * kGCGrowFactor);
        if (gc->next_gc < kGCMinThreshold)
            gc->next_gc = kGCMinThreshold;
        while (gc->next_gc <= gc->bytes_allocated)
            gc->next_gc *= 2;

#ifdef ZEN_DEBUG_GC
        fprintf(stderr, "[GC] collected %zu bytes (%zu -> %zu), next at %zu\n",
                before - gc->bytes_allocated, before, gc->bytes_allocated, gc->next_gc);
#endif

        /* Re-enable GC trigger */
        gc->vm = saved_vm;
    }

} /* namespace zen */
