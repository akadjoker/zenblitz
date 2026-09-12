/*
** test_class_api — Teste completo da API de classes C++ ↔ Script
**
** Testa:
** 1. Criar classe em C++ com fields e methods nativos
** 2. Criar instância a partir de C++
** 3. Chamar método nativo via vtable slot
** 4. Script herda classe C++ e faz override
** 5. C++ chama override do script via vtable (polimorfismo)
** 6. Verificar que o slot correcto é chamado para cada tipo
*/

#include "vm.h"
#include "compiler.h"
#include <cstdio>
#include <cstring>
#include <cassert>

using namespace zen;

/* =========================================================
** Native methods for Entity class
** ========================================================= */

/* Entity.init(x, y, hp) — native constructor */
static int nat_entity_init(VM *vm, Value *args, int nargs)
{
    (void)vm;
    ObjInstance *self = as_instance(args[0]);
    /* fields: [x=0, y=1, hp=2] */
    self->fields[0] = nargs > 1 ? args[1] : val_int(0);  /* x */
    self->fields[1] = nargs > 2 ? args[2] : val_int(0);  /* y */
    self->fields[2] = nargs > 3 ? args[3] : val_int(100); /* hp */
    args[0] = val_obj((Obj *)self); /* return self */
    return 1;
}

/* Entity.damage(amount) */
static int nat_entity_damage(VM *vm, Value *args, int nargs)
{
    (void)vm; (void)nargs;
    ObjInstance *self = as_instance(args[0]);
    int64_t amount = args[1].as.integer;
    self->fields[2].as.integer -= amount; /* hp -= amount */
    return 0;
}

/* Entity.get_hp() */
static int nat_entity_get_hp(VM *vm, Value *args, int nargs)
{
    (void)vm; (void)nargs;
    ObjInstance *self = as_instance(args[0]);
    args[0] = self->fields[2]; /* return hp */
    return 1;
}

/* Entity.move(dx, dy) */
static int nat_entity_move(VM *vm, Value *args, int nargs)
{
    (void)vm; (void)nargs;
    ObjInstance *self = as_instance(args[0]);
    self->fields[0].as.integer += args[1].as.integer; /* x += dx */
    self->fields[1].as.integer += args[2].as.integer; /* y += dy */
    return 0;
}

/* =========================================================
** Test helpers
** ========================================================= */

static int tests_run = 0;
static int tests_passed = 0;

#define CHECK(cond, msg)                                            \
    do {                                                            \
        if (!(cond)) {                                              \
            printf("  FAIL: %s (line %d)\n", msg, __LINE__);        \
            return false;                                           \
        }                                                           \
    } while (0)

#define RUN_TEST(fn)                \
    do {                            \
        tests_run++;                \
        printf("%-50s ", #fn);      \
        fflush(stdout);             \
        if (fn()) {                 \
            tests_passed++;         \
            printf("PASS\n");       \
        } else {                    \
            printf("FAIL\n");       \
        }                           \
    } while (0)

/* =========================================================
** Test 1: Create class from C++, instantiate, call methods
** ========================================================= */

static bool test_native_class_basic()
{
    VM vm;

    /* Define Entity class with fields and native methods */
    ObjClass *entity = vm.def_class("Entity")
        .field("x")
        .field("y")
        .field("hp")
        .method("init", nat_entity_init, 3)
        .method("damage", nat_entity_damage, 1)
        .method("get_hp", nat_entity_get_hp, 0)
        .method("move", nat_entity_move, 2)
        .end();

    CHECK(entity != nullptr, "class created");
    CHECK(entity->num_fields == 3, "3 fields");
    CHECK(entity->vtable_size > 0, "vtable allocated");

    /* Create instance from C++ */
    Value args[] = { val_int(10), val_int(20), val_int(100) };
    Value inst = vm.make_instance(entity, args, 3);
    CHECK(is_instance(inst), "is instance");

    ObjInstance *obj = as_instance(inst);
    CHECK(obj->fields[0].as.integer == 10, "x = 10");
    CHECK(obj->fields[1].as.integer == 20, "y = 20");
    CHECK(obj->fields[2].as.integer == 100, "hp = 100");

    /* Call damage via vtable slot */
    int slot_damage = vm.find_selector("damage", 6);
    CHECK(slot_damage >= 0, "damage slot found");

    Value dmg_args[] = { val_int(30) };
    vm.invoke(inst, slot_damage, dmg_args, 1);
    CHECK(obj->fields[2].as.integer == 70, "hp = 70 after damage(30)");

    /* Call get_hp */
    Value hp = vm.invoke(inst, "get_hp", nullptr, 0);
    CHECK(hp.as.integer == 70, "get_hp() returns 70");

    /* Call move */
    Value move_args[] = { val_int(5), val_int(-3) };
    vm.invoke(inst, "move", move_args, 2);
    CHECK(obj->fields[0].as.integer == 15, "x = 15 after move(5,-3)");
    CHECK(obj->fields[1].as.integer == 17, "y = 17 after move(5,-3)");

    return true;
}

/* =========================================================
** Test 2: Script inherits C++ class, C++ calls script override
** ========================================================= */

static bool test_script_inherits_native()
{
    VM vm;

    /* Define Entity in C++ */
    ObjClass *entity = vm.def_class("Entity")
        .field("x")
        .field("y")
        .field("hp")
        .method("init", nat_entity_init, 3)
        .method("damage", nat_entity_damage, 1)
        .method("get_hp", nat_entity_get_hp, 0)
        .method("move", nat_entity_move, 2)
        .end();
    (void)entity;

    /* Compile script that defines Boss inheriting Entity */
    const char *script = R"(
        class Boss : Entity {
            var rage;
            def init(x, y) {
                self.x = x;
                self.y = y;
                self.hp = 500;
                self.rage = 0;
            }
            def damage(amount) {
                self.hp = self.hp - amount;
                self.rage = self.rage + 10;
            }
            def get_rage() {
                return self.rage;
            }
        }
    )";

    Compiler compiler;
    ObjFunc *fn = compiler.compile(&vm.get_gc(), &vm, script, "<test>");
    CHECK(fn != nullptr, "compiled OK");
    CHECK(!compiler.had_error(), "no compile errors");
    vm.run(fn);

    /* Get Boss class from globals */
    int boss_idx = vm.find_global("Boss");
    CHECK(boss_idx >= 0, "Boss class registered");
    Value boss_val = vm.get_global(boss_idx);
    CHECK(is_class(boss_val), "Boss is a class");
    ObjClass *boss_class = as_class(boss_val);

    /* Verify Boss inherits Entity */
    CHECK(boss_class->parent == entity, "Boss.parent == Entity");

    /* Create Boss instance from C++ */
    Value boss_args[] = { val_int(50), val_int(60) };
    Value boss = vm.make_instance(boss_class, boss_args, 2);
    CHECK(is_instance(boss), "boss is instance");

    ObjInstance *boss_obj = as_instance(boss);
    CHECK(boss_obj->fields[0].as.integer == 50, "boss.x = 50");
    CHECK(boss_obj->fields[1].as.integer == 60, "boss.y = 60");
    CHECK(boss_obj->fields[2].as.integer == 500, "boss.hp = 500");

    /* Call damage on Boss — should use SCRIPT override, not C++ native */
    int slot_damage = vm.find_selector("damage", 6);
    Value dmg[] = { val_int(80) };
    vm.invoke(boss, slot_damage, dmg, 1);

    /* Check: hp decreased AND rage increased (proves script override ran) */
    CHECK(boss_obj->fields[2].as.integer == 420, "boss.hp = 420 (script damage)");
    CHECK(boss_obj->fields[3].as.integer == 10, "boss.rage = 10 (script damage)");

    /* Call move on Boss — should use NATIVE from Entity (inherited) */
    int slot_move = vm.find_selector("move", 4);
    Value move_args[] = { val_int(100), val_int(200) };
    vm.invoke(boss, slot_move, move_args, 2);
    CHECK(boss_obj->fields[0].as.integer == 150, "boss.x = 150 (native move)");
    CHECK(boss_obj->fields[1].as.integer == 260, "boss.y = 260 (native move)");

    /* Verify polymorphism: same slot, different class = different method */
    Value ent_args[] = { val_int(0), val_int(0), val_int(100) };
    Value ent = vm.make_instance(entity, ent_args, 3);
    Value ent_dmg[] = { val_int(25) };
    vm.invoke(ent, slot_damage, ent_dmg, 1);
    CHECK(as_instance(ent)->fields[2].as.integer == 75, "entity.hp = 75 (native damage)");

    return true;
}

