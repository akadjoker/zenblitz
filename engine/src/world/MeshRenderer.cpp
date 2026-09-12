#include "engine/MeshRenderer.h"
#include <cstring>
#include <cmath>
#include <string>

namespace engine
{
    // Shader bodies without a #version line: shaderPrefix() prepends the
    // dialect's version/precision and an optional SKINNED define.
    static const char kUniformBlock[] =
        "layout(std140) uniform Uniforms {\n"
        "  mat4 u_mvp;\n"
        "  mat4 u_model;\n"
        "  vec4 u_color;\n"
        "  vec4 u_ambient;\n"
        "  vec4 u_fogColor;\n"
        "  int u_flags;\n"
        "  int u_lightCount;\n"
        "  int u_fogMode;\n"
        "  float u_fogNear;\n"
        "  float u_fogFar;\n"
        "  float u_morph;\n"
        "  vec4 u_lightPosType[4];\n"
        "  vec4 u_lightColorRange[4];\n"
        "  vec4 u_lightDir[4];\n"
        "};\n";

    static const char kVertexBody[] =
        "layout(location=0) in vec3 a_pos;\n"
        "layout(location=1) in vec3 a_normal;\n"
        "#ifndef MORPH\n"
        "layout(location=2) in vec4 a_color;\n"
        "#endif\n"
        "layout(location=3) in vec2 a_uv;\n"
        "#ifdef SKINNED\n"
        "layout(location=4) in vec4 a_bones;\n"
        "layout(location=5) in vec4 a_weights;\n"
        "layout(std140) uniform Bones { mat4 u_bones[128]; };\n"
        "#endif\n"
        "#ifdef MORPH\n"
        "layout(location=6) in vec3 a_posB;\n"
        "layout(location=7) in vec3 a_normalB;\n"
        "#endif\n"
        "out vec3 v_worldPos;\n"
        "out vec3 v_normal;\n"
        "out vec4 v_color;\n"
        "out vec2 v_uv;\n"
        "void main(){\n"
        "#ifdef MORPH\n"
        "  vec4 lp = vec4(mix(a_pos, a_posB, u_morph),1.0);\n"
        "  vec3 ln = mix(a_normal, a_normalB, u_morph);\n"
        "  vec4 a_color = vec4(1.0);\n"
        "#else\n"
        "  vec4 lp = vec4(a_pos,1.0);\n"
        "  vec3 ln = a_normal;\n"
        "#endif\n"
        "#ifdef SKINNED\n"
        "  int b0 = int(a_bones.x*255.0+0.5);\n"
        "  if (b0 == 255) {\n"
        "    lp = u_bones[0]*lp;\n"
        "    ln = mat3(u_bones[0])*ln;\n"
        "  } else {\n"
        "    vec4 sp = vec4(0.0); vec3 sn = vec3(0.0);\n"
        "    for (int i = 0; i < 4; ++i) {\n"
        "      int b = int(a_bones[i]*255.0+0.5);\n"
        "      if (b == 255) break;\n"
        "      sp += a_weights[i]*(u_bones[b]*lp);\n"
        "      sn += a_weights[i]*(mat3(u_bones[b])*ln);\n"
        "    }\n"
        "    lp = sp; ln = sn;\n"
        "  }\n"
        "#endif\n"
        "  vec4 wp = u_model * lp;\n"
        "  v_worldPos = wp.xyz;\n"
        "  v_normal = mat3(u_model) * ln;\n"
        "  v_color = a_color;\n"
        "  v_uv = a_uv;\n"
        "  gl_Position = u_mvp * lp;\n"
        "}\n";

