/*
** bb_runtime.cpp — runtime support natives and command registration.
*/
#include "bb_runtime.h"
#include "object.h"
#include <cctype>
#include <unordered_map>

using namespace zen;

namespace bb
{
    static BBRuntime *g_rt = 0;

    BBRuntime *bb_runtime_get() { return g_rt; }

    BBRuntime *bb_runtime_for(VM *vm)
    {
        static map<VM *, BBRuntime *> per_vm;
        map<VM *, BBRuntime *>::iterator it = per_vm.find(vm);
        if (it != per_vm.end()) { g_rt = it->second; return it->second; }
        BBRuntime *rt = bb_runtime_create(vm);
        per_vm[vm] = rt;
        return rt;
    }

    /* ================= output ================= */
    void bb_print(VM *vm, const char *s, int n)
    {
        const ZenCallbacks &cb = vm->get_callbacks();
        cb.print(s, n, cb.userdata);
    }

    /* ================= helpers on instances ================= */
    static inline void set_field(VM *vm, ObjInstance *inst, int idx, Value v)
    {
        inst->fields[idx] = v;
        if (is_obj(v)) gc_write_barrier(&vm->get_gc(), (Obj *)inst, v.as.obj);
    }

    static inline bool obj_alive(Value v, BBTypeInfo *ti)
    {
        if (!is_instance(v)) return false;
        Value a = as_instance(v)->fields[ti->nfields + HF_ALIVE];
        return is_bool(a) && a.as.boolean;
    }

    static BBTypeInfo *type_of_value(Value v)
    {
        if (!is_instance(v)) return 0;
        map<ObjClass *, BBTypeInfo *>::iterator it = g_rt->typeByClass.find(as_instance(v)->klass);
        return it == g_rt->typeByClass.end() ? 0 : it->second;
    }

    static Value default_for_kind(VM *vm, int kind)
    {
        switch (kind)
        {
        case KIND_FLOAT: return val_float(0.0);
        case KIND_STR: return val_obj((Obj *)vm->make_string(""));
        case KIND_OBJ: return val_nil();
        default: return val_int(0);
        }
    }

    static ObjArray *make_filled_array(VM *vm, int kind, long long n)
    {
        GC *gc = &vm->get_gc();
        ObjArray *arr = new_array(gc);
        if (n < 0) n = 0;
        array_reserve(gc, arr, (int32_t)n);
        Value d = default_for_kind(vm, kind);
        for (long long i = 0; i < n; ++i) array_push(gc, arr, d);
        return arr;
    }

    /* ================= handles ================= */
    static std::unordered_map<long long, ObjInstance *> handle_map;
    static long long next_handle = 0;

    /* ================= natives: types/objects ================= */
    static int nat_new(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        int idx = (int)bb_arg_int(args[0]);
        BBTypeInfo *ti = g_rt->types[idx];
        GC *gc = &vm->get_gc();
        gc_pause(gc);
        ObjInstance *inst = new_instance(gc, ti->klass);
        Value v = val_obj((Obj *)inst);
        args[0] = v;
        for (int i = 0; i < ti->nfields; ++i)
        {
            if (ti->kinds[i] == KIND_VEC)
                set_field(vm, inst, i, val_obj((Obj *)make_filled_array(vm, ti->vecKinds[i], ti->vecSizes[i])));
            else
                set_field(vm, inst, i, default_for_kind(vm, ti->kinds[i]));
        }
        int nf = ti->nfields;
        Value last = vm->get_global(ti->gLast);
        set_field(vm, inst, nf + HF_PREV, last);
        set_field(vm, inst, nf + HF_NEXT, val_nil());
        set_field(vm, inst, nf + HF_ALIVE, val_bool(true));
        set_field(vm, inst, nf + HF_HANDLE, val_int(0));
        if (is_nil(last)) vm->set_global(ti->gFirst, v);
        else set_field(vm, as_instance(last), nf + HF_NEXT, v);
        vm->set_global(ti->gLast, v);
        gc_resume(gc);
        return 1;
    }