/* =========================================================
** Test 3: Multiple classes, same selector → correct dispatch
** ========================================================= */

static bool test_polymorphic_dispatch()
{
    VM vm;

    /* Define base in C++ */
    vm.def_class("Entity")
        .field("x").field("y").field("hp")
        .method("init", nat_entity_init, 3)
        .method("damage", nat_entity_damage, 1)
        .method("get_hp", nat_entity_get_hp, 0)
        .end();

    /* Two different script classes override damage differently */
    const char *script = R"(
        class Warrior : Entity {
            def init(hp) {
                self.x = 0; self.y = 0; self.hp = hp;
            }
            def damage(amount) {
                self.hp = self.hp - amount + 10;
            }
        }

        class Mage : Entity {
            def init(hp) {
                self.x = 0; self.y = 0; self.hp = hp;
            }
            def damage(amount) {
                self.hp = self.hp - amount * 2;
            }
        }
    )";

    Compiler compiler;
    ObjFunc *fn = compiler.compile(&vm.get_gc(), &vm, script, "<test>");
    CHECK(fn != nullptr, "compiled");
    vm.run(fn);

    ObjClass *warrior = as_class(vm.get_global("Warrior"));
    ObjClass *mage = as_class(vm.get_global("Mage"));

    Value w_args[] = { val_int(100) };
    Value m_args[] = { val_int(100) };
    Value w = vm.make_instance(warrior, w_args, 1);
    Value m = vm.make_instance(mage, m_args, 1);

    int slot_damage = vm.find_selector("damage", 6);

    /* Same call, different results — polimorfismo */
    Value dmg[] = { val_int(20) };
    vm.invoke(w, slot_damage, dmg, 1);
    vm.invoke(m, slot_damage, dmg, 1);

    /* Warrior takes reduced: 100 - 20 + 10 = 90 */
    CHECK(as_instance(w)->fields[2].as.integer == 90, "warrior hp=90 (reduced damage)");
    /* Mage takes double: 100 - 20*2 = 60 */
    CHECK(as_instance(m)->fields[2].as.integer == 60, "mage hp=60 (double damage)");

    return true;
}

/* =========================================================
** Main
** ========================================================= */

/* =========================================================
** Test 4: Native ctor/dtor — lifecycle management
** C++ object created/destroyed automatically
** ========================================================= */

struct SpriteData {
    float x, y;
    int width, height;
    bool alive;
};

static int g_sprite_ctor_calls = 0;
static int g_sprite_dtor_calls = 0;

static void *sprite_ctor(VM *vm, int argc, Value *args)
{
    (void)vm;
    g_sprite_ctor_calls++;
    SpriteData *s = new SpriteData();
    s->x = (argc > 0) ? (float)args[0].as.integer : 0.0f;
    s->y = (argc > 1) ? (float)args[1].as.integer : 0.0f;
    s->width = 32;
    s->height = 32;
    s->alive = true;
    return s;
}

static void sprite_dtor(VM *vm, void *data)
{
    (void)vm;
    g_sprite_dtor_calls++;
    delete (SpriteData *)data;
}

/* Sprite.get_x() */
static int sprite_get_x(VM *vm, Value *args, int nargs)
{
    (void)vm; (void)nargs;
    SpriteData *s = zen_instance_data<SpriteData>(args[0]);
    args[0] = val_int((int64_t)s->x);
    return 1;
}

/* Sprite.get_y() */
static int sprite_get_y(VM *vm, Value *args, int nargs)
{
    (void)vm; (void)nargs;
    SpriteData *s = zen_instance_data<SpriteData>(args[0]);
    args[0] = val_int((int64_t)s->y);
    return 1;
}

/* Sprite.set_pos(x, y) */
static int sprite_set_pos(VM *vm, Value *args, int nargs)
{
    (void)vm; (void)nargs;
    SpriteData *s = zen_instance_data<SpriteData>(args[0]);
    s->x = (float)args[1].as.integer;
    s->y = (float)args[2].as.integer;
    return 0;
}

/* Sprite.is_alive() */
static int sprite_is_alive(VM *vm, Value *args, int nargs)
{
    (void)vm; (void)nargs;
    SpriteData *s = zen_instance_data<SpriteData>(args[0]);
    args[0] = val_bool(s->alive);
    return 1;
}

/* Sprite.kill() */
static int sprite_kill(VM *vm, Value *args, int nargs)
{
    (void)vm; (void)nargs;
    SpriteData *s = zen_instance_data<SpriteData>(args[0]);
    s->alive = false;
    return 0;
}