    static const char kFragmentBody[] =
        "in vec3 v_worldPos;\n"
        "in vec3 v_normal;\n"
        "in vec4 v_color;\n"
        "in vec2 v_uv;\n"
        "uniform sampler2D u_texture;\n"
        "out vec4 o_color;\n"
        "const int FX_FULLBRIGHT=0x0001;\n"
        "const int FX_VERTEXCOLOR=0x0002;\n"
        "const int FX_FLATSHADED=0x0004;\n"
        "const int FX_NOFOG=0x0008;\n"
        "const int FX_DOUBLESIDED=0x0010;\n"
        "const int FX_VERTEXALPHA=0x0020;\n"
        "const int FX_ALPHATEST=0x2000;\n"
        "void main(){\n"
        "  vec4 base = u_color;\n"
        "  if ((u_flags & FX_VERTEXCOLOR) != 0) base *= v_color;\n"
        "  vec4 tex = texture(u_texture, v_uv);\n"
        "  base *= tex;\n"
        "  if ((u_flags & FX_ALPHATEST) != 0 && base.a < 0.5) discard;\n"
        "  vec3 n;\n"
        "  if ((u_flags & FX_FLATSHADED) != 0) {\n"
        "    n = normalize(cross(dFdy(v_worldPos), dFdx(v_worldPos)));\n"
        "  } else {\n"
        "    n = normalize(v_normal);\n"
        "  }\n"
        "  if ((u_flags & FX_DOUBLESIDED) != 0 && !gl_FrontFacing) n = -n;\n"
        "  vec3 lit = u_ambient.rgb;\n"
        "  if ((u_flags & FX_FULLBRIGHT) == 0) {\n"
        "    for (int i = 0; i < u_lightCount; ++i) {\n"
        "      float type = u_lightPosType[i].w;\n"
        "      vec3 lightColor = u_lightColorRange[i].rgb;\n"
        "      float range = u_lightColorRange[i].a;\n"
        "      vec3 toLight; float atten = 1.0;\n"
        "      if (type < 0.5) {\n"
        "        toLight = -u_lightDir[i].xyz;\n"
        "      } else {\n"
        "        vec3 delta = u_lightPosType[i].xyz - v_worldPos;\n"
        "        float dist = length(delta);\n"
        "        toLight = dist > 0.0001 ? delta / dist : vec3(0,1,0);\n"
        "        atten = clamp(1.0 - dist / max(range,0.0001), 0.0, 1.0);\n"
        "        if (type > 1.5) {\n"
        "          float inner = u_lightDir[i].w;\n"
        "          float cosAngle = dot(normalize(-toLight), normalize(u_lightDir[i].xyz));\n"
        "          float spot = clamp((cosAngle - inner) / max(1.0 - inner, 0.0001), 0.0, 1.0);\n"
        "          atten *= spot;\n"
        "        }\n"
        "      }\n"
        "      float ndotl = max(dot(n, toLight), 0.0);\n"
        "      lit += lightColor * ndotl * atten;\n"
        "    }\n"
        "  } else {\n"
        "    lit = vec3(1.0);\n"
        "  }\n"
        "  float alpha = base.a;\n"
        "  if ((u_flags & FX_VERTEXALPHA) != 0) alpha *= v_color.a;\n"
        "  vec3 litColor = base.rgb * lit;\n"
        "  if (u_fogMode != 0 && (u_flags & FX_NOFOG) == 0) {\n"
        "    float dist = length(v_worldPos);\n"
        "    float f = 1.0;\n"
        "    if (u_fogMode == 1) {\n"
        "      f = clamp((u_fogFar - dist) / max(u_fogFar - u_fogNear, 0.0001), 0.0, 1.0);\n"
        "    } else if (u_fogMode == 2) {\n"
        "      f = clamp(exp(-dist / max(u_fogFar,0.0001)), 0.0, 1.0);\n"
        "    } else if (u_fogMode == 3) {\n"
        "      float d = dist / max(u_fogFar,0.0001);\n"
        "      f = clamp(exp(-d*d), 0.0, 1.0);\n"
        "    }\n"
        "    litColor = mix(u_fogColor.rgb, litColor, f);\n"
        "  }\n"
        "  o_color = vec4(litColor, alpha);\n"
        "}\n";

    static std::string shaderSource(kx::ShaderDialect dialect, bool fragment, bool skinned, bool morph)
    {
        std::string s;
        if (dialect == kx::ShaderDialect::GLSLES300)
        {
            s += "#version 300 es\n";
            s += "precision highp float;\n";
            s += "precision highp int;\n";
        }
        else
        {
            s += "#version 330 core\n";
        }
        if (skinned) s += "#define SKINNED 1\n";
        if (morph) s += "#define MORPH 1\n";
        s += kUniformBlock;
        s += fragment ? kFragmentBody : kVertexBody;
        return s;
    }