    static void unlink_obj(VM *vm, BBTypeInfo *ti, ObjInstance *inst)
    {
        int nf = ti->nfields;
        Value prev = inst->fields[nf + HF_PREV];
        Value next = inst->fields[nf + HF_NEXT];
        if (is_nil(prev)) vm->set_global(ti->gFirst, next);
        else set_field(vm, as_instance(prev), nf + HF_NEXT, next);
        if (is_nil(next)) vm->set_global(ti->gLast, prev);
        else set_field(vm, as_instance(next), nf + HF_PREV, prev);
    }

    static void kill_obj(VM *vm, BBTypeInfo *ti, ObjInstance *inst)
    {
        int nf = ti->nfields;
        inst->fields[nf + HF_ALIVE] = val_bool(false);
        Value h = inst->fields[nf + HF_HANDLE];
        if (is_int(h) && h.as.integer) handle_map.erase(h.as.integer);
        inst->fields[nf + HF_HANDLE] = val_int(0);
    }

    static int nat_delete(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        Value v = args[0];
        BBTypeInfo *ti = type_of_value(v);
        if (!ti || !obj_alive(v, ti)) return 0;
        ObjInstance *inst = as_instance(v);
        unlink_obj(vm, ti, inst);
        kill_obj(vm, ti, inst);
        return 0;
    }

    static int nat_delete_each(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        BBTypeInfo *ti = g_rt->types[(int)bb_arg_int(args[0])];
        Value cur = vm->get_global(ti->gFirst);
        while (is_instance(cur))
        {
            ObjInstance *inst = as_instance(cur);
            Value next = inst->fields[ti->nfields + HF_NEXT];
            kill_obj(vm, ti, inst);
            cur = next;
        }
        vm->set_global(ti->gFirst, val_nil());
        vm->set_global(ti->gLast, val_nil());
        return 0;
    }

    static int nat_after(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        Value v = args[0];
        BBTypeInfo *ti = type_of_value(v);
        if (!ti) { vm->runtime_error("Object does not exist"); return -1; }
        int nf = ti->nfields;
        Value n = as_instance(v)->fields[nf + HF_NEXT];
        while (is_instance(n) && !obj_alive(n, ti)) n = as_instance(n)->fields[nf + HF_NEXT];
        args[0] = is_instance(n) ? n : val_nil();
        return 1;
    }

    static int nat_before(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        Value v = args[0];
        BBTypeInfo *ti = type_of_value(v);
        if (!ti) { vm->runtime_error("Object does not exist"); return -1; }
        int nf = ti->nfields;
        Value n = as_instance(v)->fields[nf + HF_PREV];
        while (is_instance(n) && !obj_alive(n, ti)) n = as_instance(n)->fields[nf + HF_PREV];
        args[0] = is_instance(n) ? n : val_nil();
        return 1;
    }

    static int insert_common(VM *vm, Value *args, bool before)
    {
        Value a = args[0], b = args[1];
        BBTypeInfo *ti = type_of_value(a);
        BBTypeInfo *tb = type_of_value(b);
        if (!ti || !tb || !obj_alive(a, ti) || !obj_alive(b, tb)) { vm->runtime_error("Object does not exist"); return -1; }
        if (a.as.obj == b.as.obj) return 0;
        ObjInstance *ia = as_instance(a), *ib = as_instance(b);
        int nf = ti->nfields;
        unlink_obj(vm, ti, ia);
        if (before)
        {
            Value prev = ib->fields[nf + HF_PREV];
            set_field(vm, ia, nf + HF_PREV, prev);
            set_field(vm, ia, nf + HF_NEXT, b);
            if (is_nil(prev)) vm->set_global(ti->gFirst, a);
            else set_field(vm, as_instance(prev), nf + HF_NEXT, a);
            set_field(vm, ib, nf + HF_PREV, a);
        }
        else
        {
            Value next = ib->fields[nf + HF_NEXT];
            set_field(vm, ia, nf + HF_NEXT, next);
            set_field(vm, ia, nf + HF_PREV, b);
            if (is_nil(next)) vm->set_global(ti->gLast, a);
            else set_field(vm, as_instance(next), nf + HF_PREV, a);
            set_field(vm, ib, nf + HF_NEXT, a);
        }
        return 0;
    }

