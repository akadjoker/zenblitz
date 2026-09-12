#ifndef ZEN_VM_H
#define ZEN_VM_H

#include "memory.h"
#include "opcodes.h"
#include "backend.h"

namespace zen
{
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
           currently running (Flip, Delay, ...). wake_at_ms is a deadline on
           the backend's millisecs() clock (0 = no deadline, e.g. Flip
           yielding one frame); the host reads it with wake_at() to know when
           to call resume() again — a console loop can sleep until then, a
           browser/Android loop schedules a timer instead of blocking. */
        void request_suspend(int64_t wake_at_ms = 0)
        {
            suspend_requested_ = true;
            wake_at_ms_ = wake_at_ms;
        }
        int64_t wake_at() const { return wake_at_ms_; }
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
        /* platform services (backend.h); set before install_runtime() */
        void set_backend(const Backend &b) { backend_ = b; }
        const Backend &backend() const { return backend_; }

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
        int64_t wake_at_ms_;
        char error_msg_[512];

        Backend backend_;
    };

} /* namespace zen */

#endif /* ZEN_VM_H */