    bool MeshRenderer::init(gpu::Device &dev, kx::ShaderDialect dialect)
    {
        mGpu = &dev;
        mDialect = dialect;

        std::uint32_t align = dev.capabilities().uniformBufferOffsetAlignment;
        if (align == 0) align = 1;
        mUniformStride = ((std::uint32_t)sizeof(Uniforms) + align - 1) / align * align;
        mBoneStride = ((std::uint32_t)(kMaxGpuBones * sizeof(Matrix4)) + align - 1) / align * align;

        gpu::SamplerDesc samplerDesc;
        samplerDesc.minFilter = gpu::Filter::Linear;
        samplerDesc.magFilter = gpu::Filter::Linear;
        samplerDesc.mipFilter = gpu::Filter::Linear;
        samplerDesc.addressU = gpu::AddressMode::Repeat;
        samplerDesc.addressV = gpu::AddressMode::Repeat;
        samplerDesc.debugName = "meshrenderer.sampler";
        mSampler = dev.createSampler(samplerDesc);

        const unsigned char whitePixel[4] = {255, 255, 255, 255};
        gpu::TextureDesc texDesc;
        texDesc.width = 1;
        texDesc.height = 1;
        texDesc.format = gpu::Format::RGBA8;
        texDesc.initialData = {whitePixel, sizeof(whitePixel)};
        texDesc.debugName = "meshrenderer.white";
        mWhiteTexture = dev.createTexture(texDesc);

        // clear quad: clip-space corners at z=+1 (NDC far), drawn with an
        // identity MVP; cull is off on its pipeline so winding is moot
        mClearQuad.clear(true, true);
        static const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (int i = 0; i < 4; ++i)
        {
            Surface::Vertex v;
            v.coords = Vector(corners[i][0], corners[i][1], 1.0f);
            v.normal = Vector(0, 0, -1);
            mClearQuad.addVertex(v);
        }
        Surface::Triangle t1{{0, 1, 2}}, t2{{0, 2, 3}};
        mClearQuad.addTriangle(t1);
        mClearQuad.addTriangle(t2);
        bool quadOk = mClearQuad.ensureGpu(dev);

        return mSampler.valid() && mWhiteTexture.valid() && quadOk;
    }

    bool MeshRenderer::ensureBuffer(gpu::Device &dev, gpu::BufferHandle &buffer, std::uint64_t &capacity,
                                    std::uint64_t needed, const char *debugName)
    {
        if (needed <= capacity && buffer.valid()) return true;

        if (buffer.valid()) dev.destroy(buffer);

        std::uint64_t cap = capacity ? capacity : needed;
        while (cap < needed) cap *= 2;

        gpu::BufferDesc desc;
        desc.size = cap;
        desc.usage = gpu::BufferUsageUniform;
        desc.debugName = debugName;
        buffer = dev.createBuffer(desc);
        capacity = buffer.valid() ? cap : 0;
        return buffer.valid();
    }

    void MeshRenderer::shutdown()
    {
        if (!mGpu) return;
        for (size_t k = 0; k < mPipelines.size(); ++k) mGpu->destroy(mPipelines[k].pipeline);
        mPipelines.clear();
        if (mUniformBuffer.valid()) mGpu->destroy(mUniformBuffer);
        if (mBoneBuffer.valid()) mGpu->destroy(mBoneBuffer);
        if (mSampler.valid()) mGpu->destroy(mSampler);
        if (mWhiteTexture.valid()) mGpu->destroy(mWhiteTexture);
        mClearQuad.freeGpu(*mGpu);
        mUniformBuffer = gpu::BufferHandle();
        mBoneBuffer = gpu::BufferHandle();
        mUniformBufferCapacity = mBoneBufferCapacity = 0;
        mGpu = nullptr;
    }

