/*
** vm.cpp — VM state: globals, natives, struct definitions, GC roots,
** execution entry points and error reporting. The dispatch loop lives in
** vm_dispatch.cpp.
*/
#include "vm.h"
#include "debug.h"
#include "zenconf.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace zen
{
    /* =========================================================
    ** Null backend — text through zenconf.h, no files, no clock
    ** ========================================================= */
    static void null_print(const char *str, int len, void *) { zen_write(str, (size_t)len); }
    static void null_log(int, const char *msg, void *)
    {
        zen_writeerr(msg, strlen(msg));
        zen_writeerr("\n", 1);
    }

    Backend null_backend()
    {
        Backend b;
        memset(&b, 0, sizeof(b));
        b.print = null_print;
        b.log = null_log;
        return b;
    }

    /* =========================================================
    ** Constructor / Destructor
    ** ========================================================= */
    VM::VM()
        : globals_(nullptr), global_names_(nullptr), num_globals_(0), globals_capacity_(0),
          main_fiber_(nullptr), had_error_(false), suspend_requested_(false), wake_at_ms_(0)
    {
        gc_init(&gc_);
        gc_.vm = this;
        backend_ = null_backend();
        error_msg_[0] = '\0';

        globals_capacity_ = kInitGlobalCapacity;
        globals_ = (Value *)calloc(globals_capacity_, sizeof(Value));
        global_names_ = (ObjString **)calloc(globals_capacity_, sizeof(ObjString *));

        main_fiber_ = new_fiber(kMainStackSlots, kMaxFrames);
    }

    VM::~VM()
    {
        free_fiber(main_fiber_);
        main_fiber_ = nullptr;

        gc_sweep_all(&gc_);
        free(globals_);
        free(global_names_);
        globals_ = nullptr;
        global_names_ = nullptr;
        globals_capacity_ = 0;
        num_globals_ = 0;
        if (gc_.gray_list) free(gc_.gray_list);
        if (gc_.strings) free(gc_.strings);
        gc_.strings = nullptr;
        arena_destroy(&gc_.arena);
    }

    /* =========================================================
    ** Fibers (execution contexts) — owned by the VM, not by the GC
    ** ========================================================= */
    Fiber *VM::new_fiber(int stack_size, int max_frames)
    {
        Fiber *fiber = (Fiber *)calloc(1, sizeof(Fiber));
        fiber->state = FIBER_READY;
        fiber->stack_capacity = stack_size;
        fiber->stack = (Value *)calloc((size_t)stack_size, sizeof(Value));
        fiber->stack_top = fiber->stack;
        fiber->frame_capacity = max_frames;
        fiber->frames = (CallFrame *)calloc((size_t)max_frames, sizeof(CallFrame));
        fiber->frame_count = 0;
        return fiber;
    }

    void VM::free_fiber(Fiber *fiber)
    {
        if (!fiber) return;
        free(fiber->stack);
        free(fiber->frames);
        free(fiber);
    }

    /* =========================================================
    ** Run
    ** ========================================================= */
    void VM::run(ObjFunc *func)
    {
        Fiber *fiber = main_fiber_;
        fiber->frame_count = 1;
        CallFrame *frame = &fiber->frames[0];
        frame->func = func;
        frame->ip = func->code;
        frame->base = fiber->stack;
        frame->ret_reg = 0;
        frame->ret_count = 0;
        fiber->stack_top = fiber->stack + func->num_regs;
        fiber->state = FIBER_RUNNING;
        suspend_requested_ = false;

        execute(fiber);

        if (fiber->state != FIBER_SUSPENDED)
        {
            fiber->stack_top = fiber->stack;
            fiber->frame_count = 0;
        }
    }

    bool VM::resume()
    {
        Fiber *fiber = main_fiber_;
        if (fiber->state != FIBER_SUSPENDED) return false;
        fiber->state = FIBER_RUNNING;
        suspend_requested_ = false;
        execute(fiber);
        if (fiber->state == FIBER_SUSPENDED) return true;
        fiber->stack_top = fiber->stack;
        fiber->frame_count = 0;
        return false;
    }

    Value VM::call_global(int idx, Value *args, int nargs)
    {
        Value callee = globals_[idx];
        if (is_native(callee))
        {
            ObjNative *nat = as_native(callee);
            int nret = nat->fn(this, args, nargs);
            return (nret > 0) ? args[0] : val_nil();
        }
        if (is_func(callee))
        {
            ObjFunc *fn = as_func(callee);
            Fiber *fiber = main_fiber_;
            /* place the frame above whatever is live on the main stack */
            Value *base = fiber->frame_count > 0 ? fiber->stack_top : fiber->stack;
            if (fiber->frame_count >= fiber->frame_capacity ||
                base + fn->num_regs > fiber->stack + fiber->stack_capacity)
            {
                runtime_error("stack overflow");
                return val_nil();
            }
            for (int i = 0; i < nargs; i++)
                base[i] = args[i];
            CallFrame *frame = &fiber->frames[fiber->frame_count++];
            frame->func = fn;
            frame->ip = fn->code;
            frame->base = base;
            frame->ret_reg = 0;
            frame->ret_count = 1;
            Value *saved_top = fiber->stack_top;
            int saved_frames = fiber->frame_count - 1;
            fiber->stack_top = base + fn->num_regs;
            fiber->state = FIBER_RUNNING;
            execute(fiber);
            Value result = base[0];
            fiber->frame_count = saved_frames;
            fiber->stack_top = saved_top;
            return result;
        }
        runtime_error("global %d is not callable", idx);
        return val_nil();
    }

    Value VM::call_global(const char *name, Value *args, int nargs)
    {
        int idx = find_global(name);
        if (idx < 0)
        {
            runtime_error("undefined global '%s'", name);
            return val_nil();
        }
        return call_global(idx, args, nargs);
    }

    /* =========================================================
    ** Globals
    ** ========================================================= */
    int VM::find_global(const char *name) const
    {
        for (int i = 0; i < num_globals_; i++)
            if (global_names_[i] && strcmp(global_names_[i]->chars, name) == 0)
                return i;
        return -1;
    }

    int VM::def_global(const char *name, Value val)
    {
        int existing = find_global(name);
        if (existing >= 0)
        {
            globals_[existing] = val;
            return existing;
        }
        if (!grow_globals(num_globals_ + 1))
            return -1;
        int idx = num_globals_++;
        global_names_[idx] = intern_string(&gc_, name, (int)strlen(name),
                                           hash_string(name, (int)strlen(name)));
        globals_[idx] = val;
        return idx;
    }

    bool VM::grow_globals(int required)
    {
        if (required <= globals_capacity_)
            return true;
        if (required > kMaxGlobalsHard)
        {
            runtime_error("too many globals");
            return false;
        }
        int new_cap = globals_capacity_ > 0 ? globals_capacity_ : kInitGlobalCapacity;
        while (new_cap < required && new_cap < kMaxGlobalsHard)
            new_cap *= 2;
        if (new_cap < required)
            new_cap = kMaxGlobalsHard;
        Value *new_globals = (Value *)realloc(globals_, sizeof(Value) * (size_t)new_cap);
        if (!new_globals)
        {
            runtime_error("out of memory growing globals");
            return false;
        }
        ObjString **new_names = (ObjString **)realloc(global_names_, sizeof(ObjString *) * (size_t)new_cap);
        if (!new_names)
        {
            globals_ = new_globals;
            runtime_error("out of memory growing globals");
            return false;
        }
        for (int i = globals_capacity_; i < new_cap; i++)
        {
            new_globals[i] = val_nil();
            new_names[i] = nullptr;
        }
        globals_ = new_globals;
        global_names_ = new_names;
        globals_capacity_ = new_cap;
        return true;
    }

    Value VM::get_global(const char *name) const
    {
        int idx = find_global(name);
        return idx >= 0 ? globals_[idx] : val_nil();
    }

    void VM::set_global(const char *name, Value val)
    {
        int idx = find_global(name);
        if (idx >= 0) globals_[idx] = val;
        else def_global(name, val);
    }

    /* =========================================================
    ** Natives
    ** ========================================================= */
    int VM::def_native(const char *name, NativeFn fn, int arity)
    {
        ObjString *s = intern_string(&gc_, name, (int)strlen(name),
                                     hash_string(name, (int)strlen(name)));
        ObjNative *nat = new_native(&gc_, fn, arity, s);
        return def_global(name, val_obj((Obj *)nat));
    }

    /* =========================================================
    ** Struct builder
    ** ========================================================= */
    VM::StructBuilder VM::def_struct(const char *name)
    {
        return StructBuilder(this, name);
    }

    VM::StructBuilder::StructBuilder(VM *vm, const char *name) : vm_(vm)
    {
        ObjString *s = intern_string(&vm->gc_, name, (int)strlen(name),
                                     hash_string(name, (int)strlen(name)));
        def_ = new_struct_def(&vm->gc_, s);
    }

    VM::StructBuilder &VM::StructBuilder::field(const char *name)
    {
        int idx = def_->num_fields++;
        def_->field_names = (ObjString **)zen_realloc(
            &vm_->gc_, def_->field_names,
            sizeof(ObjString *) * (idx),
            sizeof(ObjString *) * (idx + 1));
        def_->field_names[idx] = intern_string(&vm_->gc_, name, (int)strlen(name),
                                               hash_string(name, (int)strlen(name)));
        return *this;
    }

    ObjStructDef *VM::StructBuilder::end()
    {
        vm_->def_global(def_->name->chars, val_obj((Obj *)def_));
        return def_;
    }

    /* =========================================================
    ** Strings / GC
    ** ========================================================= */
    ObjString *VM::make_string(const char *str, int length)
    {
        if (length < 0)
            length = (int)strlen(str);
        return new_string(&gc_, str, length);
    }

    void VM::collect()
    {
        gc_collect(this);
    }

    void VM::gc_mark_roots()
    {
        GC *gc = &gc_;
        for (int i = 0; i < num_globals_; i++)
        {
            gc_mark_value(gc, globals_[i]);
            if (global_names_[i])
                gc_mark_obj(gc, (Obj *)global_names_[i]);
        }
        Fiber *fiber = main_fiber_;
        if (fiber)
        {
            for (Value *v = fiber->stack; v < fiber->stack_top; v++)
                gc_mark_value(gc, *v);
            for (int f = 0; f < fiber->frame_count; f++)
                if (fiber->frames[f].func)
                    gc_mark_obj(gc, (Obj *)fiber->frames[f].func);
        }
    }

    /* =========================================================
    ** Errors
    ** ========================================================= */
    void VM::runtime_error(const char *fmt, ...)
    {
        had_error_ = true;
        char msg[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(msg, sizeof(msg), fmt, args);
        va_end(args);
        snprintf(error_msg_, sizeof(error_msg_), "%s", msg);

        if (main_fiber_)
            main_fiber_->state = FIBER_ERROR;

        char line[560];
        snprintf(line, sizeof(line), "[runtime error] %s", msg);
        backend_log(backend_, LOG_ERROR, line);

        if (main_fiber_)
        {
            for (int i = main_fiber_->frame_count - 1; i >= 0; i--)
            {
                CallFrame *frame = &main_fiber_->frames[i];
                ObjFunc *func = frame->func;
                if (!func) continue;
                int offset = (int)(frame->ip - func->code - 1);
                int line = (offset >= 0 && offset < func->code_count) ? func->lines[offset] : 0;
                const char *fname = func->name ? func->name->chars : "<script>";
                const char *src = func->source ? func->source->chars : "?";
                char traceline[512];
                snprintf(traceline, sizeof(traceline), "  File \"%s\", line %d, in %s", src, line, fname);
                backend_log(backend_, LOG_ERROR, traceline);
            }
        }
    }

} /* namespace zen */
