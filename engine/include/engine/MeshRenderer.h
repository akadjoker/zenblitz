#ifndef ENGINE_MESHRENDERER_H
#define ENGINE_MESHRENDERER_H

#include "engine/Surface.h"
#include "engine/Brush.h"
#include "engine/Light.h"
#include "engine/Matrix4.h"
#include <ct/vector.hpp>

namespace engine
{
    static constexpr int kMaxRenderLights = 4;

    class MeshRenderer
    {
    public:
        bool init(gpu::Device &dev);
        void shutdown();

        void setViewProjection(const Matrix4 &vp) { mViewProjection = vp; }
        void setLights(const ct::Vector<Light *> &lights);
        void setAmbient(const Vector &ambient) { mAmbient = ambient; }

        enum { FogNone = 0, FogLinear = 1, FogExp = 2, FogExp2 = 3 };
        void setFog(int mode, const Vector &color, float nearRange, float farRange)
        {
            mFogMode = mode; mFogColor = color; mFogNear = nearRange; mFogFar = farRange;
        }

        // upload the per-draw uniforms - call with no render pass open
        void prepare(const Surface *surface, const Brush &brush, const Matrix4 &model);
        // issue the draw call - call inside a render pass
        void draw(gpu::Device &dev, const Surface *surface, const Brush &brush);

    private:
        struct PipelineKey
        {
            int blend = 0;
            bool doubleSided = false;
            bool hasTexture = false;
            std::uint32_t key() const
            {
                return (std::uint32_t)blend | (doubleSided ? 0x100u : 0u) | (hasTexture ? 0x200u : 0u);
            }
        };
        struct CachedPipeline
        {
            std::uint32_t key = 0;
            gpu::PipelineHandle pipeline;
        };

        gpu::Device *mGpu = nullptr;
        gpu::BufferHandle mUniformBuffer;
        gpu::SamplerHandle mSampler;
        gpu::TextureHandle mWhiteTexture;
        ct::Vector<CachedPipeline> mPipelines;

        Matrix4 mViewProjection = Matrix4::identity();
        Vector mAmbient{0.2f, 0.2f, 0.2f};

        int mFogMode = FogNone;
        Vector mFogColor;
        float mFogNear = 1.0f, mFogFar = 1000.0f;

        struct Uniforms
        {
            float mvp[16];
            float model[16];
            float color[4];
            float ambient[4];
            float fogColor[4];
            std::int32_t flags;
            std::int32_t lightCount;
            std::int32_t fogMode;
            float fogNear;
            float fogFar;
            std::int32_t pad[3];
            float lightPosType[kMaxRenderLights][4];
            float lightColorRange[kMaxRenderLights][4];
            float lightDir[kMaxRenderLights][4];
        };
        Uniforms mUniforms;

        gpu::PipelineHandle pipelineFor(const PipelineKey &pk);
    };
}

#endif
