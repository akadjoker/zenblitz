/*
** bb_cmds_image.cpp — Marco 2: LoadImage, LoadAnimImage, DrawImage,
** MaskImage, FreeImage, ImageWidth/Height, HandleImage/MidHandle.
**
** Fiel to bbruntime/bbgraphics.cpp: LoadImage is one frame; LoadAnimImage
** cuts a cellwidth x cellheight grid out of the sheet, `first` frames per
** row (fpr = sheet width / cellwidth), matching the original's own maths.
** Handle defaults to (0,0) — Blitz3D only auto-mid-handles when the
** (rarely used) AutoMidHandle flag is set, not by default, so this
** doesn't either.
*/
#include "runtime.h"
#include "vm.h"
#include "object.h"
#include "ct/hashmap.hpp"
#include "engine/Platform.h"
#include "engine/Image.h"
#include <utility>

namespace bb3d { extern engine::Platform *platform_for(zen::VM *vm); }

namespace
{
    inline long long arg_int(zen::Value v)
    {
        return zen::is_int(v) ? v.as.integer : zen::is_float(v) ? (long long)v.as.number : 0;
    }
    inline const char *arg_cstr(zen::Value v)
    {
        return zen::is_string(v) ? zen::as_cstring(v) : "";
    }
}

using namespace zen;

namespace bb3d
{
    static ct::HashMap<long long, engine::Image *> g_images;
    static long long g_next_image = 0;

    static engine::Image *image_of(long long h)
    {
        engine::Image **found = g_images.find(h);
        return found ? *found : nullptr;
    }

    static long long store_image(engine::Image *img)
    {
        long long h = ++g_next_image;
        g_images.put(h, img);
        return h;
    }

    static int c_LoadImage(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        zengl::Pixmap p;
        if (!p.load(arg_cstr(args[0]))) { args[0] = val_int(0); return 1; }
        engine::Image *img = new engine::Image();
        img->addFrame(std::move(p));
        args[0] = val_int(store_image(img));
        (void)vm;
        return 1;
    }

    static int c_LoadAnimImage(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        const char *path = arg_cstr(args[0]);
        int cellW = (int)arg_int(args[1]), cellH = (int)arg_int(args[2]);
        int first = (int)arg_int(args[3]), count = (int)arg_int(args[4]);

        if (count < 1 || first < 0 || cellW <= 0 || cellH <= 0) { args[0] = val_int(0); return 1; }

        zengl::Pixmap sheet;
        if (!sheet.load(path)) { args[0] = val_int(0); return 1; }

        /* frames per row, per sheet — the same division the original does */
        int fpr = sheet.width / cellW;
        int fpp = fpr > 0 ? (sheet.height / cellH) * fpr : 0;
        if (fpr <= 0 || first + count > fpp) { args[0] = val_int(0); return 1; }

        engine::Image *img = new engine::Image();
        for (int k = 0; k < count; ++k)
        {
            int idx = first + k;
            int srcX = (idx % fpr) * cellW, srcY = (idx / fpr) * cellH;
            zengl::IntRect rect{srcX, srcY, cellW, cellH};
            zengl::Pixmap frame(sheet, rect);
            img->addFrame(std::move(frame));
        }
        args[0] = val_int(store_image(img));
        (void)vm;
        return 1;
    }

    static int c_FreeImage(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        long long h = arg_int(args[0]);
        engine::Image *img = image_of(h);
        if (img) { delete img; g_images.erase(h); }
        (void)vm;
        return 0;
    }

    static int c_DrawImage(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        engine::Image *img = image_of(arg_int(args[0]));
        if (!img) return 0;
        float x = (float)arg_int(args[1]), y = (float)arg_int(args[2]);
        int frame = (int)arg_int(args[3]);
        engine::Platform *p = platform_for(vm);
        if (!img->ensureUploaded(p->device(), frame)) return 0;
        const engine::ImageFrame &f = img->frame(frame);
        p->batch().drawTexture(f.texture, x - (float)img->handleX(), y - (float)img->handleY(),
                               (float)f.pixels.width, (float)f.pixels.height);
        return 0;
    }

    static int c_MaskImage(VM *vm, Value *args, int nargs)
    {
        (void)nargs;
        engine::Image *img = image_of(arg_int(args[0]));
        if (!img) return 0;
        unsigned char r = (unsigned char)arg_int(args[1]);
        unsigned char g = (unsigned char)arg_int(args[2]);
        unsigned char b = (unsigned char)arg_int(args[3]);
        for (int i = 0; i < img->frameCount(); ++i)
        {
            engine::ImageFrame &f = img->frame(i);
            f.pixels.set_color_key(zengl::Color(r, g, b, 255));
            f.dirty = true;
        }
        (void)vm;
        return 0;
    }

    static int c_ImageWidth(VM *vm, Value *args, int nargs)
    {
        (void)nargs; (void)vm;
        engine::Image *img = image_of(arg_int(args[0]));
        args[0] = val_int(img ? img->width() : 0);
        return 1;
    }

    static int c_ImageHeight(VM *vm, Value *args, int nargs)
    {
        (void)nargs; (void)vm;
        engine::Image *img = image_of(arg_int(args[0]));
        args[0] = val_int(img ? img->height() : 0);
        return 1;
    }

    static int c_HandleImage(VM *vm, Value *args, int nargs)
    {
        (void)nargs; (void)vm;
        engine::Image *img = image_of(arg_int(args[0]));
        if (img) img->setHandle((int)arg_int(args[1]), (int)arg_int(args[2]));
        return 0;
    }

    static int c_MidHandle(VM *vm, Value *args, int nargs)
    {
        (void)nargs; (void)vm;
        engine::Image *img = image_of(arg_int(args[0]));
        if (img) img->setMidHandle();
        return 0;
    }

    /* ================= table ================= */
    extern const zen::CommandDecl bb3d_cmds_image[] = {
        {"%LoadImage$file", c_LoadImage},
        {"%LoadAnimImage$file%cellwidth%cellheight%first%count", c_LoadAnimImage},
        {"FreeImage%image", c_FreeImage},
        {"DrawImage%image%x%y%frame=0", c_DrawImage},
        {"MaskImage%image%red%green%blue", c_MaskImage},
        {"%ImageWidth%image", c_ImageWidth},
        {"%ImageHeight%image", c_ImageHeight},
        {"HandleImage%image%x%y", c_HandleImage},
        {"MidHandle%image", c_MidHandle},
    };
    extern const int bb3d_cmds_image_count = (int)(sizeof(bb3d_cmds_image) / sizeof(bb3d_cmds_image[0]));
}