    void MeshRenderer::setLights(const ct::Vector<Light *> &lights)
    {
        mLights = lights;

        int count = (int)mLights.size();
        if (count > kMaxRenderLights) count = kMaxRenderLights;
        mLightBlock.count = count;
        for (int i = 0; i < count; ++i)
        {
            Light *l = mLights[i];
            const Vector &pos = l->getRenderPosition();
            const Vector &dir = l->getRenderDirection();
            mLightBlock.posType[i][0] = pos.x;
            mLightBlock.posType[i][1] = pos.y;
            mLightBlock.posType[i][2] = pos.z;
            mLightBlock.posType[i][3] = (float)(l->getType() - 1);
            mLightBlock.colorRange[i][0] = l->getColor().x;
            mLightBlock.colorRange[i][1] = l->getColor().y;
            mLightBlock.colorRange[i][2] = l->getColor().z;
            mLightBlock.colorRange[i][3] = l->getRange();
            float innerCos = std::cos(l->getInnerAngle() * blitz::PI / 180.0f);
            mLightBlock.dir[i][0] = dir.x;
            mLightBlock.dir[i][1] = dir.y;
            mLightBlock.dir[i][2] = dir.z;
            mLightBlock.dir[i][3] = innerCos;
        }
    }

    void MeshRenderer::beginFrame()
    {
        mStaged.clear();
        mStagedCount = 0;
        mBoneStaged.clear();
        mBoneStagedCount = 0;
        // the Canvas Batch draws between our prepare() and draw() and
        // binds its own pipeline/buffers, so nothing can be assumed bound
        mBound = BoundState();
    }

    int MeshRenderer::stageBones(const Matrix4 *mats, int count)
    {
        if (!mats || count <= 0 || count > kMaxGpuBones) return -1;
        std::uint64_t offset = (std::uint64_t)mBoneStagedCount * mBoneStride;
        if (mBoneStaged.size() < offset + mBoneStride) mBoneStaged.resize(offset + mBoneStride);
        std::memcpy(&mBoneStaged[offset], mats, (size_t)count * sizeof(Matrix4));
        return (int)mBoneStagedCount++;
    }

