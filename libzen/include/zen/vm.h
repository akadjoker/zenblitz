#ifndef ZEN_VM_H
#define ZEN_VM_H

#include "memory.h"
#include "opcodes.h"

namespace zen
{
    /* =========================================================
    ** ZenIO — platform-abstracted file I/O (override for SDL/WASM).
    ** ========================================================= */
    typedef void *ZenFile;

    struct ZenIO
    {
        ZenFile (*open)(const char *path, const char *mode, void *userdata);
        long (*read)(ZenFile file, void *buf, long size, void *userdata);
        long (*write)(ZenFile file, const void *buf, long size, void *userdata);
        long (*seek)(ZenFile file, long offset, int whence, void *userdata);
        int (*close)(ZenFile file, void *userdata);
        int (*exists)(const char *path, void *userdata);
    };

    struct ZenCallbacks
    {
        ZenIO io;
        void (*print)(const char *str, int len, void *userdata);
        void (*print_err)(const char *str, int len, void *userdata);
        void *userdata;
    };

    ZenCallbacks zen_default_callbacks();

    /*
    ** VM — interpreter state: globals, GC, the main execution context.
    */
    class VM
    {
    public:
        VM();
        ~VM();

        VM(const VM &) = delete;
        VM &operator=(const VM &) = delete;

        /* --- Execution --- */
        void run(ObjFunc *func);
        /* Continue a program stopped by request_suspend(); returns false when
           the program has finished (or failed). */
        bool resume();
        bool suspended() const { return main_fiber_->state == FIBER_SUSPENDED; }
        /* Ask the interpreter to return to the host right after the native
           currently running (Flip, Delay, ...). */
        void request_suspend() { suspend_requested_ = true; }
        Value call_global(int idx, Value *args, int nargs);
        Value call_global(const char *name, Value *args, int nargs);

        /* --- Globals (by index — O(1), used at runtime) --- */
        int def_global(const char *name, Value val); /* returns index */
        Value get_global(int idx) const { return globals_[idx]; }
        void set_global(int idx, Value val) { globals_[idx] = val; }

        /* --- Globals (by name — O(n), compile/embed time) --- */
        int find_global(const char *name) const;
        Value get_global(const char *name) const;
        void set_global(const char *name, Value val);
        int num_globals() const { return num_globals_; }
        const char *global_name(int idx) const { return global_names_[idx] ? global_names_[idx]->chars : nullptr; }

        /* --- Natives --- */
        int def_native(const char *name, NativeFn fn, int arity);

        /* --- Struct builder (Blitz Types) --- */
        struct StructBuilder
        {
            StructBuilder(VM *vm, const char *name);
            StructBuilder &field(const char *name);
            ObjStructDef *end();
        private:
            VM *vm_;
            ObjStructDef *def_;
        };
        StructBuilder def_struct(const char *name);

        /* --- Callbacks (platform I/O hooks) --- */
        void set_callbacks(const ZenCallbacks &cb) { callbacks_ = cb; }
        const ZenCallbacks &get_callbacks() const { return callbacks_; }

        /* --- Strings (interned) --- */
        ObjString *make_string(const char *str, int length = -1);

        /* --- GC --- */
        GC &get_gc() { return gc_; }
        Fiber *main_fiber() const { return main_fiber_; }
        void collect();
        void gc_mark_roots();

        /* --- Errors --- */
        void runtime_error(const char *fmt, ...);
        bool had_error() const { return had_error_; }
        void clear_error() { had_error_ = false; }
        const char *error_message() const { return error_msg_; }

    private:
/*
** Dispatch mode:
**   -DZEN_COMPUTED_GOTO → computed goto (GCC/Clang)
**   default             → switch (portable, MSVC)
*/
#ifndef ZEN_DISPATCH_MODE
#if defined(__GNUC__) || defined(__clang__)
#define ZEN_COMPUTED_GOTO
#endif
#endif
        void execute(Fiber *fiber); /* vm_dispatch.cpp */

        Fiber *new_fiber(int stack_size, int max_frames);
        void free_fiber(Fiber *fiber);

        GC gc_;

        Value *globals_;
        ObjString **global_names_;
        int num_globals_;
        int globals_capacity_;
        bool grow_globals(int required);

        Fiber *main_fiber_;
        bool had_error_;
        bool suspend_requested_;
        char error_msg_[512];

        ZenCallbacks callbacks_;
    };

} /* namespace zen */

#endif /* ZEN_VM_H */
