#ifndef ENGINE_TEXTURE_H
#define ENGINE_TEXTURE_H

#include "engine/Pixmap.h"
#include "gpu/GPU.h"
#include <ct/vector.hpp>
#include <string>

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
        static Texture *load(const std::string &file, int flags);
        static Texture *loadAnim(const std::string &file, int flags, int w, int h, int first, int count);

        void addRef() { ++mRefCount; }
        static void release(Texture *t) { if (t && !--t->mRefCount) delete t; }

        void setScale(float u, float v) { mSx = u; mSy = v; mMatUsed = true; }
        void setPosition(float u, float v) { mTx = u; mTy = v; mMatUsed = true; }
        void setRotation(float angle) { mRot = angle; mMatUsed = true; }
        void setBlend(int blend) { mBlend = blend; }
        void setFlags(int flags) { mFlags = flags; }

        int getBlend() const { return mBlend; }
        int getFlags() const { return mFlags; }
        bool isTransparent() const { return mTransparent; }
        std::string getName() const { return mName; }
        int frameCount() const { return (int)mFrames.size(); }

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
        std::string mName;
        ct::Vector<zengl::Pixmap> mFrames;
        ct::Vector<gpu::TextureHandle> mGpuFrames;
        gpu::Device *mGpuOwner = nullptr;
        int mBlend = 0, mFlags = 0;
        bool mTransparent = false;

        float mSx = 1, mSy = 1, mTx = 0, mTy = 0, mRot = 0;
        bool mMatUsed = false;

        ~Texture();
    };

    // Blitz3D resolves a mesh loader's relative texture paths against the
    // directory of the file being loaded (CachedTexture::setPath); this is
    // that directory, checked first, then the working directory.
    void setTexturePath(const std::string &dir);
    std::string resolveTexturePath(const std::string &file);
}

#endif