    const MeshRenderer::CachedPipeline *MeshRenderer::pipelineFor(const PipelineKey &pk)
    {
        std::uint32_t key = pk.key();
        for (size_t i = 0; i < mPipelines.size(); ++i)
            if (mPipelines[i].key == key) return &mPipelines[i];

        const bool morph = pk.layout == GpuGeometry::LayoutMd2Morph;
        std::string vs = shaderSource(mDialect, false, pk.skinned, morph);
        std::string fs = shaderSource(mDialect, true, pk.skinned, morph);

        gpu::PipelineDesc desc;
        desc.vertex.source = {vs.data(), vs.size()};
        desc.fragment.source = {fs.data(), fs.size()};
        desc.vertex.debugName = morph ? "mesh.morph.vs" : pk.skinned ? "mesh.skinned.vs" : "mesh.vs";
        desc.fragment.debugName = "mesh.fs";
        desc.debugName = morph ? "mesh.morph" : pk.skinned ? "mesh.skinned" : "mesh";
        if (morph)
        {
            // MD2: stream 0 = frame A, stream 1 = frame B (same Md2Vert
            // layout, selected by buffer offset), stream 2 = uv
            desc.vertexBufferCount = 3;
            desc.vertexBuffers[0].stride = sizeof(Md2Vert);
            desc.vertexBuffers[0].attributeCount = 2;
            desc.vertexBuffers[0].attributes[0] = {gpu::VertexFormat::Float32x3, (std::uint32_t)offsetof(Md2Vert, x), 0};
            desc.vertexBuffers[0].attributes[1] = {gpu::VertexFormat::Float32x3, (std::uint32_t)offsetof(Md2Vert, nx), 1};
            desc.vertexBuffers[1].stride = sizeof(Md2Vert);
            desc.vertexBuffers[1].attributeCount = 2;
            desc.vertexBuffers[1].attributes[0] = {gpu::VertexFormat::Float32x3, (std::uint32_t)offsetof(Md2Vert, x), 6};
            desc.vertexBuffers[1].attributes[1] = {gpu::VertexFormat::Float32x3, (std::uint32_t)offsetof(Md2Vert, nx), 7};
            desc.vertexBuffers[2].stride = sizeof(Md2Uv);
            desc.vertexBuffers[2].attributeCount = 1;
            desc.vertexBuffers[2].attributes[0] = {gpu::VertexFormat::Float32x2, (std::uint32_t)offsetof(Md2Uv, u), 3};
        }
        else
        {
            desc.vertexBufferCount = 1;
            desc.vertexBuffers[0].stride = sizeof(Surface::Vertex);
            desc.vertexBuffers[0].attributeCount = pk.skinned ? 6 : 4;
            desc.vertexBuffers[0].attributes[0] = {gpu::VertexFormat::Float32x3, (std::uint32_t)offsetof(Surface::Vertex, coords), 0};
            desc.vertexBuffers[0].attributes[1] = {gpu::VertexFormat::Float32x3, (std::uint32_t)offsetof(Surface::Vertex, normal), 1};
            desc.vertexBuffers[0].attributes[2] = {gpu::VertexFormat::Unorm8x4, (std::uint32_t)offsetof(Surface::Vertex, color), 2};
            desc.vertexBuffers[0].attributes[3] = {gpu::VertexFormat::Float32x2, (std::uint32_t)offsetof(Surface::Vertex, texCoords), 3};
            if (pk.skinned)
            {
                // bone indices as Unorm8x4: the shader decodes int(x*255+0.5)
                desc.vertexBuffers[0].attributes[4] = {gpu::VertexFormat::Unorm8x4, (std::uint32_t)offsetof(Surface::Vertex, boneBones), 4};
                desc.vertexBuffers[0].attributes[5] = {gpu::VertexFormat::Float32x4, (std::uint32_t)offsetof(Surface::Vertex, boneWeights), 5};
            }
        }
        desc.topology = gpu::Topology::Triangles;
        desc.colorTargetCount = 1;
        desc.colorTargets[0].format = gpu::Format::RGBA8;
        desc.depthStencil.depthTestEnabled = true;
        desc.depthStencil.depthWriteEnabled = (pk.blend == BlendReplace);
        desc.raster.cullMode = pk.doubleSided ? gpu::CullMode::None : gpu::CullMode::Back;
        // Blitz3D/DirectX winds front faces clockwise; keep ported geometry
        // and loaders untouched instead of flipping winding everywhere.
        desc.raster.frontFace = gpu::FrontFace::Clockwise;

        gpu::ColorTargetState &ct0 = desc.colorTargets[0];
        switch (pk.blend)
        {
        case BlendAlpha:
            ct0.blendEnabled = true;
            ct0.colorBlend = {gpu::BlendFactor::SourceAlpha, gpu::BlendFactor::OneMinusSourceAlpha, gpu::BlendOperation::Add};
            ct0.alphaBlend = ct0.colorBlend;
            break;
        case BlendAdd:
            ct0.blendEnabled = true;
            ct0.colorBlend = {gpu::BlendFactor::One, gpu::BlendFactor::One, gpu::BlendOperation::Add};
            ct0.alphaBlend = ct0.colorBlend;
            break;
        case BlendMultiply:
            ct0.blendEnabled = true;
            ct0.colorBlend = {gpu::BlendFactor::DestinationColor, gpu::BlendFactor::Zero, gpu::BlendOperation::Add};
            ct0.alphaBlend = ct0.colorBlend;
            break;
        default:
            ct0.blendEnabled = false;
            break;
        }

        if (pk.clear)
        {
            // viewport clear: always passes depth, writes far depth and/or
            // colour per CameraClsMode, ignores winding and blending
            desc.depthStencil.depthCompare = gpu::CompareOp::Always;
            desc.depthStencil.depthWriteEnabled = pk.clearDepth;
            ct0.writeMask = pk.clearColor ? gpu::ColorWriteAll : 0;
            ct0.blendEnabled = false;
            desc.raster.cullMode = gpu::CullMode::None;
        }

        CachedPipeline cached;
        cached.key = key;
        cached.pipeline = mGpu->createPipeline(desc);
        if (cached.pipeline.valid())
        {
            // the GL backend assigns block bindings by active-block index,
            // which the linker orders - look both up by name
            std::int32_t u = mGpu->uniformBlockSlot(cached.pipeline, "Uniforms");
            cached.uniformsSlot = u >= 0 ? u : 0;
            cached.bonesSlot = pk.skinned ? mGpu->uniformBlockSlot(cached.pipeline, "Bones") : -1;
        }
        mPipelines.push_back(cached);
        return &mPipelines[mPipelines.size() - 1];
    }