    static int nat_ins_before(VM *vm, Value *args, int nargs) { (void)nargs; return insert_common(vm, args, true); }
    static int nat_ins_after(VM *vm, Value *args, int nargs) { (void)nargs; return insert_common(vm, args, false); }

    static int nat_handle(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        Value v = args[0];
        BBTypeInfo *ti = type_of_value(v);
        if (!ti || !obj_alive(v, ti)) { args[0] = val_int(0); return 1; }
        ObjInstance *inst = as_instance(v);
        Value h = inst->fields[ti->nfields + HF_HANDLE];
        if (is_int(h) && h.as.integer) { args[0] = h; return 1; }
        ++next_handle;
        inst->fields[ti->nfields + HF_HANDLE] = val_int(next_handle);
        handle_map[next_handle] = inst;
        args[0] = val_int(next_handle);
        return 1;
    }

    static int nat_object(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        long long h = bb_arg_int(args[0]);
        BBTypeInfo *ti = g_rt->types[(int)bb_arg_int(args[1])];
        std::unordered_map<long long, ObjInstance *>::iterator it = handle_map.find(h);
        if (it == handle_map.end() || it->second->klass != ti->klass) { args[0] = val_nil(); return 1; }
        args[0] = val_obj((Obj *)it->second);
        return 1;
    }

    static void obj_to_str(Value v, string &out, int depth, Obj *root)
    {
        BBTypeInfo *ti = type_of_value(v);
        if (!ti || !obj_alive(v, ti)) { out += "[NULL]"; return; }
        if (v.as.obj == root && depth > 0) { out += "[ROOT]"; return; }
        if (depth >= 8) { out += "...."; return; }
        ObjInstance *inst = as_instance(v);
        out += "[";
        for (int k = 0; k < ti->nfields; ++k)
        {
            if (k) out += ",";
            Value f = inst->fields[k];
            switch (ti->kinds[k])
            {
            case KIND_INT: out += bb_itoa(bb_arg_int(f)); break;
            case KIND_FLOAT: out += bb_ftoa(bb_arg_float(f)); break;
            case KIND_STR: out += "\"" + bb_arg_str(f) + "\""; break;
            case KIND_OBJ: obj_to_str(f, out, depth + 1, root ? root : v.as.obj); break;
            default: out += "???";
            }
        }
        out += "]";
    }

    static int nat_obj_to_str(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        string s;
        obj_to_str(args[0], s, 0, 0);
        args[0] = bb_ret_str(vm, s);
        return 1;
    }

    /* ================= natives: conversions ================= */
    static int nat_ftoi(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        args[0] = val_int(bb_arg_int(args[0]));
        return 1;
    }

    static int nat_stoi(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        args[0] = val_int(bb_atoi(bb_arg_cstr(args[0])));
        return 1;
    }

    static int nat_stof(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        args[0] = val_float(bb_atof(bb_arg_cstr(args[0])));
        return 1;
    }

    static int nat_ftostr(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        args[0] = bb_ret_str(vm, bb_ftoa(bb_arg_float(args[0])));
        return 1;
    }

    static int nat_sgn(VM *vm, Value *args, int nargs)
    {
        (void)vm; (void)nargs;
        Value v = args[0];
        if (is_float(v)) args[0] = val_float(v.as.number > 0 ? 1.0 : (v.as.number < 0 ? -1.0 : 0.0));
        else { long long n = bb_arg_int(v); args[0] = val_int(n > 0 ? 1 : (n < 0 ? -1 : 0)); }
        return 1;
    }

    /* ================= natives: arrays ================= */
    static int nat_dim(VM *vm, Value *args, int nargs)
    {
        int kind = (int)bb_arg_int(args[0]);
        long long total = 1;
        for (int k = 1; k < nargs; ++k)
        {
            long long s = bb_arg_int(args[k]);
            if (s <= 0) s = 1;
            total *= s;
        }
        if (total > 100000000LL) { vm->runtime_error("Array too large"); return -1; }
        args[0] = val_obj((Obj *)make_filled_array(vm, kind, total));
        return 1;
    }

