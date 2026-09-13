#ifndef ENGINE_TEXTURE_H
#define ENGINE_TEXTURE_H

#include "engine/Pixmap.h"
#include "engine/Image.h"
#include "gpu/GPU.h"
#include <ct/vector.hpp>
#include <ct/string.hpp>

namespace engine
{
    enum
    {
        TexRgb = 0x0001,
        TexAlpha = 0x0002,
        TexMask = 0x0004,
        TexMipmap = 0x0008,
        TexClampU = 0x0010,
        TexClampV = 0x0020,
    };

    // A loaded/created texture and its Blitz-side transform (scale/pos/
    // rotation, used to build BrushTexture's uv matrix). Refcounted (like
    // the original's Texture::Rep) so BrushTexture can copy it cheaply;
    // no CachedTexture path-based dedup - LoadTexture reloads the file.
    class Texture
    {
    public:
        static Texture *load(const ct::String &file, int flags);
        // Same as load(), but decodes an already-in-memory image buffer
        // instead of reading a file - a glTF/GLB image embedded in a
        // buffer_view or a base64 data: URI has no file path to give
        // load(). `name` is only a label (used the same way load()'s
        // `file` argument is, e.g. for BrushTexture bookkeeping); it is
        // never opened.
        static Texture *loadFromMemory(const ct::String &name, const unsigned char *data, unsigned size,
                                       int flags);
        static Texture *loadAnim(const ct::String &file, int flags, int w, int h, int first, int count);
        // CreateTexture: a blank, drawable texture - count opaque black
        // frames (or transparent, with TexAlpha) a script paints into via
        // SetBuffer TextureBuffer(tex) before ever using it in 3D, same as
        // the original's Texture::Rep holding one BBCanvas per frame.
        static Texture *create(int w, int h, int flags, int count);

        void addRef() { ++mRefCount; }
        static void release(Texture *t) { if (t && !--t->mRefCount) delete t; }

        // Only non-null for a create()d texture: the CPU-side drawable
        // backing SetBuffer TextureBuffer(tex,frame) paints into. A
        // load()ed texture has no canvas - its pixels are never meant to
        // be redrawn - and this stays null for it, exactly as the
        // original's getCanvas() returned null for a texture with no
        // color depth to draw with.
        ImageFrame *canvas(int frame);

        void setScale(float u, float v) { mSx = u; mSy = v; mMatUsed = true; }
        void setPosition(float u, float v) { mTx = u; mTy = v; mMatUsed = true; }
        void setRotation(float angle) { mRot = angle; mMatUsed = true; }
        void setBlend(int blend) { mBlend = blend; }
        void setFlags(int flags) { mFlags = flags; }

        int getBlend() const { return mBlend; }
        int getFlags() const { return mFlags; }
        bool isTransparent() const { return mTransparent; }
        ct::String getName() const { return mName; }
        int frameCount() const { return mImage.frameCount(); }
        int width() const { return mImage.width(); }
        int height() const { return mImage.height(); }

        bool ensureUploaded(gpu::Device &dev, int frame);
        gpu::TextureHandle handle(int frame) const;

        // uv matrix (row-major 3x2, matching BrushTexture's u/vScale+Pos+
        // rotation fields) - only meaningful if scale/pos/rotation were set
        bool hasMatrix() const { return mMatUsed; }
        void getMatrix(float &sx, float &sy, float &tx, float &ty, float &rot) const
        {
            sx = mSx; sy = mSy; tx = mTx; ty = mTy; rot = mRot;
        }

    private:
        int mRefCount = 1;
        ct::String mName;
        // Backs both a load()ed texture's static pixels and a create()d
        // one's paintable canvas() - Image already carries the CPU pixels,
        // GPU handle and dirty flag per frame this needs either way.
        Image mImage;
        int mBlend = 2, mFlags = 0;
        bool mTransparent = false;

        float mSx = 1, mSy = 1, mTx = 0, mTy = 0, mRot = 0;
        bool mMatUsed = false;

        ~Texture();
    };

    // Blitz3D resolves a mesh loader's relative texture paths against the
    // directory of the file being loaded (CachedTexture::setPath); this is
    // that directory, checked first, then the working directory.
    void setTexturePath(const ct::String &dir);
    ct::String resolveTexturePath(const ct::String &file);
}

#endif