static bool test_native_ctor_dtor()
{
    g_sprite_ctor_calls = 0;
    g_sprite_dtor_calls = 0;

    {
        VM vm;

        ObjClass *sprite_class = vm.def_class("Sprite")
            .ctor(sprite_ctor)
            .dtor(sprite_dtor)
            .method("get_x", sprite_get_x, 0)
            .method("get_y", sprite_get_y, 0)
            .method("set_pos", sprite_set_pos, 2)
            .method("is_alive", sprite_is_alive, 0)
            .method("kill", sprite_kill, 0)
            .end();

        /* Create instance with args — native ctor receives them */
        Value args[] = { val_int(100), val_int(200) };
        Value s = vm.make_instance(sprite_class, args, 2);
        CHECK(g_sprite_ctor_calls == 1, "ctor called once");

        /* Verify native_data was set */
        ObjInstance *inst = as_instance(s);
        CHECK(inst->native_data != nullptr, "native_data set");

        SpriteData *data = zen_instance_data<SpriteData>(s);
        CHECK(data->x == 100.0f, "sprite.x = 100");
        CHECK(data->y == 200.0f, "sprite.y = 200");
        CHECK(data->alive == true, "sprite.alive = true");

        /* Call methods that access native_data */
        Value gx = vm.invoke(s, "get_x", nullptr, 0);
        CHECK(gx.as.integer == 100, "get_x() = 100");

        Value gy = vm.invoke(s, "get_y", nullptr, 0);
        CHECK(gy.as.integer == 200, "get_y() = 200");

        Value pos_args[] = { val_int(300), val_int(400) };
        vm.invoke(s, "set_pos", pos_args, 2);
        CHECK(data->x == 300.0f, "set_pos: x = 300");
        CHECK(data->y == 400.0f, "set_pos: y = 400");

        vm.invoke(s, "kill", nullptr, 0);
        CHECK(data->alive == false, "kill: alive = false");

        /* VM destructor will sweep → dtor must be called */
    }

    CHECK(g_sprite_dtor_calls == 1, "dtor called on GC sweep");
    return true;
}

/* =========================================================
** Test 5: Persistent class — GC never touches, C++ destroys
** ========================================================= */

static int g_engine_ctor_calls = 0;
static int g_engine_dtor_calls = 0;

struct EngineObject {
    int id;
    char name[32];
};

static void *engine_ctor(VM *vm, int argc, Value *args)
{
    (void)vm;
    g_engine_ctor_calls++;
    EngineObject *e = new EngineObject();
    e->id = (argc > 0) ? (int)args[0].as.integer : -1;
    snprintf(e->name, sizeof(e->name), "obj_%d", e->id);
    return e;
}

static void engine_dtor(VM *vm, void *data)
{
    (void)vm;
    g_engine_dtor_calls++;
    delete (EngineObject *)data;
}

static int engine_get_id(VM *vm, Value *args, int nargs)
{
    (void)vm; (void)nargs;
    EngineObject *e = zen_instance_data<EngineObject>(args[0]);
    args[0] = val_int(e->id);
    return 1;
}

static bool test_persistent_class()
{
    g_engine_ctor_calls = 0;
    g_engine_dtor_calls = 0;

    VM vm;

    ObjClass *obj_class = vm.def_class("EngineObj")
        .ctor(engine_ctor)
        .dtor(engine_dtor)
        .persistent(true)
        .method("get_id", engine_get_id, 0)
        .end();

    /* Create persistent instances */
    Value a1[] = { val_int(42) };
    Value a2[] = { val_int(99) };
    Value inst1 = vm.make_instance(obj_class, a1, 1);
    Value inst2 = vm.make_instance(obj_class, a2, 1);

    CHECK(g_engine_ctor_calls == 2, "2 ctor calls");
    CHECK(g_engine_dtor_calls == 0, "0 dtor calls (persistent)");

    /* Verify data */
    EngineObject *e1 = zen_instance_data<EngineObject>(inst1);
    EngineObject *e2 = zen_instance_data<EngineObject>(inst2);
    CHECK(e1->id == 42, "e1.id = 42");
    CHECK(e2->id == 99, "e2.id = 99");

    /* Force GC — persistent must survive */
    vm.collect();
    vm.collect();
    vm.collect();

    /* Still alive after GC */
    CHECK(g_engine_dtor_calls == 0, "dtor NOT called by GC");
    Value id1 = vm.invoke(inst1, "get_id", nullptr, 0);
    CHECK(id1.as.integer == 42, "inst1 still alive after GC");

    /* Explicitly destroy */
    vm.destroy_instance(inst1);
    CHECK(g_engine_dtor_calls == 1, "dtor called on destroy_instance");

    vm.destroy_instance(inst2);
    CHECK(g_engine_dtor_calls == 2, "dtor called for second");

    return true;
}

/* =========================================================
** Test 6: Bidirectional — C++ creates, script controls, C++ reads back
** ========================================================= */

static int g_transform_count = 0;

struct TransformData {
    float x, y, rotation;
    float scale_x, scale_y;
};

static void *transform_ctor(VM *vm, int argc, Value *args)
{
    (void)vm; (void)argc; (void)args;
    g_transform_count++;
    TransformData *t = new TransformData();
    t->x = 0; t->y = 0; t->rotation = 0;
    t->scale_x = 1.0f; t->scale_y = 1.0f;
    return t;
}

static void transform_dtor(VM *vm, void *data)
{
    (void)vm;
    g_transform_count--;
    delete (TransformData *)data;
}

/* Transform.translate(dx, dy) */
static int transform_translate(VM *vm, Value *args, int nargs)
{
    (void)vm; (void)nargs;
    TransformData *t = zen_instance_data<TransformData>(args[0]);
    t->x += (float)args[1].as.integer;
    t->y += (float)args[2].as.integer;
    return 0;
}

/* Transform.rotate(degrees) */
static int transform_rotate(VM *vm, Value *args, int nargs)
{
    (void)vm; (void)nargs;
    TransformData *t = zen_instance_data<TransformData>(args[0]);
    t->rotation += (float)args[1].as.integer;
    return 0;
}

/* Transform.get_x() / get_y() / get_rotation() */
static int transform_get_x(VM *vm, Value *args, int nargs)
{
    (void)vm; (void)nargs;
    TransformData *t = zen_instance_data<TransformData>(args[0]);
    args[0] = val_int((int64_t)t->x);
    return 1;
}
static int transform_get_y(VM *vm, Value *args, int nargs)
{
    (void)vm; (void)nargs;
    TransformData *t = zen_instance_data<TransformData>(args[0]);
    args[0] = val_int((int64_t)t->y);
    return 1;
}
static int transform_get_rotation(VM *vm, Value *args, int nargs)
{
    (void)vm; (void)nargs;
    TransformData *t = zen_instance_data<TransformData>(args[0]);
    args[0] = val_int((int64_t)t->rotation);
    return 1;
}

