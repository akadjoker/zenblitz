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

        void setViewProjection(const Matrix4 &vp) { mPending.viewProjection = vp; }
        void setLights(const ct::Vector<Light *> &lights);
        void setAmbient(const Vector &ambient) { mPending.ambient = ambient; }

        enum { FogNone = 0, FogLinear = 1, FogExp = 2, FogExp2 = 3 };
        void setFog(int mode, const Vector &color, float nearRange, float farRange)
        {
            mPending.fogMode = mode; mPending.fogColor = color; mPending.fogNear = nearRange; mPending.fogFar = farRange;
        }

        // Call once per frame, with no render pass open, before any prepare().
        void beginFrame();
        // Stages one draw call's uniforms (reads the current
        // viewProjection/ambient/fog/lights) - call with no render pass
        // open, any number of times between beginFrame() and flushUniforms().
        void prepare(const Surface *surface, const Brush &brush, const Matrix4 &model);
        // Uploads every staged draw call's uniforms in one buffer - call
        // once, with no render pass open, after all prepare() calls.
        void flushUniforms(gpu::Device &dev);
        // Issues the Nth prepared draw call - call inside a render pass,
        // index matching prepare() call order since the last beginFrame().
        void draw(gpu::Device &dev, int index, const Surface *surface, const Brush &brush);

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
        std::uint64_t mUniformBufferCapacity = 0;
        std::uint32_t mUniformStride = 0;
        gpu::SamplerHandle mSampler;
        gpu::TextureHandle mWhiteTexture;
        ct::Vector<CachedPipeline> mPipelines;

        struct PendingState
        {
            Matrix4 viewProjection = Matrix4::identity();
            Vector ambient{0.2f, 0.2f, 0.2f};
            int fogMode = FogNone;
            Vector fogColor;
            float fogNear = 1.0f, fogFar = 1000.0f;
        };
        PendingState mPending;
        ct::Vector<Light *> mLights;

        // Pre-encoded once per setLights() call (once per frame), not
        // re-derived (with a per-light std::cos) on every prepare().
        struct LightBlock
        {
            std::int32_t count = 0;
            float posType[kMaxRenderLights][4] = {};
            float colorRange[kMaxRenderLights][4] = {};
            float dir[kMaxRenderLights][4] = {};
        };
        LightBlock mLightBlock;

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
        // Staged uniforms, laid out at mUniformStride apart (not tightly
        // packed as sizeof(Uniforms)) so flushUniforms can upload the
        // whole thing in one updateBuffer and draw() can bind each
        // entry's slot directly by offset.
        ct::Vector<unsigned char> mStaged;
        std::uint32_t mStagedCount = 0;

        // last handles bound by draw() this frame (0 = nothing bound yet)
        struct BoundState
        {
            std::uint64_t pipeline = 0, texture = 0, vertexBuffer = 0, indexBuffer = 0;
        };
        BoundState mBound;

        gpu::PipelineHandle pipelineFor(const PipelineKey &pk);
        bool ensureUniformBuffer(gpu::Device &dev, std::uint32_t count);
    };
}

#endif