    static int nat_vec(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        args[0] = val_obj((Obj *)make_filled_array(vm, (int)bb_arg_int(args[0]), bb_arg_int(args[1])));
        return 1;
    }

    /* ================= natives: data ================= */
    static bool read_data(VM *vm, Value &out)
    {
        Value arr = vm->get_global(g_rt->g_data);
        long long p = bb_arg_int(vm->get_global(g_rt->g_dataptr));
        if (!is_array(arr) || p < 0 || p >= arr_count(as_array(arr)))
        {
            vm->runtime_error("Out of data");
            return false;
        }
        out = as_array(arr)->data[p];
        vm->set_global(g_rt->g_dataptr, val_int(p + 1));
        return true;
    }

    static int nat_read_int(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        Value v;
        if (!read_data(vm, v)) return -1;
        if (is_string(v)) args[0] = val_int(bb_atoi(as_cstring(v)));
        else args[0] = val_int(bb_arg_int(v));
        return 1;
    }

    static int nat_read_float(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        Value v;
        if (!read_data(vm, v)) return -1;
        if (is_string(v)) args[0] = val_float(bb_atof(as_cstring(v)));
        else args[0] = val_float(bb_arg_float(v));
        return 1;
    }

    static int nat_read_str(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        Value v;
        if (!read_data(vm, v)) return -1;
        if (is_string(v)) args[0] = v;
        else if (is_float(v)) args[0] = bb_ret_str(vm, bb_ftoa(v.as.number));
        else args[0] = bb_ret_str(vm, bb_itoa(bb_arg_int(v)));
        return 1;
    }

    /* ================= natives: gosub / errors ================= */
    static int nat_gosub_pop(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        Value arr = args[0];
        if (!is_array(arr) || arr_count(as_array(arr)) == 0)
        {
            vm->runtime_error("Return without Gosub");
            return -1;
        }
        args[0] = array_pop(as_array(arr));
        return 1;
    }

    static int nat_rterror(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        vm->runtime_error("%s", bb_arg_cstr(args[0]));
        return -1;
    }

    /* ================= type registry ================= */
    int BBRuntime::registerType(const string &name, const vector<int> &kinds,
                                const vector<int> &vecSizes, const vector<int> &vecKinds)
    {
        BBTypeInfo *ti = new BBTypeInfo();
        ti->name = name;
        ti->nfields = (int)kinds.size();
        ti->kinds = kinds;
        ti->vecSizes = vecSizes;
        ti->vecKinds = vecKinds;
        VM::ClassBuilder b = vm->def_class(("_t" + name).c_str());
        for (int i = 0; i < ti->nfields; ++i) b.field(("f" + bb_itoa(i)).c_str());
        b.field("__prev").field("__next").field("__alive").field("__handle");
        ti->klass = b.end();
        ti->gFirst = vm->def_global(("_t" + name + "_first").c_str(), val_nil());
        ti->gLast = vm->def_global(("_t" + name + "_last").c_str(), val_nil());
        types.push_back(ti);
        typeByClass[ti->klass] = ti;
        return (int)types.size() - 1;
    }

    /* ================= command registration ================= */
    static Type *typeof_tag(int c)
    {
        switch (c)
        {
        case '%': return Type::int_type;
        case '#': return Type::float_type;
        case '$': return Type::string_type;
        }
        return Type::void_type;
    }