static bool test_bidirectional_cpp_script()
{
    g_transform_count = 0;
    VM vm;

    ObjClass *transform_class = vm.def_class("Transform")
        .ctor(transform_ctor)
        .dtor(transform_dtor)
        .persistent(true)
        .method("translate", transform_translate, 2)
        .method("rotate", transform_rotate, 1)
        .method("get_x", transform_get_x, 0)
        .method("get_y", transform_get_y, 0)
        .method("get_rotation", transform_get_rotation, 0)
        .end();

    /* C++ creates the object (engine owns it) */
    Value t = vm.make_instance(transform_class, nullptr, 0);
    CHECK(g_transform_count == 1, "transform created");

    /* Register the instance as a global so script can access it */
    vm.def_global("player_transform", t);

    /* Script controls the C++ object */
    const char *script = R"(
        class PlayerController {
            def update(transform) {
                transform.translate(10, 5);
                transform.rotate(45);
                transform.translate(20, 30);
                transform.rotate(90);
            }
        }

        var ctrl = PlayerController();
        ctrl.update(player_transform);
    )";

    Compiler compiler;
    ObjFunc *fn = compiler.compile(&vm.get_gc(), &vm, script, "<test>");
    CHECK(fn != nullptr, "script compiled");
    CHECK(!compiler.had_error(), "no errors");
    vm.run(fn);
    CHECK(!vm.had_error(), "no runtime error");

    /* C++ reads back the result — script modified our C++ object */
    TransformData *td = zen_instance_data<TransformData>(t);
    CHECK((int)td->x == 30, "transform.x = 30 (10+20)");
    CHECK((int)td->y == 35, "transform.y = 35 (5+30)");
    CHECK((int)td->rotation == 135, "transform.rotation = 135 (45+90)");

    /* C++ calls method on same object — verify it still works */
    Value args[] = { val_int(100), val_int(100) };
    vm.invoke(t, "translate", args, 2);
    CHECK((int)td->x == 130, "translate from C++: x = 130");

    vm.destroy_instance(t);
    CHECK(g_transform_count == 0, "transform destroyed");

    return true;
}

/* =========================================================
** Test 7: Script inherits native with native_data
** Script class overrides method but still accesses C++ data
** ========================================================= */

static bool test_script_inherits_native_with_data()
{
    VM vm;

    ObjClass *sprite_class = vm.def_class("Sprite")
        .ctor(sprite_ctor)
        .dtor(sprite_dtor)
        .method("get_x", sprite_get_x, 0)
        .method("get_y", sprite_get_y, 0)
        .method("set_pos", sprite_set_pos, 2)
        .method("is_alive", sprite_is_alive, 0)
        .method("kill", sprite_kill, 0)
        .end();
    (void)sprite_class;

    g_sprite_ctor_calls = 0;
    g_sprite_dtor_calls = 0;

    /* Script defines a subclass */
    const char *script = R"(
        class Player : Sprite {
            var score;
            def init() {
                self.score = 0;
            }
            def add_score(n) {
                self.score = self.score + n;
            }
            def get_score() {
                return self.score;
            }
        }
    )";

    Compiler compiler;
    ObjFunc *fn = compiler.compile(&vm.get_gc(), &vm, script, "<test>");
    CHECK(fn != nullptr, "compiled");
    vm.run(fn);

    ObjClass *player_class = as_class(vm.get_global("Player"));
    CHECK(player_class->parent == sprite_class, "Player inherits Sprite");

    /* Create Player — native ctor runs (from Sprite), then script init runs */
    Value args[] = { val_int(50), val_int(75) };
    Value p = vm.make_instance(player_class, args, 2);
    CHECK(g_sprite_ctor_calls == 1, "native ctor ran for subclass");

    /* native_data should be set (inherited from Sprite's ctor) */
    ObjInstance *pinst = as_instance(p);
    CHECK(pinst->native_data != nullptr, "native_data inherited");

    SpriteData *sd = zen_instance_data<SpriteData>(p);
    CHECK(sd->x == 50.0f, "sprite x from ctor args");
    CHECK(sd->y == 75.0f, "sprite y from ctor args");

    /* Call inherited native method */
    Value gx = vm.invoke(p, "get_x", nullptr, 0);
    CHECK(gx.as.integer == 50, "inherited get_x works");

    /* Call script-only method */
    Value score_args[] = { val_int(100) };
    vm.invoke(p, "add_score", score_args, 1);
    Value score = vm.invoke(p, "get_score", nullptr, 0);
    CHECK(score.as.integer == 100, "script method works");

    /* Call inherited native set_pos → modifies C++ data */
    Value pos[] = { val_int(999), val_int(888) };
    vm.invoke(p, "set_pos", pos, 2);
    CHECK(sd->x == 999.0f, "set_pos from script subclass updates C++");
    CHECK(sd->y == 888.0f, "set_pos y");

    return true;
}

/* =========================================================
** Test 8: GC stress — many non-persistent native instances
** Ensure dtor called for all when GC collects
** ========================================================= */

static int g_gc_stress_alive = 0;

struct StressObj { int id; };

static void *stress_ctor(VM *vm, int argc, Value *args)
{
    (void)vm; (void)argc; (void)args;
    g_gc_stress_alive++;
    return new StressObj{g_gc_stress_alive};
}

static void stress_dtor(VM *vm, void *data)
{
    (void)vm;
    g_gc_stress_alive--;
    delete (StressObj *)data;
}

static int stress_get_id(VM *vm, Value *args, int nargs)
{
    (void)vm; (void)nargs;
    StressObj *s = zen_instance_data<StressObj>(args[0]);
    args[0] = val_int(s->id);
    return 1;
}

static bool test_gc_stress_native()
{
    g_gc_stress_alive = 0;

    {
        VM vm;
        ObjClass *cls = vm.def_class("StressObj")
            .ctor(stress_ctor)
            .dtor(stress_dtor)
            .method("get_id", stress_get_id, 0)
            .end();

        /* Create many instances — some will be GC roots (in globals), some not */
        Value instances[100];
        for (int i = 0; i < 100; i++)
        {
            instances[i] = vm.make_instance(cls, nullptr, 0);
        }
        CHECK(g_gc_stress_alive == 100, "100 objects alive");

        /* Verify they all work */
        for (int i = 0; i < 100; i++)
        {
            StressObj *s = zen_instance_data<StressObj>(instances[i]);
            CHECK(s != nullptr, "native_data valid");
            CHECK(s->id == i + 1, "correct id");
        }

        /* VM destructor sweeps everything */
    }

    CHECK(g_gc_stress_alive == 0, "all 100 dtors called on VM destroy");
    return true;
}

