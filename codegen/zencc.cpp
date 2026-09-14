/*
** zencc.cpp - native C++ source emission entry point.
*/
#include "bb_parser.h"
#include "bb_runtime.h"
#include "c_codegen.h"
#include "runtime.h"
#include "vm.h"

#include <cstdio>
#include <fstream>
#include <sstream>

using namespace zen;

static void install_window_declarations(bb::BBRuntime *runtime)
{
    bb::DeclSeq *decls = runtime->env->funcDecls;

    auto decl_void = [&](const char *name, bb::DeclSeq *params) {
        decls->insertDecl(name, new bb::FuncType(bb::Type::void_type, params, false, false),
                          bb::DECL_FUNC);
    };
    auto decl_int = [&](const char *name, bb::DeclSeq *params) {
        decls->insertDecl(name, new bb::FuncType(bb::Type::int_type, params, false, false),
                          bb::DECL_FUNC);
    };
    auto decl_str = [&](const char *name, bb::DeclSeq *params) {
        decls->insertDecl(name, new bb::FuncType(bb::Type::string_type, params, false, false),
                          bb::DECL_FUNC);
    };
    auto param_int = [&](const char *name, long long def = 0, bool has_def = false) {
        auto *ds = new bb::DeclSeq();
        ds->insertDecl(name, bb::Type::int_type, bb::DECL_PARAM,
                       has_def ? new bb::ConstType(def) : 0);
        return ds;
    };
    auto param_str = [&](const char *name) {
        auto *ds = new bb::DeclSeq();
        ds->insertDecl(name, bb::Type::string_type, bb::DECL_PARAM);
        return ds;
    };
    auto no_params = [&]() { return new bb::DeclSeq(); };

    /* Graphics / Graphics3D */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("width", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("height", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("depth", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        p->insertDecl("mode", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        decl_void("graphics", p);
        decl_void("graphics3d", new bb::DeclSeq(*p));
    }
    decl_void("endgraphics", no_params());

    /* Flip */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("vwait", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(1LL));
        decl_void("flip", p);
    }

    /* Cls / ClsColor */
    decl_void("cls", no_params());
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("red", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("green", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("blue", bb::Type::int_type, bb::DECL_PARAM);
        decl_void("clscolor", p);
    }

    /* Color */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("red", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("green", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("blue", bb::Type::int_type, bb::DECL_PARAM);
        decl_void("color", p);
    }

    /* Plot */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("x", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("y", bb::Type::int_type, bb::DECL_PARAM);
        decl_void("plot", p);
    }

    /* Line */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("x1", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("y1", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("x2", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("y2", bb::Type::int_type, bb::DECL_PARAM);
        decl_void("line", p);
    }

    /* Rect / Oval */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("x", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("y", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("width", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("height", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("solid", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(1LL));
        decl_void("rect", new bb::DeclSeq(*p));
        decl_void("oval", new bb::DeclSeq(*p));
    }

    /* Text */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("x", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("y", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("string", bb::Type::string_type, bb::DECL_PARAM);
        p->insertDecl("centre_x", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        p->insertDecl("centre_y", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        decl_void("text", p);
    }

    /* Input: KeyDown, KeyHit, GetKey */
    decl_int("keydown", param_int("key"));
    decl_int("keyhit", param_int("key"));
    decl_int("getkey", no_params());
    decl_void("waitkey", no_params());
    decl_void("flushkeys", no_params());

    /* Input: Mouse */
    decl_int("mousex", no_params());
    decl_int("mousey", no_params());
    decl_int("mousedown", param_int("button"));
    decl_int("mousehit", param_int("button"));
    decl_int("mousez", no_params());

    /* GraphicsWidth / Height */
    decl_int("graphicswidth", no_params());
    decl_int("graphicsheight", no_params());
    decl_int("graphicsdepth", no_params());

    /* 2D canvas extras — signatures match runtime3d exactly */
    decl_int("backbuffer", no_params());
    decl_int("frontbuffer", no_params());
    decl_int("graphicsbuffer", no_params());
    decl_void("setbuffer", param_int("buffer"));
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("x", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("y", bb::Type::int_type, bb::DECL_PARAM);
        decl_void("origin", p);
    }
    decl_void("hidepointer", no_params());
    decl_void("showpointer", no_params());
    decl_int("countgfxmodes", no_params());
    decl_int("windowed3d", no_params());
    decl_int("readpixel", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("x", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("y", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("buffer", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL)); return q; }());
    decl_void("writepixel", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("x", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("y", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("argb", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("buffer", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL)); return q; }());
    decl_int("readpixelfast", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("x", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("y", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("buffer", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL)); return q; }());
    decl_void("writepixelfast", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("x", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("y", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("argb", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("buffer", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL)); return q; }());
    auto buffer_copy_params = [&](const char *dest_default) {
        auto *q = new bb::DeclSeq();
        for (const char *name : {"src_x", "src_y", "src_buffer", "dest_x", "dest_y"})
            q->insertDecl(name, bb::Type::int_type, bb::DECL_PARAM);
        q->insertDecl(dest_default, bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        return q;
    };
    decl_void("copypixel", buffer_copy_params("dest_buffer"));
    decl_void("copypixelfast", buffer_copy_params("dest_buffer"));
    {
        auto *q = new bb::DeclSeq();
        for (const char *name : {"source_x", "source_y", "width", "height", "dest_x", "dest_y", "src_buffer"})
            q->insertDecl(name, bb::Type::int_type, bb::DECL_PARAM);
        q->insertDecl("dest_buffer", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        decl_void("copyrect", q);
    }
    decl_int("loadbuffer", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("buffer", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("bmpfile", bb::Type::string_type, bb::DECL_PARAM); return q; }());
    decl_void("bufferdirty", param_int("buffer"));
    decl_void("getcolor", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("x", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("y", bb::Type::int_type, bb::DECL_PARAM); return q; }());
    decl_int("colorred", no_params());
    decl_int("colorgreen", no_params());
    decl_int("colorblue", no_params());

    /* Math: random — #Rnd#from#to=0, %Rand%from%to=1 */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("from", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("to", bb::Type::float_type, bb::DECL_PARAM, new bb::ConstType(0.0));
        decls->insertDecl("rnd", new bb::FuncType(bb::Type::float_type, p, false, false), bb::DECL_FUNC);
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("from", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("to", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(1LL));
        decls->insertDecl("rand", new bb::FuncType(bb::Type::int_type, p, false, false), bb::DECL_FUNC);
    }
    decl_void("seedrnd", param_int("seed"));
    decl_int("randseed", no_params());
    decl_void("delay", param_int("millisecs"));
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("frames", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(1LL));
        decl_void("vwait", p);
    }
    decl_int("waitmouse", no_params());
    decl_void("apptitle", param_str("title"));  // simplified: close_prompt optional

    /* Textures — %CreateTexture%width%height%flags=1%frames=1 */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("width", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("height", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("flags", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(1LL));
        p->insertDecl("frames", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(1LL));
        decls->insertDecl("createtexture", new bb::FuncType(bb::Type::int_type, p, false, false), bb::DECL_FUNC);
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("texture", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("u_scale", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("v_scale", bb::Type::float_type, bb::DECL_PARAM);
        decl_void("scaletexture", p);
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("texture", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("angle", bb::Type::float_type, bb::DECL_PARAM);
        decl_void("rotatetexture", p);
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("texture", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("u_offset", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("v_offset", bb::Type::float_type, bb::DECL_PARAM);
        decl_void("positiontexture", p);
    }

    /* Sprites & images */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("file", bb::Type::string_type, bb::DECL_PARAM);
        p->insertDecl("texture_flags", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(1LL));
        p->insertDecl("parent", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        decls->insertDecl("loadsprite", new bb::FuncType(bb::Type::int_type, p, false, false), bb::DECL_FUNC);
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("width", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("height", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("frames", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(1LL));
        decls->insertDecl("createimage", new bb::FuncType(bb::Type::int_type, p, false, false), bb::DECL_FUNC);
    }

    /* Mesh — FitMesh%mesh#x#y#z#width#height#depth%uniform=0 */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("mesh", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("x", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("y", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("z", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("width", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("height", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("depth", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("uniform", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        decl_void("fitmesh", p);
    }
    decl_int("createsurface", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("mesh", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("brush", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL)); return q; }());
        {
            auto *p = new bb::DeclSeq();
            p->insertDecl("mesh", bb::Type::int_type, bb::DECL_PARAM);
            p->insertDecl("red", bb::Type::float_type, bb::DECL_PARAM);
            p->insertDecl("green", bb::Type::float_type, bb::DECL_PARAM);
            p->insertDecl("blue", bb::Type::float_type, bb::DECL_PARAM);
            p->insertDecl("range", bb::Type::float_type, bb::DECL_PARAM, new bb::ConstType(0.0));
            p->insertDecl("x", bb::Type::float_type, bb::DECL_PARAM, new bb::ConstType(0.0));
            p->insertDecl("y", bb::Type::float_type, bb::DECL_PARAM, new bb::ConstType(0.0));
            p->insertDecl("z", bb::Type::float_type, bb::DECL_PARAM, new bb::ConstType(0.0));
            decl_void("lightmesh", p);
        }

    /* Collisions — Collisions%source_type%destination_type%method%response */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("source_type", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("destination_type", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("method", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("response", bb::Type::int_type, bb::DECL_PARAM);
        decl_void("collisions", p);
    }
    decl_void("clearcollisions", no_params());
    decl_int("countcollisions", param_int("entity"));
    decl_int("collisionentity", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("collision_index", bb::Type::int_type, bb::DECL_PARAM); return q; }());
    for (const char *name : {"collisionx", "collisiony", "collisionz", "collisionnx", "collisionny", "collisionnz"})
    {
        auto *q = new bb::DeclSeq();
        q->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM);
        q->insertDecl("collision_index", bb::Type::int_type, bb::DECL_PARAM);
        decls->insertDecl(name, new bb::FuncType(bb::Type::float_type, q, false, false), bb::DECL_FUNC);
    }
    decl_int("entitycollided", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("type", bb::Type::int_type, bb::DECL_PARAM); return q; }());

    {
        auto *q = new bb::DeclSeq();
        q->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM);
        q->insertDecl("vector_x", bb::Type::float_type, bb::DECL_PARAM);
        q->insertDecl("vector_y", bb::Type::float_type, bb::DECL_PARAM);
        q->insertDecl("vector_z", bb::Type::float_type, bb::DECL_PARAM);
        q->insertDecl("axis", bb::Type::int_type, bb::DECL_PARAM);
        q->insertDecl("rate", bb::Type::float_type, bb::DECL_PARAM, new bb::ConstType(1.0));
        decl_void("aligntovector", q);
    }
    for (const char *name : {"tformpoint", "tformvector", "tformnormal"})
    {
        auto *q = new bb::DeclSeq();
        for (const char *param : {"x", "y", "z"}) q->insertDecl(param, bb::Type::float_type, bb::DECL_PARAM);
        q->insertDecl("source_entity", bb::Type::int_type, bb::DECL_PARAM);
        q->insertDecl("dest_entity", bb::Type::int_type, bb::DECL_PARAM);
        decl_void(name, q);
    }
    for (const char *name : {"tformedx", "tformedy", "tformedz"})
        decls->insertDecl(name, new bb::FuncType(bb::Type::float_type, new bb::DeclSeq(), false, false), bb::DECL_FUNC);
    decl_void("texturefilter", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("match_text", bb::Type::string_type, bb::DECL_PARAM); q->insertDecl("texture_flags", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL)); return q; }());
    decl_void("cleartexturefilters", no_params());
    decls->insertDecl("entitydistance", new bb::FuncType(bb::Type::float_type, [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("source_entity", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("destination_entity", bb::Type::int_type, bb::DECL_PARAM); return q; }(), false, false), bb::DECL_FUNC);

    /* Viewport */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("x", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("y", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("width", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("height", bb::Type::int_type, bb::DECL_PARAM);
        decl_void("viewport", p);
    }

    /* Fonts */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("fontname", bb::Type::string_type, bb::DECL_PARAM);
        p->insertDecl("height", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(12LL));
        p->insertDecl("bold", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        p->insertDecl("italic", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        p->insertDecl("underline", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        decl_int("loadfont", p);
    }
    decl_void("setfont", param_int("font"));
    decl_void("freefont", param_int("font"));
    decl_int("fontwidth", no_params());
    decl_int("fontheight", no_params());
    decl_int("stringwidth", param_str("string"));
    decl_int("stringheight", param_str("string"));

    /* Images 2D */
    decl_int("loadimage", param_str("bmpfile"));
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("image", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("x", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("y", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("frame", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        decl_void("drawimage", p);
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("image", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("x", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("y", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("rect_x", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("rect_y", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("rect_width", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("rect_height", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("frame", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        decl_void("drawimagerect", p);
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("image", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("x", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("y", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("frame", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        decl_void("grabimage", p);
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("bmpfile", bb::Type::string_type, bb::DECL_PARAM);
        p->insertDecl("cellwidth", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("cellheight", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("first", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("count", bb::Type::int_type, bb::DECL_PARAM);
        decl_int("loadanimimage", p);
    }

    /* Gfx drivers/modes */
    decl_int("countgfxdrivers", no_params());
    decl_int("countgfxmodes3d", no_params());
    decl_int("gfxmodewidth", param_int("mode"));
    decl_int("gfxmodeheight", param_int("mode"));
    decl_int("gfxmodedepth", param_int("mode"));
    decl_int("gfxmode3d", param_int("mode"));
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("width", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("height", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("depth", bb::Type::int_type, bb::DECL_PARAM);
        decls->insertDecl("gfxmode3dexists", new bb::FuncType(bb::Type::int_type, p, false, false), bb::DECL_FUNC);
    }

    /* Buffer lock */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("buffer", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        decl_void("lockbuffer", p);
        decl_void("unlockbuffer", new bb::DeclSeq(*p));
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("texture", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("frame", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        decls->insertDecl("texturebuffer", new bb::FuncType(bb::Type::int_type, p, false, false), bb::DECL_FUNC);
    }

    /* Camera (already declared above, but cameraviewport was missing) */
    // CameraViewport is already declared above

    /* Entity extras */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("collision_type", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("recursive", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        decl_void("entitytype", p);
    }
    decl_int("getentitytype", param_int("entity"));
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("pick_geometry", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("obscurer", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(1LL));
        decl_void("entitypickmode", p);
    }
    decl_int("copyentity", [&]() { auto *p = new bb::DeclSeq(); p->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM); p->insertDecl("parent", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL)); return p; }());

    /* Light */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("light", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("red", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("green", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("blue", bb::Type::float_type, bb::DECL_PARAM);
        decl_void("lightcolor", p);
    }

    /* Mesh extras */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("mesh", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("x_scale", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("y_scale", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("z_scale", bb::Type::float_type, bb::DECL_PARAM);
        decl_void("scalemesh", p);
    }
    decl_void("flipmesh", param_int("mesh"));

    /* Sprites */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("sprite", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("x_scale", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("y_scale", bb::Type::float_type, bb::DECL_PARAM);
        decl_void("scalesprite", p);
    }
    decl_int("createsprite", param_int("parent", 0, true));
    decl_void("rotatesprite", [&]() { auto *p = new bb::DeclSeq(); p->insertDecl("sprite", bb::Type::int_type, bb::DECL_PARAM); p->insertDecl("angle", bb::Type::float_type, bb::DECL_PARAM); return p; }());
    decl_void("handlesprite", [&]() { auto *p = new bb::DeclSeq(); p->insertDecl("sprite", bb::Type::int_type, bb::DECL_PARAM); p->insertDecl("x_handle", bb::Type::float_type, bb::DECL_PARAM); p->insertDecl("y_handle", bb::Type::float_type, bb::DECL_PARAM); return p; }());
    decl_void("spriteviewmode", [&]() { auto *p = new bb::DeclSeq(); p->insertDecl("sprite", bb::Type::int_type, bb::DECL_PARAM); p->insertDecl("view_mode", bb::Type::int_type, bb::DECL_PARAM); return p; }());

    /* Input */
    decl_str("input", param_str("prompt"));
    decl_void("enabledirectinput", param_int("enable"));
    decl_int("directinputenabled", no_params());

    /* Mouse extras */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("x", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("y", bb::Type::int_type, bb::DECL_PARAM);
        decl_void("movemouse", p);
    }
    // MouseWait already declared above as waitmouse

    /* Sound (stubs) */
    decl_int("loadsound", param_str("filename"));
    decl_int("load3dsound", param_str("filename"));
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("parent", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("rolloff_factor", bb::Type::float_type, bb::DECL_PARAM, new bb::ConstType(1.0));
        p->insertDecl("doppler_scale", bb::Type::float_type, bb::DECL_PARAM, new bb::ConstType(1.0));
        p->insertDecl("distance_scale", bb::Type::float_type, bb::DECL_PARAM, new bb::ConstType(1.0));
        decl_int("createlistener", p);
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("sound", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("volume", bb::Type::float_type, bb::DECL_PARAM);
        decl_void("soundvolume", p);
    }
        decl_void("freesound", param_int("sound"));
        decl_void("loopsound", param_int("sound"));
        decl_void("soundpitch", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("sound", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("pitch", bb::Type::int_type, bb::DECL_PARAM); return q; }());
        decl_void("soundpan", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("sound", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("pan", bb::Type::float_type, bb::DECL_PARAM); return q; }());
        decl_int("playsound", param_int("sound"));
        decl_int("playmusic", param_str("midifile"));
        decl_int("playcdtrack", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("track", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("mode", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(1LL)); return q; }());
        for (const char *name : {"stopchannel", "pausechannel", "resumechannel"}) decl_void(name, param_int("channel"));
        decl_void("channelpitch", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("channel", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("pitch", bb::Type::int_type, bb::DECL_PARAM); return q; }());
        decl_void("channelvolume", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("channel", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("volume", bb::Type::float_type, bb::DECL_PARAM); return q; }());
        decl_void("channelpan", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("channel", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("pan", bb::Type::float_type, bb::DECL_PARAM); return q; }());
        decl_int("channelplaying", param_int("channel"));
        decl_int("emitsound", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("sound", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM); return q; }());

    /* Terrain/MD2 (stubs) */
    decl_int("loadterrain", param_str("heightmap_file"));
    decl_int("loadmd2", param_str("file"));
    decls->insertDecl("terrainheight", new bb::FuncType(bb::Type::float_type, [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("terrain", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("terrain_x", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("terrain_z", bb::Type::int_type, bb::DECL_PARAM); return q; }(), false, false), bb::DECL_FUNC);
    for (const char *name : {"terrainx", "terrainy", "terrainz"})
        decls->insertDecl(name, new bb::FuncType(bb::Type::float_type, [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("terrain", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("world_x", bb::Type::float_type, bb::DECL_PARAM); q->insertDecl("world_y", bb::Type::float_type, bb::DECL_PARAM); q->insertDecl("world_z", bb::Type::float_type, bb::DECL_PARAM); return q; }(), false, false), bb::DECL_FUNC);
    decl_void("modifyterrain", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("terrain", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("terrain_x", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("terrain_z", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("height", bb::Type::float_type, bb::DECL_PARAM); q->insertDecl("realtime", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL)); return q; }());
    decl_void("terraindetail", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("terrain", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("detail_level", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("morph", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL)); return q; }());
    decl_void("terrainshading", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("terrain", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("enable", bb::Type::int_type, bb::DECL_PARAM); return q; }());
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("md2", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("mode", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(1LL));
        p->insertDecl("speed", bb::Type::float_type, bb::DECL_PARAM, new bb::ConstType(1.0));
        p->insertDecl("first_frame", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        p->insertDecl("last_frame", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(9999LL));
        p->insertDecl("transition", bb::Type::float_type, bb::DECL_PARAM, new bb::ConstType(0.0));
        decl_void("animatemd2", p);
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("mode", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(1LL));
        p->insertDecl("speed", bb::Type::float_type, bb::DECL_PARAM, new bb::ConstType(1.0));
        p->insertDecl("sequence", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        p->insertDecl("transition", bb::Type::float_type, bb::DECL_PARAM, new bb::ConstType(0.0));
        decl_void("animate", p);
    }
    decl_int("animlength", param_int("entity"));
    decls->insertDecl("animtime", new bb::FuncType(bb::Type::float_type, param_int("entity"), false, false), bb::DECL_FUNC);

    /* Vertex */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("surface", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("x", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("y", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("z", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("u", bb::Type::float_type, bb::DECL_PARAM, new bb::ConstType(0.0));
        p->insertDecl("v", bb::Type::float_type, bb::DECL_PARAM, new bb::ConstType(0.0));
        p->insertDecl("w", bb::Type::float_type, bb::DECL_PARAM, new bb::ConstType(1.0));
        decls->insertDecl("addvertex", new bb::FuncType(bb::Type::int_type, p, false, false), bb::DECL_FUNC);
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("surface", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("v0", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("v1", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("v2", bb::Type::int_type, bb::DECL_PARAM);
        decls->insertDecl("addtriangle", new bb::FuncType(bb::Type::int_type, p, false, false), bb::DECL_FUNC);
    }
    decl_void("vertexcoords", [&]() { auto *p = new bb::DeclSeq(); p->insertDecl("surface", bb::Type::int_type, bb::DECL_PARAM); p->insertDecl("vertex", bb::Type::int_type, bb::DECL_PARAM); p->insertDecl("x", bb::Type::float_type, bb::DECL_PARAM); p->insertDecl("y", bb::Type::float_type, bb::DECL_PARAM); p->insertDecl("z", bb::Type::float_type, bb::DECL_PARAM); return p; }());
    decl_void("updatenormals", param_int("mesh"));

    /* Camera extras (fog, pick, proj) */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("camera", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("x", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("y", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("width", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("height", bb::Type::int_type, bb::DECL_PARAM);
        decl_void("cameraviewport", p);
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("camera", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("mode", bb::Type::int_type, bb::DECL_PARAM);
        decl_void("camerafogmode", p);
        decl_void("cameraprojmode", new bb::DeclSeq(*p));
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("camera", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("red", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("green", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("blue", bb::Type::float_type, bb::DECL_PARAM);
        decl_void("camerafogcolor", p);
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("camera", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("near", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("far", bb::Type::float_type, bb::DECL_PARAM);
        decl_void("camerafogrange", p);
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("camera", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("viewport_x", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("viewport_y", bb::Type::float_type, bb::DECL_PARAM);
        decls->insertDecl("camerapick", new bb::FuncType(bb::Type::int_type, p, false, false), bb::DECL_FUNC);
    }

    /* Entity extras (radius, autofade, parent, name, class, paint) */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("x_radius", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("y_radius", bb::Type::float_type, bb::DECL_PARAM, new bb::ConstType(0.0));
        decl_void("entityradius", p);
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("near", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("far", bb::Type::float_type, bb::DECL_PARAM);
        decl_void("entityautofade", p);
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("parent", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("global", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(1LL));
        decl_void("entityparent", p);
    }
    decl_void("nameentity", [&]() { auto *p = new bb::DeclSeq(); p->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM); p->insertDecl("name", bb::Type::string_type, bb::DECL_PARAM); return p; }());
    decl_str("entityname", param_int("entity"));
    decl_str("entityclass", param_int("entity"));
    decl_void("paintmesh", [&]() { auto *p = new bb::DeclSeq(); p->insertDecl("mesh", bb::Type::int_type, bb::DECL_PARAM); p->insertDecl("brush", bb::Type::int_type, bb::DECL_PARAM); return p; }());
    decl_void("paintentity", new bb::DeclSeq(*[&]() { auto *p = new bb::DeclSeq(); p->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM); p->insertDecl("brush", bb::Type::int_type, bb::DECL_PARAM); return p; }()));

    /* Light range (already declared lightcolor, lightrange missing) */
    decl_void("lightrange", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("light", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("range", bb::Type::float_type, bb::DECL_PARAM); return q; }());
    decl_void("lightconeangles", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("light", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("inner_angle", bb::Type::float_type, bb::DECL_PARAM); q->insertDecl("outer_angle", bb::Type::float_type, bb::DECL_PARAM); return q; }());

    /* Gfx driver name */
    decl_str("gfxdrivername", param_int("driver"));

    /* Images extras */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("image", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("red", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("green", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("blue", bb::Type::int_type, bb::DECL_PARAM);
        decl_void("maskimage", p);
    }
    decl_void("midhandle", param_int("image"));
    decl_void("automidhandle", param_int("enable"));
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("image", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("frame", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        decls->insertDecl("imagebuffer", new bb::FuncType(bb::Type::int_type, p, false, false), bb::DECL_FUNC);
    }
    decl_int("copyimage", param_int("image"));
    decl_void("resizeimage", [&]() { auto *p = new bb::DeclSeq(); p->insertDecl("image", bb::Type::int_type, bb::DECL_PARAM); p->insertDecl("width", bb::Type::int_type, bb::DECL_PARAM); p->insertDecl("height", bb::Type::int_type, bb::DECL_PARAM); return p; }());
    decl_void("freeimage", param_int("image"));
    decl_void("saveimage", [&]() { auto *p = new bb::DeclSeq(); p->insertDecl("image", bb::Type::int_type, bb::DECL_PARAM); p->insertDecl("bmpfile", bb::Type::string_type, bb::DECL_PARAM); p->insertDecl("frame", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL)); return p; }());
    decl_int("savebuffer", [&]() { auto *p = new bb::DeclSeq(); p->insertDecl("buffer", bb::Type::int_type, bb::DECL_PARAM); p->insertDecl("bmpfile", bb::Type::string_type, bb::DECL_PARAM); return p; }());
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("image", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("x", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("y", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("x_handle", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("y_handle", bb::Type::int_type, bb::DECL_PARAM);
        decl_void("handleimage", p);
    }

    /* Draw block */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("image", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("x", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("y", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("frame", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        decl_void("drawblock", p);
        decl_void("drawblockrect", new bb::DeclSeq(*[&]() { auto *q = new bb::DeclSeq(); for (auto &n : {"image","x","y","rect_x","rect_y","rect_width","rect_height"}) { } q->insertDecl("image", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("x", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("y", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("rect_x", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("rect_y", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("rect_width", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("rect_height", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("frame", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL)); return q; }()));
    }

    /* TileBlock — TileBlock%image%x=0%y=0%frame=0 */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("image", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("x", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        p->insertDecl("y", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        p->insertDecl("frame", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        decl_void("tileblock", p);
    }

    /* Pixel */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("x", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("y", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("argb", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("buffer", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        decl_void("writepixelfast", p);
    }

    /* WireFrame */
    decl_void("wireframe", param_int("enable"));

    /* ===== 3D entities ===== */
    decl_int("createcamera", param_int("parent", 0, true));
    decl_int("createlight", param_int("type", 1, true));
    decl_int("createcube", param_int("parent", 0, true));
    decl_int("createsphere", param_int("segments", 8, true));
    decl_int("createcylinder", param_int("segments", 8, true));
    decl_int("createcone", param_int("segments", 8, true));
    decl_int("createplane", param_int("segments", 1, true));
    decl_int("createpivot", param_int("parent", 0, true));
    decl_int("createmesh", param_int("parent", 0, true));
    decl_int("createbrush", no_params());
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("brush", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("red", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("green", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("blue", bb::Type::float_type, bb::DECL_PARAM);
        decl_void("brushcolor", p);
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("brush", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("blend", bb::Type::int_type, bb::DECL_PARAM);
        decl_void("brushblend", p);
    }
    decl_int("createmirror", param_int("parent", 0, true));
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("length", bb::Type::int_type, bb::DECL_PARAM);
        decl_int("addanimseq", p);
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("frame", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("pos_key", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(1LL));
        p->insertDecl("rot_key", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(1LL));
        p->insertDecl("scale_key", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(1LL));
        decl_void("setanimkey", p);
    }
    decl_void("freeentity", param_int("entity"));
    decl_void("resetentity", param_int("entity"));
    decl_void("hideentity", param_int("entity"));
    decl_void("showentity", param_int("entity"));

    /* Transforms: entity + 3 floats + optional global */
    auto float3_params = [&](const char *n1, const char *n2, const char *n3, const char *n4, bool has_global) {
        auto *p = new bb::DeclSeq();
        p->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl(n1, bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl(n2, bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl(n3, bb::Type::float_type, bb::DECL_PARAM);
        if (has_global) p->insertDecl(n4, bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        return p;
    };
    decl_void("positionentity", float3_params("x", "y", "z", "global", true));
    decl_void("rotateentity", float3_params("pitch", "yaw", "roll", "global", true));
    decl_void("scaleentity", float3_params("x_scale", "y_scale", "z_scale", "global", true));
    decl_void("moveentity", float3_params("x", "y", "z", "", false));
    decl_void("translateentity", float3_params("x", "y", "z", "global", true));
    decl_void("turnentity", float3_params("pitch", "yaw", "roll", "global", true));
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("target", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("roll", bb::Type::float_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        decl_void("pointentity", p);
    }

    /* Entity queries (float return, entity + optional global) */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("global", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL));
        auto *float_ret = new bb::FuncType(bb::Type::float_type, p, false, false);
        decls->insertDecl("entityx", new bb::FuncType(bb::Type::float_type, new bb::DeclSeq(*p), false, false), bb::DECL_FUNC);
        decls->insertDecl("entityy", new bb::FuncType(bb::Type::float_type, new bb::DeclSeq(*p), false, false), bb::DECL_FUNC);
        decls->insertDecl("entityz", new bb::FuncType(bb::Type::float_type, new bb::DeclSeq(*p), false, false), bb::DECL_FUNC);
        decls->insertDecl("entitypitch", new bb::FuncType(bb::Type::float_type, new bb::DeclSeq(*p), false, false), bb::DECL_FUNC);
        decls->insertDecl("entityyaw", new bb::FuncType(bb::Type::float_type, new bb::DeclSeq(*p), false, false), bb::DECL_FUNC);
        decls->insertDecl("entityroll", new bb::FuncType(bb::Type::float_type, new bb::DeclSeq(*p), false, false), bb::DECL_FUNC);
        (void)float_ret;
    }

    /* World */
    decl_void("updateworld", no_params());
    decl_void("renderworld", [&]() { auto *p = new bb::DeclSeq(); p->insertDecl("tween", bb::Type::float_type, bb::DECL_PARAM, new bb::ConstType(1.0)); return p; }());
    decl_void("captureworld", no_params());
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("red", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("green", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("blue", bb::Type::float_type, bb::DECL_PARAM);
        decl_void("ambientlight", p);
    }

    /* Camera */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("camera", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("near", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("far", bb::Type::float_type, bb::DECL_PARAM);
        decl_void("camerarange", p);
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("camera", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("red", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("green", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("blue", bb::Type::float_type, bb::DECL_PARAM);
        decl_void("cameraclscolor", p);
    }

    /* Entity properties */
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("red", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("green", bb::Type::float_type, bb::DECL_PARAM);
        p->insertDecl("blue", bb::Type::float_type, bb::DECL_PARAM);
        decl_void("entitycolor", p);
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("alpha", bb::Type::float_type, bb::DECL_PARAM);
        decl_void("entityalpha", p);
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("fx", bb::Type::int_type, bb::DECL_PARAM);
        decl_void("entityfx", p);
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("blend", bb::Type::int_type, bb::DECL_PARAM);
        decl_void("entityblend", p);
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("order", bb::Type::int_type, bb::DECL_PARAM);
        decl_void("entityorder", p);
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("shininess", bb::Type::float_type, bb::DECL_PARAM);
        decl_void("entityshininess", p);
    }
    {
        auto *p = new bb::DeclSeq();
        p->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM);
        p->insertDecl("texture", bb::Type::int_type, bb::DECL_PARAM);
        decl_void("entitytexture", p);
    }

    /* Textures */
    decl_int("loadtexture", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("file", bb::Type::string_type, bb::DECL_PARAM); q->insertDecl("flags", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(1LL)); return q; }());
    decl_void("freetexture", param_int("texture"));
    decl_int("loadanimtexture", [&]() { auto *q = new bb::DeclSeq(); for (const char *n : {"file", "flags", "width", "height", "first", "count"}) q->insertDecl(n, bb::Type::int_type, bb::DECL_PARAM); q->decls[0]->type = bb::Type::string_type; return q; }());
    decl_int("loadmaterial", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("file", bb::Type::string_type, bb::DECL_PARAM); for (const char *n : {"flags", "frame_width", "frame_height", "first_frame", "frame_count"}) q->insertDecl(n, bb::Type::int_type, bb::DECL_PARAM); return q; }());
    decl_void("freematerial", param_int("material"));
    decl_void("textureblend", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("texture", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("blend", bb::Type::int_type, bb::DECL_PARAM); return q; }());
    decl_void("texturecoords", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("texture", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("coords", bb::Type::int_type, bb::DECL_PARAM); return q; }());
    decl_void("voxelspritetexture", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("voxel", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("texture", bb::Type::int_type, bb::DECL_PARAM); for (const char *n : {"frame_width", "frame_height", "first_frame", "frame_count"}) q->insertDecl(n, bb::Type::int_type, bb::DECL_PARAM); return q; }());
    decl_void("voxelspritematerial", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("voxel", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("material", bb::Type::int_type, bb::DECL_PARAM); return q; }());
    decl_int("loadbrush", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("file", bb::Type::string_type, bb::DECL_PARAM); q->insertDecl("texture_flags", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(1LL)); q->insertDecl("u_scale", bb::Type::float_type, bb::DECL_PARAM, new bb::ConstType(1.0)); q->insertDecl("v_scale", bb::Type::float_type, bb::DECL_PARAM, new bb::ConstType(1.0)); return q; }());
    decl_void("freebrush", param_int("brush"));
    decl_void("brushalpha", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("brush", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("alpha", bb::Type::float_type, bb::DECL_PARAM); return q; }());
    decl_void("brushshininess", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("brush", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("shininess", bb::Type::float_type, bb::DECL_PARAM); return q; }());
    decl_void("brushfx", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("brush", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("fx", bb::Type::int_type, bb::DECL_PARAM); return q; }());
    decl_void("brushtexture", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("brush", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("texture", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("frame", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL)); q->insertDecl("index", bb::Type::int_type, bb::DECL_PARAM, new bb::ConstType(0LL)); return q; }());
    decl_void("paintentity", [&]() { auto *q = new bb::DeclSeq(); q->insertDecl("entity", bb::Type::int_type, bb::DECL_PARAM); q->insertDecl("brush", bb::Type::int_type, bb::DECL_PARAM); return q; }());

    /* Mesh loading */
    decl_int("loadmesh", param_str("file"));
    decl_int("loadanimmesh", param_str("file"));
}

/* After all declarations, map each builtin's Decl* to its native wrapper.
** This replaces the old linear string-scan dispatch in c_codegen.cpp
** with a single O(1) pointer lookup per call. */
static void register_builtin_wrappers(bb::BBRuntime *runtime, bb::CGen &gen)
{
    bb::DeclSeq *fd = runtime->env->funcDecls;
    auto reg = [&](const char *name, const char *wrapper, bool win = false) {
        bb::Decl *d = fd->findDecl(name);
        if (d) gen.register_builtin(d, wrapper, win);
    };
    /* Console */
    reg("print", "zen_print");
    reg("write", "zen_write");
    reg("end", "zen_end");
    reg("runtimeerror", "zen_runtime_error");
    reg("millisecs", "zen_millisecs");
    /* 2D graphics (window) */
    reg("graphics", "zen_graphics_open", true);
    reg("graphics3d", "zen_graphics3d_open", true);
    reg("flip", "zen_flip", true);
    reg("endgraphics", "zen_graphics_close", true);
    reg("cls", "zen_cls", true);
    reg("clscolor", "zen_cls_color", true);
    reg("color", "zen_color", true);
    reg("plot", "zen_plot", true);
    reg("line", "zen_line", true);
    reg("rect", "zen_rect", true);
    reg("oval", "zen_oval", true);
    reg("text", "zen_text", true);
    reg("waitkey", "zen_wait_key", true);
    /* Input */
    reg("keydown", "zen_key_down", true);
    reg("keyhit", "zen_key_hit", true);
    reg("getkey", "zen_get_key", true);
    reg("flushkeys", "zen_flush_keys", true);
    reg("mousex", "zen_mouse_x", true);
    reg("mousey", "zen_mouse_y", true);
    reg("mousedown", "zen_mouse_down", true);
    reg("mousehit", "zen_mouse_hit", true);
    reg("mousez", "zen_mouse_z", true);
    reg("graphicswidth", "zen_graphics_width", true);
    reg("graphicsheight", "zen_graphics_height", true);
    reg("graphicsdepth", "zen_graphics_depth", true);
    /* 3D entities */
    reg("createcamera", "zen_create_camera", true);
    reg("createlight", "zen_create_light", true);
    reg("createcube", "zen_create_cube", true);
    reg("createsphere", "zen_create_sphere", true);
    reg("createcylinder", "zen_create_cylinder", true);
    reg("createcone", "zen_create_cone", true);
    reg("createplane", "zen_create_plane", true);
    reg("createpivot", "zen_create_pivot", true);
    reg("createmesh", "zen_create_mesh", true);
    reg("createbrush", "zen_create_brush", true);
    reg("brushcolor", "zen_brush_color", true);
    reg("brushblend", "zen_brush_blend", true);
    reg("freeentity", "zen_free_entity", true);
    reg("resetentity", "zen_reset_entity", true);
    reg("hideentity", "zen_hide_entity", true);
    reg("showentity", "zen_show_entity", true);
    reg("loadmesh", "zen_load_mesh", true);
    reg("loadanimmesh", "zen_load_animmesh", true);
    /* Transforms */
    reg("positionentity", "zen_position_entity", true);
    reg("rotateentity", "zen_rotate_entity", true);
    reg("scaleentity", "zen_scale_entity", true);
    reg("moveentity", "zen_move_entity", true);
    reg("translateentity", "zen_translate_entity", true);
    reg("turnentity", "zen_turn_entity", true);
    reg("pointentity", "zen_point_entity", true);
    /* Entity queries */
    reg("entityx", "zen_entity_x", true);
    reg("entityy", "zen_entity_y", true);
    reg("entityz", "zen_entity_z", true);
    reg("entitypitch", "zen_entity_pitch", true);
    reg("entityyaw", "zen_entity_yaw", true);
    reg("entityroll", "zen_entity_roll", true);
    reg("aligntovector", "zen_align_to_vector", true);
    reg("tformpoint", "zen_tform_point", true);
    reg("tformvector", "zen_tform_vector", true);
    reg("tformnormal", "zen_tform_normal", true);
    reg("tformedx", "zen_tformed_x", true);
    reg("tformedy", "zen_tformed_y", true);
    reg("tformedz", "zen_tformed_z", true);
    reg("texturefilter", "zen_texture_filter", true);
    reg("cleartexturefilters", "zen_clear_texture_filters", true);
    reg("entitydistance", "zen_entity_distance", true);
    /* Entity properties */
    reg("entitycolor", "zen_entity_color", true);
    reg("entityalpha", "zen_entity_alpha", true);
    reg("entityfx", "zen_entity_fx", true);
    reg("entityblend", "zen_entity_blend", true);
    reg("entityorder", "zen_entity_order", true);
    reg("entityshininess", "zen_entity_shininess", true);
    reg("entitytexture", "zen_entity_texture", true);
    reg("freetexture", "zen_free_texture", true);
    /* Camera */
    reg("camerarange", "zen_camera_range", true);
    reg("cameraclscolor", "zen_camera_cls_color", true);
    /* Light */
    reg("ambientlight", "zen_ambient_light", true);
    /* World */
    reg("updateworld", "zen_update_world", true);
    reg("renderworld", "zen_render_world", true);
    reg("captureworld", "zen_capture_world", true);
    /* Textures */
    reg("loadtexture", "zen_load_texture", true);
    reg("loadanimtexture", "zen_load_anim_texture", true);
    reg("loadmaterial", "zen_load_material", true);
    reg("freematerial", "zen_free_material", true);
    reg("textureblend", "zen_texture_blend", true);
    reg("texturecoords", "zen_texture_coords", true);
    reg("voxelspritetexture", "zen_voxel_sprite_texture", true);
    reg("voxelspritematerial", "zen_voxel_sprite_material", true);
    reg("loadbrush", "zen_load_brush", true);
    reg("freebrush", "zen_free_brush", true);
    reg("brushalpha", "zen_brush_alpha", true);
    reg("brushshininess", "zen_brush_shininess", true);
    reg("brushfx", "zen_brush_fx", true);
    reg("brushtexture", "zen_brush_texture", true);
    reg("paintentity", "zen_paint_entity", true);
    /* String/math builtins */
    reg("len", "zen_len");
    reg("upper", "zen_upper");
    reg("lower", "zen_lower");
    reg("chr", "zen_chr");
    reg("asc", "zen_asc");
    reg("hex", "zen_hex");
    reg("bin", "zen_bin");
    reg("sin", "zen_sin");
    reg("cos", "zen_cos");
    reg("tan", "zen_tan");
    reg("sqr", "zen_sqr");
    reg("floor", "zen_floor");
    reg("ceil", "zen_ceil");
    reg("exp", "zen_exp");
    reg("log", "zen_log");
    reg("log10", "zen_log10");
    /* File I/O */
    reg("readfile", "zen_ReadFile");
    reg("writefile", "zen_WriteFile");
    reg("openfile", "zen_OpenFile");
    reg("closefile", "zen_CloseFile");
    reg("eof", "zen_Eof");
    reg("filepos", "zen_FilePos");
    reg("filesize", "zen_FileSize");
    reg("filetype", "zen_FileType");
    reg("readbyte", "zen_ReadByte");
    reg("readshort", "zen_ReadShort");
    reg("readint", "zen_ReadInt");
    reg("readfloat", "zen_ReadFloat");
    reg("readstring", "zen_ReadString");
    reg("readline", "zen_ReadLine");
    reg("writeline", "zen_WriteLine");
    reg("writestring", "zen_WriteString");
    reg("writebyte", "zen_WriteByte");
    reg("writeshort", "zen_WriteShort");
    reg("writeint", "zen_WriteInt");
    reg("writefloat", "zen_WriteFloat");
    reg("seekfile", "zen_SeekFile");
    reg("readdir", "zen_ReadDir");
    reg("nextfile", "zen_NextFile");
    reg("closedir", "zen_CloseDir");
    reg("currentdir", "zen_CurrentDir");
    reg("copyfile", "zen_CopyFile");
    reg("deletefile", "zen_DeleteFile");
    reg("createdir", "zen_CreateDir");
    reg("deletedir", "zen_DeleteDir");
    reg("changedir", "zen_ChangeDir");
    /* Banks */
    reg("createbank", "zen_CreateBank");
    reg("freebank", "zen_FreeBank");
    reg("banksize", "zen_BankSize");
    reg("resizebank", "zen_ResizeBank");
    reg("copybank", "zen_CopyBank");
    reg("peekbyte", "zen_PeekByte");
    reg("peekshort", "zen_PeekShort");
    reg("peekint", "zen_PeekInt");
    reg("peekfloat", "zen_PeekFloat");
    reg("pokebyte", "zen_PokeByte");
    reg("pokeshort", "zen_PokeShort");
    reg("pokeint", "zen_PokeInt");
    reg("pokefloat", "zen_PokeFloat");
    reg("readbytes", "zen_ReadBytes");
    reg("writebytes", "zen_WriteBytes");
    /* 2D canvas extras */
    reg("backbuffer", "zen_back_buffer", true);
    reg("frontbuffer", "zen_front_buffer", true);
    reg("graphicsbuffer", "zen_graphics_buffer", true);
    reg("setbuffer", "zen_set_buffer", true);
    reg("origin", "zen_origin", true);
    reg("hidepointer", "zen_hide_pointer", true);
    reg("showpointer", "zen_show_pointer", true);
    reg("countgfxmodes", "zen_count_gfx_modes", true);
    reg("countgfxmodes3d", "zen_count_gfx_modes", true);
    reg("gfxmodewidth", "zen_gfx_mode_width", true);
    reg("gfxmodeheight", "zen_gfx_mode_height", true);
    reg("gfxmodedepth", "zen_gfx_mode_depth", true);
    reg("windowed3d", "zen_windowed_3d", true);
    reg("readpixel", "zen_read_pixel", true);
    reg("writepixel", "zen_write_pixel", true);
    reg("readpixelfast", "zen_read_pixel_fast", true);
    reg("writepixelfast", "zen_write_pixel_fast", true);
    reg("copypixel", "zen_copy_pixel", true);
    reg("copypixelfast", "zen_copy_pixel_fast", true);
    reg("copyrect", "zen_copy_rect", true);
    reg("loadbuffer", "zen_load_buffer", true);
    reg("bufferdirty", "zen_buffer_dirty", true);
    reg("getcolor", "zen_get_color", true);
    reg("colorred", "zen_color_red", true);
    reg("colorgreen", "zen_color_green", true);
    reg("colorblue", "zen_color_blue", true);
    /* Math: random */
    reg("rnd", "zen_rnd");
    reg("rand", "zen_rand");
    reg("seedrnd", "zen_seed_rnd");
    reg("randseed", "zen_rand_seed");
    reg("delay", "zen_delay");
    reg("vwait", "zen_vwait", true);
    reg("waitmouse", "zen_wait_mouse", true);
    reg("apptitle", "zen_app_title");
    /* Textures */
    reg("createtexture", "zen_create_texture", true);
    reg("scaletexture", "zen_scale_texture", true);
    reg("rotatetexture", "zen_rotate_texture", true);
    reg("positiontexture", "zen_position_texture", true);
    /* Sprites & images */
    reg("loadsprite", "zen_load_sprite", true);
    reg("createimage", "zen_create_image", true);
    /* Mesh */
    reg("fitmesh", "zen_fit_mesh", true);
    reg("lightmesh", "zen_light_mesh", true);
    reg("createsurface", "zen_create_surface", true);
    /* Collisions */
    reg("collisions", "zen_collisions", true);
    reg("clearcollisions", "zen_clear_collisions", true);
    reg("countcollisions", "zen_count_collisions", true);
    reg("collisionentity", "zen_collision_entity", true);
    reg("collisionx", "zen_collision_x", true);
    reg("collisiony", "zen_collision_y", true);
    reg("collisionz", "zen_collision_z", true);
    reg("collisionnx", "zen_collision_nx", true);
    reg("collisionny", "zen_collision_ny", true);
    reg("collisionnz", "zen_collision_nz", true);
    reg("entitycollided", "zen_entity_collided", true);
    /* Viewport */
    reg("viewport", "zen_viewport", true);
    /* Fonts */
    reg("loadfont", "zen_load_font", true);
    reg("setfont", "zen_set_font", true);
    reg("freefont", "zen_free_font", true);
    reg("fontwidth", "zen_font_width", true);
    reg("fontheight", "zen_font_height", true);
    reg("stringwidth", "zen_string_width", true);
    reg("stringheight", "zen_string_height", true);
    /* Images 2D */
    reg("loadimage", "zen_load_image", true);
    reg("drawimage", "zen_draw_image", true);
    reg("drawimagerect", "zen_draw_image_rect", true);
    reg("grabimage", "zen_grab_image", true);
    reg("loadanimimage", "zen_load_anim_image", true);
    /* Gfx drivers/modes */
    reg("countgfxdrivers", "zen_count_gfx_drivers", true);
    reg("gfxmode3dexists", "zen_gfx_mode3d_exists", true);
    reg("gfxmode3d", "zen_gfx_mode_3d", true);
    /* Buffer lock */
    reg("lockbuffer", "zen_lock_buffer", true);
    reg("unlockbuffer", "zen_unlock_buffer", true);
    reg("texturebuffer", "zen_texture_buffer", true);
    /* Entity extras */
    reg("entitytype", "zen_entity_type", true);
    reg("getentitytype", "zen_get_entity_type", true);
    reg("entitypickmode", "zen_entity_pick_mode", true);
    reg("copyentity", "zen_copy_entity", true);
    /* Light */
    reg("lightcolor", "zen_light_color", true);
    reg("lightrange", "zen_light_range", true);
    reg("lightconeangles", "zen_light_cone_angles", true);
    /* Mesh extras */
    reg("scalemesh", "zen_scale_mesh", true);
    reg("flipmesh", "zen_flip_mesh", true);
    /* Sprites */
    reg("scalesprite", "zen_scale_sprite", true);
    reg("createsprite", "zen_create_sprite", true);
    reg("rotatesprite", "zen_rotate_sprite", true);
    reg("handlesprite", "zen_handle_sprite", true);
    reg("spriteviewmode", "zen_sprite_view_mode", true);
    /* Input */
    reg("input", "zen_input");
    reg("enabledirectinput", "zen_enable_direct_input", true);
    reg("directinputenabled", "zen_direct_input_enabled", true);
    /* Mouse extras */
    reg("movemouse", "zen_move_mouse", true);
    /* Sound (stubs) */
    reg("loadsound", "zen_load_sound");
    reg("load3dsound", "zen_load_3d_sound");
    reg("soundvolume", "zen_sound_volume");
    reg("createlistener", "zen_create_listener");
    reg("freesound", "zen_free_sound");
    reg("loopsound", "zen_loop_sound");
    reg("soundpitch", "zen_sound_pitch");
    reg("soundpan", "zen_sound_pan");
    reg("playsound", "zen_play_sound");
    reg("playmusic", "zen_play_music");
    reg("playcdtrack", "zen_play_cd_track");
    reg("stopchannel", "zen_stop_channel");
    reg("pausechannel", "zen_pause_channel");
    reg("resumechannel", "zen_resume_channel");
    reg("channelpitch", "zen_channel_pitch");
    reg("channelvolume", "zen_channel_volume");
    reg("channelpan", "zen_channel_pan");
    reg("channelplaying", "zen_channel_playing");
    reg("emitsound", "zen_emit_sound");
    /* Terrain/MD2 */
    reg("loadterrain", "zen_load_terrain", true);
    reg("terrainheight", "zen_terrain_height", true);
    reg("terrainx", "zen_terrain_x", true);
    reg("terrainy", "zen_terrain_y", true);
    reg("terrainz", "zen_terrain_z", true);
    reg("modifyterrain", "zen_modify_terrain", true);
    reg("terraindetail", "zen_terrain_detail", true);
    reg("terrainshading", "zen_terrain_shading", true);
    reg("loadmd2", "zen_load_md2", true);
    reg("animatemd2", "zen_animate_md2", true);
    reg("animate", "zen_animate", true);
    reg("animlength", "zen_anim_length", true);
    reg("animtime", "zen_anim_time", true);
    reg("createmirror", "zen_create_mirror", true);
    reg("addanimseq", "zen_add_anim_seq", true);
    reg("setanimkey", "zen_set_anim_key", true);
    /* Vertex */
    reg("addvertex", "zen_add_vertex", true);
    reg("addtriangle", "zen_add_triangle", true);
    reg("vertexcoords", "zen_vertex_coords", true);
    reg("updatenormals", "zen_update_normals", true);
    /* Camera extras */
    reg("cameraviewport", "zen_camera_viewport", true);
    reg("camerafogmode", "zen_camera_fog_mode", true);
    reg("camerafogcolor", "zen_camera_fog_color", true);
    reg("camerafogrange", "zen_camera_fog_range", true);
    reg("cameraprojmode", "zen_camera_proj_mode", true);
    reg("camerapick", "zen_camera_pick", true);
    /* Entity extras */
    reg("entityradius", "zen_entity_radius", true);
    reg("entityautofade", "zen_entity_autofade", true);
    reg("entityparent", "zen_entity_parent", true);
    reg("nameentity", "zen_name_entity", true);
    reg("entityname", "zen_entity_name", true);
    reg("entityclass", "zen_entity_class", true);
    reg("paintmesh", "zen_paint_mesh", true);
    reg("paintentity", "zen_paint_entity", true);
    /* Gfx driver name */
    reg("gfxdrivername", "zen_gfx_driver_name", true);
    /* Images extras */
    reg("maskimage", "zen_mask_image", true);
    reg("midhandle", "zen_mid_handle", true);
    reg("automidhandle", "zen_auto_mid_handle", true);
    reg("imagebuffer", "zen_image_buffer", true);
    reg("copyimage", "zen_copy_image", true);
    reg("resizeimage", "zen_resize_image", true);
    reg("freeimage", "zen_free_image", true);
    reg("saveimage", "zen_save_image", true);
    reg("savebuffer", "zen_save_buffer", true);
    reg("handleimage", "zen_handle_image", true);
    reg("drawblock", "zen_draw_block", true);
    reg("drawblockrect", "zen_draw_block_rect", true);
    reg("tileblock", "zen_tile_block", true);
    reg("writepixelfast", "zen_write_pixel_fast", true);
    reg("wireframe", "zen_wireframe", true);
}

static bool read_source(const char *path, std::string &source)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        std::fprintf(stderr, "zencc: cannot open '%s'\n", path);
        return false;
    }
    std::ostringstream contents;
    contents << in.rdbuf();
    source = contents.str();
    return true;
}

int main(int argc, char **argv)
{
    if (argc != 4 || std::string(argv[1]) != "--emit-c")
    {
        std::fprintf(stderr, "usage: zencc --emit-c input.bb output.cpp\n");
        return 1;
    }

    std::string source;
    if (!read_source(argv[2], source))
        return 1;

    std::ofstream output(argv[3], std::ios::binary);
    if (!output)
    {
        std::fprintf(stderr, "zencc: cannot write '%s'\n", argv[3]);
        return 1;
    }

    VM vm;
    install_runtime(&vm);
    bb::BBRuntime *runtime = bb::bb_runtime_for(&vm);
    install_window_declarations(runtime);
    std::istringstream input(source);
    bb::Toker toker(input);
    bb::Parser parser(toker, &vm);
    bb::ProgNode *program = nullptr;

    try
    {
        program = parser.parse(argv[2]);
        program->semant(runtime->env);
        bb::CGen generator(output, argv[2]);
        register_builtin_wrappers(runtime, generator);
        generator.compile(program);
    }
    catch (bb::Ex &error)
    {
        std::fprintf(stderr, "zencc: %s\n", error.ex.c_str());
        delete program;
        bb::bb_runtime_destroy(runtime);
        return 1;
    }

    delete program;
    bb::bb_runtime_destroy(runtime);
    return 0;
}