    void MeshRenderer::prepare(const Brush &brush, const Matrix4 &model, float morph)
    {
        Uniforms u;
        u.morph = morph;
        u.pad[0] = u.pad[1] = 0;
        Matrix4 mvp = mPending.viewProjection * model;
        std::memcpy(u.mvp, mvp.data(), sizeof(u.mvp));
        std::memcpy(u.model, model.data(), sizeof(u.model));
        u.color[0] = brush.getColor().x;
        u.color[1] = brush.getColor().y;
        u.color[2] = brush.getColor().z;
        u.color[3] = brush.getAlpha();
        u.ambient[0] = mPending.ambient.x;
        u.ambient[1] = mPending.ambient.y;
        u.ambient[2] = mPending.ambient.z;
        u.ambient[3] = 1.0f;
        u.fogColor[0] = mPending.fogColor.x;
        u.fogColor[1] = mPending.fogColor.y;
        u.fogColor[2] = mPending.fogColor.z;
        u.fogColor[3] = 1.0f;
        u.fogMode = mPending.fogMode;
        u.fogNear = mPending.fogNear;
        u.fogFar = mPending.fogFar;
        u.flags = brush.getFX();

        u.lightCount = mLightBlock.count;
        std::memcpy(u.lightPosType, mLightBlock.posType, sizeof(u.lightPosType));
        std::memcpy(u.lightColorRange, mLightBlock.colorRange, sizeof(u.lightColorRange));
        std::memcpy(u.lightDir, mLightBlock.dir, sizeof(u.lightDir));

        std::uint64_t offset = (std::uint64_t)mStagedCount * mUniformStride;
        if (mStaged.size() < offset + mUniformStride) mStaged.resize(offset + mUniformStride);
        std::memcpy(&mStaged[offset], &u, sizeof(Uniforms));
        ++mStagedCount;
    }

    void MeshRenderer::flushUniforms(gpu::Device &dev)
    {
        if (mStagedCount)
        {
            std::uint64_t bytes = (std::uint64_t)mStagedCount * mUniformStride;
            if (ensureBuffer(dev, mUniformBuffer, mUniformBufferCapacity, bytes, "meshrenderer.uniforms"))
                dev.updateBuffer(mUniformBuffer, 0, {mStaged.data(), bytes});
        }
        if (mBoneStagedCount)
        {
            std::uint64_t bytes = (std::uint64_t)mBoneStagedCount * mBoneStride;
            if (ensureBuffer(dev, mBoneBuffer, mBoneBufferCapacity, bytes, "meshrenderer.bones"))
                dev.updateBuffer(mBoneBuffer, 0, {mBoneStaged.data(), bytes});
        }
    }

    void MeshRenderer::prepareClear(const Vector &color)
    {
        Uniforms u;
        std::memset(&u, 0, sizeof(u));
        Matrix4 identity = Matrix4::identity();
        std::memcpy(u.mvp, identity.data(), sizeof(u.mvp));
        std::memcpy(u.model, identity.data(), sizeof(u.model));
        u.color[0] = color.x;
        u.color[1] = color.y;
        u.color[2] = color.z;
        u.color[3] = 1.0f;
        u.flags = FxFullbright;
        u.fogMode = FogNone;

        std::uint64_t offset = (std::uint64_t)mStagedCount * mUniformStride;
        if (mStaged.size() < offset + mUniformStride) mStaged.resize(offset + mUniformStride);
        std::memcpy(&mStaged[offset], &u, sizeof(Uniforms));
        ++mStagedCount;
    }