/* =========================================================
** Test 9: Multiple persistent instances, partial destroy
** ========================================================= */

static bool test_persistent_partial_destroy()
{
    g_engine_ctor_calls = 0;
    g_engine_dtor_calls = 0;

    VM vm;

    ObjClass *cls = vm.def_class("PersObj")
        .ctor(engine_ctor)
        .dtor(engine_dtor)
        .persistent(true)
        .method("get_id", engine_get_id, 0)
        .end();

    /* Create 5 persistent instances */
    Value objs[5];
    for (int i = 0; i < 5; i++)
    {
        Value a[] = { val_int(i * 10) };
        objs[i] = vm.make_instance(cls, a, 1);
    }
    CHECK(g_engine_ctor_calls == 5, "5 created");

    /* Destroy only #1 and #3 */
    vm.destroy_instance(objs[1]);
    vm.destroy_instance(objs[3]);
    CHECK(g_engine_dtor_calls == 2, "2 destroyed");

    /* Remaining still work */
    Value id0 = vm.invoke(objs[0], "get_id", nullptr, 0);
    Value id2 = vm.invoke(objs[2], "get_id", nullptr, 0);
    Value id4 = vm.invoke(objs[4], "get_id", nullptr, 0);
    CHECK(id0.as.integer == 0, "obj[0] alive");
    CHECK(id2.as.integer == 20, "obj[2] alive");
    CHECK(id4.as.integer == 40, "obj[4] alive");

    /* Force GC multiple times — persistent survive */
    for (int i = 0; i < 10; i++) vm.collect();
    CHECK(g_engine_dtor_calls == 2, "GC didn't touch persistent");

    /* Cleanup remaining */
    vm.destroy_instance(objs[0]);
    vm.destroy_instance(objs[2]);
    vm.destroy_instance(objs[4]);
    CHECK(g_engine_dtor_calls == 5, "all destroyed manually");

    return true;
}

/* =========================================================
** Test 10: Bidirectional — Script creates instance of native class,
** C++ reads back. (Script calls ctor through normal class() syntax)
** ========================================================= */

static bool test_script_creates_native_instance()
{
    g_sprite_ctor_calls = 0;
    g_sprite_dtor_calls = 0;

    {
        VM vm;

        vm.def_class("Sprite")
            .ctor(sprite_ctor)
            .dtor(sprite_dtor)
            .method("get_x", sprite_get_x, 0)
            .method("get_y", sprite_get_y, 0)
            .method("set_pos", sprite_set_pos, 2)
            .method("is_alive", sprite_is_alive, 0)
            .method("kill", sprite_kill, 0)
            .end();

        /* Script creates instances of native class */
        const char *script = R"(
            var s1 = Sprite(10, 20);
            var s2 = Sprite(30, 40);
            s1.set_pos(111, 222);
            s2.kill();
        )";

        Compiler compiler;
        ObjFunc *fn = compiler.compile(&vm.get_gc(), &vm, script, "<test>");
        CHECK(fn != nullptr, "compiled");
        CHECK(!compiler.had_error(), "no errors");
        vm.run(fn);
        CHECK(!vm.had_error(), "no runtime error");

        CHECK(g_sprite_ctor_calls == 2, "2 sprites created by script");

        /* Get instances from script globals */
        Value s1_val = vm.get_global("s1");
        Value s2_val = vm.get_global("s2");
        CHECK(is_instance(s1_val), "s1 is instance");
        CHECK(is_instance(s2_val), "s2 is instance");

        /* Verify C++ data was modified by script */
        SpriteData *sd1 = zen_instance_data<SpriteData>(s1_val);
        SpriteData *sd2 = zen_instance_data<SpriteData>(s2_val);
        CHECK((int)sd1->x == 111, "s1.x = 111 (script set_pos)");
        CHECK((int)sd1->y == 222, "s1.y = 222 (script set_pos)");
        CHECK(sd1->alive == true, "s1 alive");
        CHECK(sd2->alive == false, "s2 killed by script");
    }

    CHECK(g_sprite_dtor_calls == 2, "both sprites dtor'd on VM exit");
    return true;
}

/* =========================================================
** Test 11: Non-constructable class — script cannot instantiate
** ========================================================= */

static bool test_non_constructable()
{
    g_engine_ctor_calls = 0;
    g_engine_dtor_calls = 0;

    VM vm;

    ObjClass *cls = vm.def_class("Transform")
        .ctor(engine_ctor)
        .dtor(engine_dtor)
        .persistent(true)
        .constructable(false)
        .method("get_id", engine_get_id, 0)
        .end();

    /* C++ CAN still create instances */
    Value a[] = { val_int(7) };
    Value inst = vm.make_instance(cls, a, 1);
    CHECK(g_engine_ctor_calls == 1, "C++ can create");
    Value id = vm.invoke(inst, "get_id", nullptr, 0);
    CHECK(id.as.integer == 7, "instance works");

    /* Script CANNOT create */
    const char *script = R"(
        var t = Transform(1);
    )";

    Compiler compiler;
    ObjFunc *fn = compiler.compile(&vm.get_gc(), &vm, script, "<test>");
    CHECK(fn != nullptr, "compiled (syntax is valid)");
    vm.run(fn);
    CHECK(vm.had_error(), "runtime error occurred");
    CHECK(g_engine_ctor_calls == 1, "script did NOT create instance");

    /* But script can USE an instance created by C++ */
    vm.def_global("player_t", inst);

    const char *script2 = R"(
        var x = player_t.get_id();
    )";

    Compiler compiler2;
    ObjFunc *fn2 = compiler2.compile(&vm.get_gc(), &vm, script2, "<test2>");
    CHECK(fn2 != nullptr, "script2 compiled");
    vm.run(fn2);
    /* Note: had_error_ may still be true from previous run, 
       but the point is get_id works on the instance */

    vm.destroy_instance(inst);
    CHECK(g_engine_dtor_calls == 1, "destroyed by C++");
    return true;
}

/* =========================================================
** Test 12: Reentry — native method creates another instance
** Tests GC safety when allocation happens inside a native call
** ========================================================= */

static int g_factory_created = 0;

struct FactoryProduct {
    int serial;
};

