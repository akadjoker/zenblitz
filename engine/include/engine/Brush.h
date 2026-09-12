#ifndef ENGINE_BRUSH_H
#define ENGINE_BRUSH_H

#include "engine/Geom.h"
#include "gpu/GPU.h"

namespace engine
{
    using blitz::Vector;

    enum
    {
        FxFullbright = 0x0001,
        FxVertexColor = 0x0002,
        FxFlatShaded = 0x0004,
        FxNoFog = 0x0008,
        FxDoubleSided = 0x0010,
        FxVertexAlpha = 0x0020,
        FxAlphaTest = 0x2000,
    };

    enum
    {
        BlendReplace = 0,
        BlendAlpha = 1,
        BlendMultiply = 2,
        BlendAdd = 3,
    };

    static constexpr int kMaxBrushTextures = 8;

    struct BrushTexture
    {
        gpu::TextureHandle handle;
        float uScale = 1.0f, vScale = 1.0f;
        float uPos = 0.0f, vPos = 0.0f;
        float rotation = 0.0f;
        int blend = BlendReplace;
        int flags = 0;
        bool transparent = false;
    };

    class Brush
    {
    public:
        void setColor(const Vector &color) { mColor = color; }
        void setAlpha(float alpha) { mAlpha = alpha; }
        void setShininess(float shininess) { mShininess = shininess; }
        void setBlend(int blend) { mBlend = blend; }
        void setFX(int fx) { mFx = fx; }
        void setTexture(int index, const BrushTexture &t)
        {
            if (index < 0 || index >= kMaxBrushTextures) return;
            mTextures[index] = t;
            mMaxTex = 0;
            for (int k = 0; k < kMaxBrushTextures; ++k)
                if (mTextures[k].handle.valid()) mMaxTex = k + 1;
        }

        const Vector &getColor() const { return mColor; }
        float getAlpha() const { return mAlpha; }
        float getShininess() const { return mShininess; }
        int getFX() const { return mFx; }
        const BrushTexture &getTexture(int index) const { return mTextures[index]; }
        int getTextureCount() const { return mMaxTex; }

        int getBlend() const
        {
            if (mBlend) return mBlend;
            if (mMaxTex == 1 && mTextures[0].transparent) return BlendAlpha;
            if ((mFx & FxVertexAlpha) || mAlpha < 1) return BlendAlpha;
            return BlendReplace;
        }

    private:
        Vector mColor{1, 1, 1};
        float mAlpha = 1.0f;
        float mShininess = 0.0f;
        int mBlend = 0;
        int mFx = 0;
        int mMaxTex = 0;
        BrushTexture mTextures[kMaxBrushTextures];
    };
}

#endif