    void MeshRenderer::drawClear(gpu::Device &dev, int index, bool clearColor, bool clearDepth)
    {
        if (!clearColor && !clearDepth) return;
        if (index < 0 || (std::uint32_t)index >= mStagedCount) return;

        PipelineKey pk;
        pk.clear = true;
        pk.clearColor = clearColor;
        pk.clearDepth = clearDepth;

        const CachedPipeline *cp = pipelineFor(pk);
        if (!cp->pipeline.valid()) return;
        bindAndDraw(dev, cp, index, mClearQuad.geometry(), mWhiteTexture, -1);
    }

    void MeshRenderer::draw(gpu::Device &dev, int index, const GpuGeometry &geom, const Brush &brush, int boneSlot)
    {
        if (!geom.valid()) return;
        if (index < 0 || (std::uint32_t)index >= mStagedCount) return;
        if (boneSlot >= 0 && (std::uint32_t)boneSlot >= mBoneStagedCount) return;

        PipelineKey pk;
        pk.blend = brush.getBlend();
        pk.doubleSided = (brush.getFX() & FxDoubleSided) != 0;
        pk.hasTexture = brush.getTextureCount() > 0;
        pk.skinned = boneSlot >= 0 && geom.layout == GpuGeometry::LayoutSurface;
        pk.layout = geom.layout;

        const CachedPipeline *cp = pipelineFor(pk);
        if (!cp->pipeline.valid()) return;

        gpu::TextureHandle tex = pk.hasTexture ? brush.getTexture(0).handle : mWhiteTexture;
        if (!tex.valid()) tex = mWhiteTexture;

        bindAndDraw(dev, cp, index, geom, tex, pk.skinned ? boneSlot : -1);
    }

    void MeshRenderer::bindAndDraw(gpu::Device &dev, const CachedPipeline *cp, int index, const GpuGeometry &geom,
                                   gpu::TextureHandle tex, int boneSlot)
    {
        const bool skinned = boneSlot >= 0;
        // the GL backend re-issues glUseProgram/glBindTexture/glBindBuffer
        // on every call with no caching of its own, so skip binds that
        // haven't changed since the last draw this frame
        if (cp->pipeline.value() != mBound.pipeline)
        {
            dev.setPipeline(cp->pipeline);
            mBound.pipeline = cp->pipeline.value();
        }
        dev.bindUniformBuffer((std::uint32_t)cp->uniformsSlot, mUniformBuffer,
                              (std::uint64_t)index * mUniformStride, sizeof(Uniforms));
        if (skinned && cp->bonesSlot >= 0)
            dev.bindUniformBuffer((std::uint32_t)cp->bonesSlot, mBoneBuffer,
                                  (std::uint64_t)boneSlot * mBoneStride, kMaxGpuBones * sizeof(Matrix4));
        if (tex.value() != mBound.texture)
        {
            dev.bindTexture(0, tex, mSampler);
            mBound.texture = tex.value();
        }
        if (geom.vb.value() != mBound.vb || geom.vbOffset != mBound.vbOffset)
        {
            dev.bindVertexBuffer(0, geom.vb, geom.vbOffset);
            mBound.vb = geom.vb.value();
            mBound.vbOffset = geom.vbOffset;
        }
        if (geom.layout == GpuGeometry::LayoutMd2Morph)
        {
            if (geom.vb2.value() != mBound.vb2 || geom.vb2Offset != mBound.vb2Offset)
            {
                dev.bindVertexBuffer(1, geom.vb2, geom.vb2Offset);
                mBound.vb2 = geom.vb2.value();
                mBound.vb2Offset = geom.vb2Offset;
            }
            if (geom.uvb.value() != mBound.uvb)
            {
                dev.bindVertexBuffer(2, geom.uvb, 0);
                mBound.uvb = geom.uvb.value();
            }
        }
        if (geom.ib.value() != mBound.indexBuffer)
        {
            dev.bindIndexBuffer(geom.ib, gpu::IndexFormat::Uint16, 0);
            mBound.indexBuffer = geom.ib.value();
        }
        dev.drawIndexed(geom.indexCount, 1, 0, 0, 0);
    }
}