static void *product_ctor(VM *vm, int argc, Value *args)
{
    (void)vm;
    g_factory_created++;
    FactoryProduct *p = new FactoryProduct();
    p->serial = (argc > 0) ? (int)args[0].as.integer : 0;
    return p;
}

static void product_dtor(VM *vm, void *data)
{
    (void)vm;
    g_factory_created--;
    delete (FactoryProduct *)data;
}

static int product_get_serial(VM *vm, Value *args, int nargs)
{
    (void)vm; (void)nargs;
    FactoryProduct *p = zen_instance_data<FactoryProduct>(args[0]);
    args[0] = val_int(p->serial);
    return 1;
}

/* Factory.spawn(serial) — creates a NEW Product instance inside a native method */
static ObjClass *g_product_class = nullptr;

static int factory_spawn(VM *vm, Value *args, int nargs)
{
    (void)nargs;
    /* This is the reentry: allocating a new ObjInstance while inside a native call */
    Value ctor_args[] = { args[1] }; /* serial number */
    Value new_inst = vm->make_instance(g_product_class, ctor_args, 1);
    args[0] = new_inst;
    return 1;
}

/* Factory.spawn_many(count) — creates N instances in a loop (stress reentry) */
static int factory_spawn_many(VM *vm, Value *args, int nargs)
{
    (void)nargs;
    int count = (int)args[1].as.integer;
    Value last = val_nil();
    for (int i = 0; i < count; i++)
    {
        Value ctor_args[] = { val_int(i * 100) };
        last = vm->make_instance(g_product_class, ctor_args, 1);
    }
    args[0] = last; /* return last created */
    return 1;
}

static bool test_native_method_creates_instance()
{
    g_factory_created = 0;
    g_product_class = nullptr;

    {
        VM vm;

        g_product_class = vm.def_class("Product")
            .ctor(product_ctor)
            .dtor(product_dtor)
            .method("get_serial", product_get_serial, 0)
            .end();

        vm.def_class("Factory")
            .method("spawn", factory_spawn, 1)
            .method("spawn_many", factory_spawn_many, 1)
            .end();

        /* Script uses factory to create products */
        const char *script = R"(
            var factory = Factory();
            var p1 = factory.spawn(42);
            var p2 = factory.spawn(99);
            var p3 = factory.spawn(7);
        )";

        Compiler compiler;
        ObjFunc *fn = compiler.compile(&vm.get_gc(), &vm, script, "<test>");
        CHECK(fn != nullptr, "compiled");
        CHECK(!compiler.had_error(), "no errors");
        vm.run(fn);
        CHECK(!vm.had_error(), "no runtime error");

        CHECK(g_factory_created == 3, "3 products created via reentry");

        /* Verify the instances are valid */
        Value p1 = vm.get_global("p1");
        Value p2 = vm.get_global("p2");
        Value p3 = vm.get_global("p3");
        CHECK(is_instance(p1), "p1 is instance");
        CHECK(is_instance(p2), "p2 is instance");
        CHECK(is_instance(p3), "p3 is instance");

        FactoryProduct *fp1 = zen_instance_data<FactoryProduct>(p1);
        FactoryProduct *fp2 = zen_instance_data<FactoryProduct>(p2);
        FactoryProduct *fp3 = zen_instance_data<FactoryProduct>(p3);
        CHECK(fp1->serial == 42, "p1.serial = 42");
        CHECK(fp2->serial == 99, "p2.serial = 99");
        CHECK(fp3->serial == 7, "p3.serial = 7");

        /* Stress: spawn_many creates 50 instances inside one native call */
        const char *stress_script = R"(
            var factory2 = Factory();
            var last = factory2.spawn_many(50);
        )";

        Compiler compiler2;
        ObjFunc *fn2 = compiler2.compile(&vm.get_gc(), &vm, stress_script, "<stress>");
        CHECK(fn2 != nullptr, "stress compiled");
        vm.run(fn2);
        CHECK(!vm.had_error(), "no runtime error in stress");

        /* 3 from before + 50 from spawn_many = 53 */
        CHECK(g_factory_created == 53, "53 total products alive");

        /* Verify last one is valid */
        Value last = vm.get_global("last");
        CHECK(is_instance(last), "last is instance");
        FactoryProduct *fl = zen_instance_data<FactoryProduct>(last);
        CHECK(fl->serial == 4900, "last.serial = 4900 (49*100)");
    }

    /* All dtors called on VM destroy */
    CHECK(g_factory_created == 0, "all products destroyed by GC");
    g_product_class = nullptr;
    return true;
}

/* =========================================================
** Test 13: Raylib pattern — NativeStruct (Color) + NativeStruct (Texture)
** + free function that receives both as arguments.
** Simulates: DrawTexture(texture, x, y, color)
** All lightweight — VM allocates buffer, ctor just fills. Zero malloc.
** ========================================================= */

/* Simple Color struct — value type, fields by offset */
struct TestColor {
    uint8_t r, g, b, a;
};

static void color_ctor(VM *vm, void *buffer, int argc, Value *args)
{
    (void)vm;
    TestColor *c = (TestColor *)buffer;
    c->r = (argc > 0) ? (uint8_t)args[0].as.integer : 255;
    c->g = (argc > 1) ? (uint8_t)args[1].as.integer : 255;
    c->b = (argc > 2) ? (uint8_t)args[2].as.integer : 255;
    c->a = (argc > 3) ? (uint8_t)args[3].as.integer : 255;
}

/* Texture struct — also a value type (like raylib's Texture2D) */
struct TestTexture {
    int id;
    int width, height;
    int mipmaps;
    int format;
};

static void texture_ctor(VM *vm, void *buffer, int argc, Value *args)
{
    (void)vm;
    TestTexture *t = (TestTexture *)buffer;
    t->id = (argc > 0) ? (int)args[0].as.integer : 0;
    t->width = (argc > 1) ? (int)args[1].as.integer : 64;
    t->height = (argc > 2) ? (int)args[2].as.integer : 64;
    t->mipmaps = 1;
    t->format = 7; /* PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 */
}

/* Track draw calls for verification */
struct DrawCall {
    int tex_id;
    int x, y;
    uint8_t r, g, b, a;
};
static DrawCall g_last_draw = {};
static int g_draw_count = 0;

