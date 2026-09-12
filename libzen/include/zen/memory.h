#ifndef ZEN_MEMORY_H
#define ZEN_MEMORY_H

#include "object.h"
#include "zen_arena.h"

namespace zen
{
    /*
    ** GC — tri-color mark & sweep with epoch flip (see memory.cpp).
    */
    struct GC
    {
        Obj *objects;    /* linked list of ALL objects */
        Obj *gray_stack;
        int gray_count;
        int gray_capacity;
        Obj **gray_list;

        size_t bytes_allocated;
        size_t next_gc;
        size_t pause_saved_next_gc;
        int pause_depth;

        GCColor white_val;
        GCColor black_val;
        Obj *first_old;

        /* String interning table (open addressing) */
        ObjString **strings;
        int string_count;
        int string_capacity;

        void *vm; /* back-pointer (GC trigger from zen_alloc) */

        Arena arena;
    };

    void *zen_alloc(GC *gc, size_t size);
    void *zen_alloc_now(GC *gc, size_t size);
    void *zen_realloc(GC *gc, void *ptr, size_t old_size, size_t new_size);
    void zen_free(GC *gc, void *ptr, size_t size);

    /* Object creation (allocates + registers with the GC) */
    ObjString *new_string(GC *gc, const char *chars, int length);
    ObjString *new_string_uninit(GC *gc, int length);
    ObjString *create_string(GC *gc, const char *chars, int length); /* non-interned */
    ObjString *intern_string(GC *gc, const char *chars, int length, uint32_t hash);
    ObjString *new_string_concat(GC *gc, ObjString *a, ObjString *b);
    ObjString *string_append_inplace(GC *gc, ObjString *a, ObjString *b);
    ObjFunc *new_func(GC *gc);
    ObjNative *new_native(GC *gc, NativeFn fn, int arity, ObjString *name);
    ObjArray *new_array(GC *gc);
    ObjStructDef *new_struct_def(GC *gc, ObjString *name);
    ObjStruct *new_struct(GC *gc, ObjStructDef *def); /* fields start as nil */

    /* Array operations */
    void array_push_slow(GC *gc, ObjArray *arr, Value val);
    void array_set(GC *gc, ObjArray *arr, int32_t index, Value val);
    Value array_pop(ObjArray *arr);
    void array_insert(GC *gc, ObjArray *arr, int32_t index, Value val);
    void array_remove(ObjArray *arr, int32_t index);
    void array_clear(ObjArray *arr);
    void array_reserve(GC *gc, ObjArray *arr, int32_t cap);
    int32_t array_find(ObjArray *arr, Value val);
    int32_t array_find_int(ObjArray *arr, int32_t target);
    bool array_contains(ObjArray *arr, Value val);
    void array_reverse(ObjArray *arr);
    void array_sort_int(ObjArray *arr);
    void array_copy(GC *gc, ObjArray *dst, ObjArray *src);
    void array_push_n(GC *gc, ObjArray *arr, const Value *vals, int32_t n);
    void array_append(GC *gc, ObjArray *dst, ObjArray *src);

    /* Buffer operations */
    ObjBuffer *new_buffer(GC *gc, BufferType btype, int32_t count);
    void buffer_set(ObjBuffer *buf, int32_t index, double val);
    double buffer_get(ObjBuffer *buf, int32_t index);
    void buffer_fill(ObjBuffer *buf, double val);

    /* GC control */
    void gc_init(GC *gc);
    void gc_collect(VM *vm);
    void gc_pause(GC *gc);
    void gc_resume(GC *gc);
    void gc_sweep_all(GC *gc);
    void gc_mark_value(GC *gc, Value v);
    void gc_mark_obj(GC *gc, Obj *obj);

    /* Write barrier — call when a BLACK object receives a new reference. */
    inline void gc_write_barrier(GC *gc, Obj *parent, Obj *child)
    {
        if (parent->color == gc->black_val && child && child->color == gc->white_val)
        {
            parent->color = GC_GRAY;
            if (gc->gray_count >= gc->gray_capacity)
            {
                gc->gray_capacity = gc->gray_capacity < 8 ? 8 : gc->gray_capacity * 2;
                gc->gray_list = (Obj **)realloc(gc->gray_list,
                                                sizeof(Obj *) * gc->gray_capacity);
            }
            gc->gray_list[gc->gray_count++] = parent;
        }
    }

    inline void array_push(GC *gc, ObjArray *arr, Value val)
    {
        if (__builtin_expect(arr->end != arr->cap_end, 1))
        {
            *arr->end++ = val;
            if (__builtin_expect(val.type == VAL_OBJ, 0))
                gc_write_barrier(gc, (Obj *)arr, val.as.obj);
        }
        else
        {
            array_push_slow(gc, arr, val);
        }
    }

    inline void array_push_int(GC *gc, ObjArray *arr, int32_t n)
    {
        Value v;
        v.type = VAL_INT;
        v.as.integer = n;
        if (__builtin_expect(arr->end != arr->cap_end, 1))
            *arr->end++ = v;
        else
            array_push_slow(gc, arr, v);
    }

    inline Value array_get(ObjArray *arr, int32_t index)
    {
        if (__builtin_expect((uint32_t)index < (uint32_t)arr_count(arr), 1))
            return arr->data[index];
        return val_nil();
    }

    inline Value array_pop_unsafe(ObjArray *arr)
    {
        return *--arr->end;
    }

    ObjString *find_interned(GC *gc, const char *chars, int length, uint32_t hash);

    inline ObjString *intern_string(GC *gc, const char *chars, int length)
    {
        return intern_string(gc, chars, length, hash_string(chars, length));
    }

    inline const uint8_t *get_ws_table(void)
    {
        static uint8_t ws[256] = {0};
        static int init = 0;
        if (!init)
        {
            ws[(uint8_t)' '] = 1;
            ws[(uint8_t)'\t'] = 1;
            ws[(uint8_t)'\n'] = 1;
            ws[(uint8_t)'\r'] = 1;
            init = 1;
        }
        return ws;
    }

} /* namespace zen */

#endif /* ZEN_MEMORY_H */