    void BBRuntime::registerCommands(const BBCommand *cmds, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            string s = cmds[i].sig;
            size_t start = 0, k;
            Type *t = Type::void_type;
            if (!isalpha((unsigned char)s[0])) { start = 1; t = typeof_tag(s[0]); }
            for (k = start + 1; k < s.size(); ++k)
                if (!isalnum((unsigned char)s[k]) && s[k] != '_') break;
            string name = s.substr(start, k - start);
            DeclSeq *params = new DeclSeq();
            while (k < s.size())
            {
                Type *pt = typeof_tag(s[k++]);
                size_t from = k;
                for (; k < s.size() && (isalnum((unsigned char)s[k]) || s[k] == '_'); ++k) {}
                string pname = s.substr(from, k - from);
                ConstType *defType = 0;
                if (k < s.size() && s[k] == '=')
                {
                    size_t f2 = ++k;
                    if (s[k] == '\"')
                    {
                        for (++k; s[k] != '\"'; ++k) {}
                        string v = s.substr(f2 + 1, k - f2 - 1);
                        defType = new ConstType(v);
                        ++k;
                    }
                    else
                    {
                        if (s[k] == '-') ++k;
                        for (; k < s.size() && (isdigit((unsigned char)s[k]) || s[k] == '.'); ++k) {}
                        string v = s.substr(f2, k - f2);
                        if (pt == Type::int_type) defType = new ConstType(bb_atoi(v));
                        else if (pt == Type::float_type) defType = new ConstType(bb_atof(v));
                        else defType = new ConstType(v);
                    }
                    env->types.push_back(defType);
                }
                params->insertDecl(pname, pt, DECL_PARAM, defType);
            }
            FuncType *ft = new FuncType(t, params, false, false);
            env->types.push_back(ft);
            string lname = bb_tolower(name);
            Decl *d = env->funcDecls->insertDecl(lname, ft, DECL_FUNC);
            if (!d)
            {
                fprintf(stderr, "bb: duplicate command '%s'\n", name.c_str());
                continue;
            }
            d->offset = vm->def_native(("_f" + lname).c_str(), cmds[i].fn, -1);
            if (lname == "end") g_end = d->offset;
        }
    }

    BBRuntime *bb_runtime_create(VM *vm)
    {
        BBRuntime *rt = new BBRuntime();
        g_rt = rt;
        rt->vm = vm;
        rt->env = new Environ("", Type::void_type, -1, 0);
        rt->g_end = -1;

        rt->g_ftoi = vm->def_native("__bbFtoI", nat_ftoi, 1);
        rt->g_stoi = vm->def_native("__bbStoI", nat_stoi, 1);
        rt->g_stof = vm->def_native("__bbStoF", nat_stof, 1);
        rt->g_ftostr = vm->def_native("__bbFtoStr", nat_ftostr, 1);
        rt->g_objtostr = vm->def_native("__bbObjToStr", nat_obj_to_str, 1);
        rt->g_sgn = vm->def_native("__bbSgn", nat_sgn, 1);
        rt->g_new = vm->def_native("__bbNew", nat_new, 1);
        rt->g_delete = vm->def_native("__bbDelete", nat_delete, 1);
        rt->g_deleteEach = vm->def_native("__bbDeleteEach", nat_delete_each, 1);
        rt->g_insBefore = vm->def_native("__bbInsBefore", nat_ins_before, 2);
        rt->g_insAfter = vm->def_native("__bbInsAfter", nat_ins_after, 2);
        rt->g_after = vm->def_native("__bbAfter", nat_after, 1);
        rt->g_before = vm->def_native("__bbBefore", nat_before, 1);
        rt->g_handle = vm->def_native("__bbHandle", nat_handle, 1);
        rt->g_object = vm->def_native("__bbObject", nat_object, 2);
        rt->g_dim = vm->def_native("__bbDim", nat_dim, -1);
        rt->g_vec = vm->def_native("__bbVec", nat_vec, 2);
        rt->g_readInt = vm->def_native("__bbReadInt", nat_read_int, 0);
        rt->g_readFloat = vm->def_native("__bbReadFloat", nat_read_float, 0);
        rt->g_readStr = vm->def_native("__bbReadStr", nat_read_str, 0);
        rt->g_gosubPop = vm->def_native("__bbGosubPop", nat_gosub_pop, 1);
        rt->g_rterror = vm->def_native("__bbRuntimeError", nat_rterror, 1);
        rt->g_data = vm->def_global("__DATA", val_nil());
        rt->g_dataptr = vm->def_global("__DATAPTR", val_int(0));

        rt->registerCommands(bb_cmds_basic, bb_cmds_basic_count);
        return rt;
    }

    void bb_runtime_destroy(BBRuntime *rt)
    {
        if (!rt) return;
        for (size_t k = 0; k < rt->types.size(); ++k) delete rt->types[k];
        delete rt->env;
        if (g_rt == rt) g_rt = 0;
        delete rt;
    }
}