/* Free function: draw_texture(texture, x, y, color)
** Receives TWO NativeStructs as arguments — zero heap alloc */
static int nat_draw_texture(VM *vm, Value *args, int nargs)
{
    (void)vm; (void)nargs;
    /* args[0] = texture (ObjNativeStruct) */
    /* args[1] = x (int) */
    /* args[2] = y (int) */
    /* args[3] = color (ObjNativeStruct) */

    TestTexture *tex = zen_struct_ptr(args[0], TestTexture);
    int x = (int)args[1].as.integer;
    int y = (int)args[2].as.integer;
    TestColor *col = zen_struct_ptr(args[3], TestColor);

    g_last_draw.tex_id = tex->id;
    g_last_draw.x = x;
    g_last_draw.y = y;
    g_last_draw.r = col->r;
    g_last_draw.g = col->g;
    g_last_draw.b = col->b;
    g_last_draw.a = col->a;
    g_draw_count++;

    return 0;
}

/* Free function: clear_background(color) — just a native struct arg */
static uint8_t g_bg_r = 0, g_bg_g = 0, g_bg_b = 0;

static int nat_clear_background(VM *vm, Value *args, int nargs)
{
    (void)vm; (void)nargs;
    TestColor *c = zen_struct_ptr(args[0], TestColor);
    g_bg_r = c->r;
    g_bg_g = c->g;
    g_bg_b = c->b;
    return 0;
}

static bool test_raylib_pattern()
{
    g_draw_count = 0;
    g_last_draw = {};
    g_bg_r = g_bg_g = g_bg_b = 0;

    {
        VM vm;

        /* Register Color as native struct (value type, fields by offset) */
        vm.register_native_struct("Color", sizeof(TestColor), color_ctor, nullptr)
            .field("r", offsetof(TestColor, r), FIELD_BYTE)
            .field("g", offsetof(TestColor, g), FIELD_BYTE)
            .field("b", offsetof(TestColor, b), FIELD_BYTE)
            .field("a", offsetof(TestColor, a), FIELD_BYTE)
            .end();

        /* Register Texture as native struct too — it's just metadata */
        vm.register_native_struct("Texture", sizeof(TestTexture), texture_ctor, nullptr)
            .field("id", offsetof(TestTexture, id), FIELD_INT)
            .field("width", offsetof(TestTexture, width), FIELD_INT)
            .field("height", offsetof(TestTexture, height), FIELD_INT)
            .field("mipmaps", offsetof(TestTexture, mipmaps), FIELD_INT)
            .field("format", offsetof(TestTexture, format), FIELD_INT)
            .end();

        /* Register free functions (like raylib C API) */
        vm.def_native("draw_texture", nat_draw_texture, 4);
        vm.def_native("clear_background", nat_clear_background, 1);

        /* Script uses both structs together */
        const char *script = R"(
            var red = Color(255, 0, 0, 255);
            var blue = Color(0, 0, 255, 128);
            var tex = Texture(42, 256, 128);

            clear_background(red);
            draw_texture(tex, 100, 200, blue);
            draw_texture(tex, 300, 400, red);
        )";

        Compiler compiler;
        ObjFunc *fn = compiler.compile(&vm.get_gc(), &vm, script, "<test>");
        CHECK(fn != nullptr, "compiled");
        CHECK(!compiler.had_error(), "no compile errors");
        vm.run(fn);
        CHECK(!vm.had_error(), "no runtime error");

        /* Verify clear_background received Color struct correctly */
        CHECK(g_bg_r == 255, "bg.r = 255");
        CHECK(g_bg_g == 0, "bg.g = 0");
        CHECK(g_bg_b == 0, "bg.b = 0");

        /* Verify draw calls */
        CHECK(g_draw_count == 2, "2 draw calls");

        /* Last draw: tex=42, pos=(300,400), color=red */
        CHECK(g_last_draw.tex_id == 42, "last draw tex_id = 42");
        CHECK(g_last_draw.x == 300, "last draw x = 300");
        CHECK(g_last_draw.y == 400, "last draw y = 400");
        CHECK(g_last_draw.r == 255, "last draw color.r = 255");
        CHECK(g_last_draw.g == 0, "last draw color.g = 0");
        CHECK(g_last_draw.b == 0, "last draw color.b = 0");

        /* Verify struct field access from script */
        const char *field_script = R"(
            var c = Color(10, 20, 30, 40);
            var r_val = c.r;
            var g_val = c.g;
            var b_val = c.b;
            var a_val = c.a;
            var t = Texture(99, 512, 256);
            var tw = t.width;
            var th = t.height;
        )";

        Compiler compiler2;
        ObjFunc *fn2 = compiler2.compile(&vm.get_gc(), &vm, field_script, "<fields>");
        CHECK(fn2 != nullptr, "field script compiled");
        vm.run(fn2);
        CHECK(!vm.had_error(), "no runtime error in field access");

        CHECK(vm.get_global("r_val").as.integer == 10, "c.r = 10");
        CHECK(vm.get_global("g_val").as.integer == 20, "c.g = 20");
        CHECK(vm.get_global("b_val").as.integer == 30, "c.b = 30");
        CHECK(vm.get_global("a_val").as.integer == 40, "c.a = 40");
        CHECK(vm.get_global("tw").as.integer == 512, "tex.width = 512");
        CHECK(vm.get_global("th").as.integer == 256, "tex.height = 256");

        /* Verify struct field WRITE from script */
        const char *write_script = R"(
            var c2 = Color(0, 0, 0, 0);
            c2.r = 123;
            c2.g = 45;
            c2.b = 67;
            c2.a = 89;
            clear_background(c2);
        )";

        Compiler compiler3;
        ObjFunc *fn3 = compiler3.compile(&vm.get_gc(), &vm, write_script, "<write>");
        CHECK(fn3 != nullptr, "write script compiled");
        vm.run(fn3);
        CHECK(!vm.had_error(), "no runtime error in field write");
        CHECK(g_bg_r == 123, "written c2.r = 123");
        CHECK(g_bg_g == 45, "written c2.g = 45");
        CHECK(g_bg_b == 67, "written c2.b = 67");
    }

    return true;
}

/* entity.get_component<Transform>(123) — a native generic method. Type args
** and value args arrive as two separate arrays, and `receiver` is explicit
** rather than folded into args[0] (see GenericNativeFn in object.h). */
static int nat_get_component(VM *, Value receiver,
                             Value *type_args, int ntype_args,
                             Value *args, int nargs)
{
    bool ok = is_instance(receiver) &&
              ntype_args == 1 &&
              is_class(type_args[0]) &&
              strcmp(as_class(type_args[0])->name->chars, "Transform") == 0 &&
              nargs == 1 &&
              is_int(args[0]) &&
              args[0].as.integer == 123;
    args[0] = val_bool(ok);
    return 1;
}

