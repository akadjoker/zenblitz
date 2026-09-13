#ifndef ENGINE_MESHRENDERER_H
#define ENGINE_MESHRENDERER_H

#include "engine/Surface.h"
#include "engine/Brush.h"
#include "engine/Light.h"
#include "engine/Matrix4.h"
#include "engine/ShaderDialect.h"
#include <ct/vector.hpp>
#include <cstddef>

namespace engine
{
    static constexpr int kMaxRenderLights = 4;
    // 128 mat4 = 8 KB, half the 16 KB uniform block minimum WebGL2/GLES3
    // guarantee. Models with more bones fall back to CPU skinning.
    static constexpr int kMaxGpuBones = 128;

    class MeshRenderer
    {
    public:
        bool init(gpu::Device &dev, ShaderDialect dialect);
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
        // Stages one model's world-space bone matrices; returns the slot to
        // pass to prepare()/draw(), or -1 if count is 0 or above
        // kMaxGpuBones (caller then skins on the CPU instead).
        int stageBones(const Matrix4 *mats, int count);
        // Stages one draw call's uniforms (reads the current
        // viewProjection/ambient/fog/lights) - call with no render pass
        // open, any number of times between beginFrame() and flushUniforms().
        // morph is the frame A->B blend for LayoutMd2Morph geometry (0 for
        // everything else).
        void prepare(const Brush &brush, const Matrix4 &model, float morph = 0.0f);
        // Uploads every staged uniform and bone block in one buffer each -
        // call once, with no render pass open, after all prepare() calls.
        void flushUniforms(gpu::Device &dev);
        // Issues the Nth prepared draw call - call inside a render pass,
        // index matching prepare() call order since the last beginFrame().
        // boneSlot from stageBones() selects the skinned pipeline; -1 draws
        // the geometry as is. The geometry's layout selects the vertex
        // format/streams (Surface, or MD2 two-frame morph).
        void draw(gpu::Device &dev, int index, const GpuGeometry &geom, const Brush &brush, int boneSlot);

        // gxScene::setFlippedTris: cull the front face instead of the back
        // for the duration of a mirror pass, because reflecting the camera
        // reverses every triangle's winding. World::render sets it around
        // the reflected pass and clears it for the normal one.
        void setFlippedTris(bool flipped) { mFlippedTris = flipped; }

        // Camera viewport clear (CameraClsColor/CameraClsMode): stages a
        // draw of a full-viewport quad at far depth in the given colour.
        // No render pass may be open; occupies a prepare() index like any
        // other draw call. drawClear issues it inside the pass, with the
        // viewport already set - the viewport bounds the clear. Colour
        // and/or depth writes are enabled per the flags.
        void prepareClear(const Vector &color);
        void drawClear(gpu::Device &dev, int index, bool clearColor, bool clearDepth);

        // Per-frame counters, reset by beginFrame(): what actually reached
        // the GPU after the bind dedup in bindAndDraw(). The switches are
        // the interesting part - a scene whose pipeline/texture switches
        // approach its draw calls is sorting badly, since every switch
        // costs a full state re-issue in the GL backend.
        struct Stats
        {
            std::uint32_t drawCalls = 0;
            std::uint32_t triangles = 0;
            std::uint32_t pipelineSwitches = 0;
            std::uint32_t textureSwitches = 0;
            std::uint32_t vertexBufferSwitches = 0;
            std::uint32_t indexBufferSwitches = 0;
            std::uint32_t uniformBinds = 0;
        };
        const Stats &stats() const { return mStats; }

    private:
        struct PipelineKey
        {
            int blend = 0;
            bool doubleSided = false;
            bool hasTexture = false;
            bool skinned = false;
            bool clear = false, clearColor = false, clearDepth = false;
            // Mirror pass: reflecting the camera through the mirror plane
            // is a negative-determinant transform, so every triangle comes
            // out wound the other way and back-face culling would throw
            // away exactly the faces that should be visible. Culling the
            // front face instead restores it - the original did the same
            // through gxScene::setFlippedTris (D3DCULL_CW in place of
            // D3DCULL_CCW). Part of the key so the mirror pass gets its
            // own pipeline rather than mutating the shared one.
            bool flippedTris = false;
            GpuGeometry::Layout layout = GpuGeometry::LayoutSurface;
            std::uint32_t key() const
            {
                return (std::uint32_t)blend | (doubleSided ? 0x100u : 0u) | (hasTexture ? 0x200u : 0u) |
                       (skinned ? 0x400u : 0u) | (clear ? 0x800u : 0u) | (clearColor ? 0x1000u : 0u) |
                       (clearDepth ? 0x2000u : 0u) | (flippedTris ? 0x4000u : 0u) |
                       ((std::uint32_t)layout << 16);
            }
        };
        struct CachedPipeline
        {
            std::uint32_t key = 0;
            gpu::PipelineHandle pipeline;
            std::int32_t uniformsSlot = 0;
            std::int32_t bonesSlot = -1;
            std::int32_t texture0Slot = 0;
            std::int32_t texture1Slot = 1;
        };