/* Two type parameters, zero value arguments — proves the two counts really
** are independent, not one folded argument list. */
static int nat_pair_of_types(VM *, Value,
                             Value *type_args, int ntype_args,
                             Value *args, int)
{
    bool ok = ntype_args == 2 &&
              is_class(type_args[0]) && is_class(type_args[1]) &&
              strcmp(as_class(type_args[0])->name->chars, "Transform") == 0 &&
              strcmp(as_class(type_args[1])->name->chars, "Sprite") == 0;
    args[0] = val_bool(ok);
    return 1;
}

/* Signals failure the native way: a negative return must surface as a runtime
** error, never be swallowed into a nil result. */
static int nat_generic_fails(VM *, Value, Value *, int, Value *, int)
{
    return -1;
}

static bool test_native_generic_method()
{
    VM vm;

    vm.def_class("Transform").end();
    vm.def_class("Sprite").end();

    vm.def_class("Holder")
        .generic_method("getComponent", nat_get_component, 1, 1)
        .generic_method("pairOf", nat_pair_of_types, 2, 0)
        .end();

    const char *script = R"(
        var holder = Holder();
        var ok_method = holder.getComponent<Transform>(123);
        var ok_pair = holder.pairOf<Transform, Sprite>();
        var comparison_still_works = 1 < 2;
    )";

    Compiler compiler;
    ObjFunc *fn = compiler.compile(&vm.get_gc(), &vm, script, "<native-generic>");
    CHECK(fn != nullptr, "native generic method compiled");
    CHECK(!compiler.had_error(), "no compile errors");
    vm.run(fn);
    CHECK(!vm.had_error(), "no runtime error");

    CHECK(vm.get_global("ok_method").as.boolean, "receiver, 1 type arg and 1 value arg arrive split");
    CHECK(vm.get_global("ok_pair").as.boolean, "2 type args with 0 value args");
    CHECK(vm.get_global("comparison_still_works").as.boolean, "comparison still parses normally");

    return true;
}

static bool test_native_generic_error_paths()
{
    /* A native returning < 0 must raise, not yield nil. */
    {
        VM vm;
        vm.def_class("Transform").end();
        vm.def_class("Boom").generic_method("go", nat_generic_fails, 1, 0).end();

        const char *script = R"(
            var b = Boom();
            b.go<Transform>();
        )";
        Compiler compiler;
        ObjFunc *fn = compiler.compile(&vm.get_gc(), &vm, script, "<native-generic-err>");
        CHECK(fn != nullptr, "compiled");
        CHECK(!compiler.had_error(), "no compile errors");
        vm.run(fn);
        CHECK(vm.had_error(), "a negative native return raises instead of being swallowed");
    }

    /* Calling a generic native through the plain (non-generic) path must be
    ** rejected rather than reading the wrong union member. */
    {
        VM vm;
        vm.def_class("Transform").end();
        vm.def_class("Holder").generic_method("getComponent", nat_get_component, 1, 1).end();

        const char *script = R"(
            var holder = Holder();
            holder.getComponent(123);
        )";
        Compiler compiler;
        ObjFunc *fn = compiler.compile(&vm.get_gc(), &vm, script, "<native-generic-bypass>");
        CHECK(fn != nullptr, "compiled");
        vm.run(fn);
        CHECK(vm.had_error(), "generic native reached without <...> is rejected");
    }

    return true;
}

static bool test_class_operator_overloads()
{
    VM vm;

    const char *script = R"(
        class Vec2 {
            var x;
            var y;

            def init(x, y) {
                self.x = x;
                self.y = y;
            }

            def __add__(other) {
                return Vec2(self.x + other.x, self.y + other.y);
            }

            def __sub__(other) {
                return Vec2(self.x - other.x, self.y - other.y);
            }

            def __mul__(scale) {
                return Vec2(self.x * scale, self.y * scale);
            }

            def __rmul__(scale) {
                return Vec2(self.x * scale, self.y * scale);
            }

            def __neg__() {
                return Vec2(-self.x, -self.y);
            }

            def __eq__(other) {
                return self.x == other.x && self.y == other.y;
            }
        }

        def run_vec2_ops() {
            var a = Vec2(1, 2);
            var b = Vec2(3, 4);
            var c = a + b;
            var d = c - a;
            var e = d * 2;
            var f = 3 * a;
            var g = -b;

            ok_add = c.x == 4 && c.y == 6;
            ok_sub = d.x == 3 && d.y == 4;
            ok_mul = e.x == 6 && e.y == 8;
            ok_rmul = f.x == 3 && f.y == 6;
            ok_neg = g.x == -3 && g.y == -4;
            ok_eq = Vec2(1, 2) == a;
        }

        run_vec2_ops();
    )";

    Compiler compiler;
    ObjFunc *fn = compiler.compile(&vm.get_gc(), &vm, script, "<operator-overloads>");
    CHECK(fn != nullptr, "operator overload script compiled");
    CHECK(!compiler.had_error(), "no compile errors");
    vm.run(fn);
    CHECK(!vm.had_error(), "no runtime error");

    CHECK(vm.get_global("ok_add").as.boolean, "Vec2 __add__ works");
    CHECK(vm.get_global("ok_sub").as.boolean, "Vec2 __sub__ works");
    CHECK(vm.get_global("ok_mul").as.boolean, "Vec2 __mul__ works");
    CHECK(vm.get_global("ok_rmul").as.boolean, "Vec2 __rmul__ works");
    CHECK(vm.get_global("ok_neg").as.boolean, "Vec2 __neg__ works");
    CHECK(vm.get_global("ok_eq").as.boolean, "Vec2 __eq__ works");

    return true;
}

int main()
{
    printf("=== Class API Tests ===\n\n");

    RUN_TEST(test_native_class_basic);
    RUN_TEST(test_script_inherits_native);
    RUN_TEST(test_polymorphic_dispatch);
    RUN_TEST(test_native_ctor_dtor);
    RUN_TEST(test_persistent_class);
    RUN_TEST(test_bidirectional_cpp_script);
    RUN_TEST(test_script_inherits_native_with_data);
    RUN_TEST(test_gc_stress_native);
    RUN_TEST(test_persistent_partial_destroy);
    RUN_TEST(test_script_creates_native_instance);
    RUN_TEST(test_non_constructable);
    RUN_TEST(test_native_method_creates_instance);
    RUN_TEST(test_raylib_pattern);
    RUN_TEST(test_native_generic_method);
    RUN_TEST(test_native_generic_error_paths);
    RUN_TEST(test_class_operator_overloads);

    printf("\n=== %d / %d PASSED ===\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