        bool mFlippedTris = false;
        gpu::Device *mGpu = nullptr;
        ShaderDialect mDialect = ShaderDialect::GLSL330;
        gpu::BufferHandle mUniformBuffer;
        std::uint64_t mUniformBufferCapacity = 0;
        std::uint32_t mUniformStride = 0;
        gpu::BufferHandle mBoneBuffer;
        std::uint64_t mBoneBufferCapacity = 0;
        std::uint32_t mBoneStride = 0;
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
            float spot[kMaxRenderLights][4] = {};
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
            float morph;
            std::int32_t pad[2];
            alignas(16) float texMatrix[2][4];
            alignas(16) float texParams[2][4];
            // std140 starts a vec4 array on the next 16-byte boundary
            // (232 -> 240), but C++ only needs 4-byte alignment for a
            // float array and would pack it at 232 - alignas(16) makes
            // the struct follow the same rule. Getting this wrong shifts
            // every light value by 8 bytes and the lighting silently
            // reads garbage (which is exactly what happened: an
            // `int u_pad2[2]` used to sit here, 8 bytes in C++ against
            // std140's 32, hiding the same class of mismatch).
            alignas(16) float lightPosType[kMaxRenderLights][4];
            alignas(16) float lightColorRange[kMaxRenderLights][4];
            alignas(16) float lightDir[kMaxRenderLights][4];
            alignas(16) float lightSpot[kMaxRenderLights][4];
        };
        // The shader reads this block as std140; if the C++ layout ever
        // drifts from it again the lighting goes silently wrong rather
        // than failing loudly, so pin the offsets that matter here.
        static_assert(offsetof(Uniforms, lightPosType) == 272, "std140 mismatch: lightPosType");
        static_assert(offsetof(Uniforms, lightColorRange) == 336, "std140 mismatch: lightColorRange");
        static_assert(offsetof(Uniforms, lightDir) == 400, "std140 mismatch: lightDir");
        static_assert(offsetof(Uniforms, lightSpot) == 464, "std140 mismatch: lightSpot");
        static_assert(sizeof(Uniforms) == 528, "std140 mismatch: Uniforms size");
        // Staged blocks laid out at their GPU stride (not tightly packed)
        // so flushUniforms uploads each buffer in one updateBuffer and
        // draw() binds any entry by offset.
        ct::Vector<unsigned char> mStaged;
        std::uint32_t mStagedCount = 0;
        ct::Vector<unsigned char> mBoneStaged;
        std::uint32_t mBoneStagedCount = 0;

        // last handles/offsets bound by draw() this frame (0 = nothing
        // bound yet); vertex streams track their offset too since MD2
        // frames select by offset into one buffer
        struct BoundState
        {
            std::uint64_t pipeline = 0, texture0 = 0, texture1 = 0, indexBuffer = 0;
            std::uint64_t vb = 0, vbOffset = 0, vb2 = 0, vb2Offset = 0, uvb = 0;
            std::int32_t texture0Slot = -1, texture1Slot = -1;
        };
        BoundState mBound;
        Stats mStats;

        // full-screen NDC quad at far depth, drawn under the camera's
        // viewport for CameraClsColor/CameraClsMode
        Surface mClearQuad;

        const CachedPipeline *pipelineFor(const PipelineKey &pk);
        void bindAndDraw(gpu::Device &dev, const CachedPipeline *cp, int index, const GpuGeometry &geom,
                         gpu::TextureHandle tex0, gpu::TextureHandle tex1, int boneSlot);
        bool ensureBuffer(gpu::Device &dev, gpu::BufferHandle &buffer, std::uint64_t &capacity,
                          std::uint64_t needed, const char *debugName);
    };
}

#endif
