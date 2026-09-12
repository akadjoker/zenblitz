#include "engine/MeshRenderer.h"
#include <cstring>
#include <cmath>

namespace engine
{
    static const char kVertexShader[] =
        "#version 330 core\n"
        "layout(location=0) in vec3 a_pos;\n"
        "layout(location=1) in vec3 a_normal;\n"
        "layout(location=2) in vec4 a_color;\n"
        "layout(location=3) in vec2 a_uv;\n"
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
        "  vec4 u_lightPosType[4];\n"
        "  vec4 u_lightColorRange[4];\n"
        "  vec4 u_lightDir[4];\n"
        "};\n"
        "out vec3 v_worldPos;\n"
        "out vec3 v_normal;\n"
        "out vec4 v_color;\n"
        "out vec2 v_uv;\n"
        "void main(){\n"
        "  vec4 wp = u_model * vec4(a_pos,1.0);\n"
        "  v_worldPos = wp.xyz;\n"
        "  v_normal = mat3(u_model) * a_normal;\n"
        "  v_color = a_color;\n"
        "  v_uv = a_uv;\n"
        "  gl_Position = u_mvp * vec4(a_pos,1.0);\n"
        "}\n";

    static const char kFragmentShader[] =
        "#version 330 core\n"
        "in vec3 v_worldPos;\n"
        "in vec3 v_normal;\n"
        "in vec4 v_color;\n"
        "in vec2 v_uv;\n"
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
        "  vec4 u_lightPosType[4];\n"
        "  vec4 u_lightColorRange[4];\n"
        "  vec4 u_lightDir[4];\n"
        "};\n"
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

    bool MeshRenderer::init(gpu::Device &dev)
    {
        mGpu = &dev;

        std::uint32_t align = dev.capabilities().uniformBufferOffsetAlignment;
        if (align == 0) align = 1;
        mUniformStride = ((std::uint32_t)sizeof(Uniforms) + align - 1) / align * align;

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

        return mSampler.valid() && mWhiteTexture.valid();
    }

    bool MeshRenderer::ensureUniformBuffer(gpu::Device &dev, std::uint32_t count)
    {
        std::uint64_t needed = (std::uint64_t)mUniformStride * count;
        if (needed <= mUniformBufferCapacity && mUniformBuffer.valid()) return true;

        if (mUniformBuffer.valid()) dev.destroy(mUniformBuffer);

        std::uint64_t capacity = mUniformBufferCapacity ? mUniformBufferCapacity : mUniformStride * 64;
        while (capacity < needed) capacity *= 2;

        gpu::BufferDesc desc;
        desc.size = capacity;
        desc.usage = gpu::BufferUsageUniform;
        desc.debugName = "meshrenderer.uniforms";
        mUniformBuffer = dev.createBuffer(desc);
        mUniformBufferCapacity = mUniformBuffer.valid() ? capacity : 0;
        return mUniformBuffer.valid();
    }

    void MeshRenderer::shutdown()
    {
        if (!mGpu) return;
        for (size_t k = 0; k < mPipelines.size(); ++k) mGpu->destroy(mPipelines[k].pipeline);
        mPipelines.clear();
        if (mUniformBuffer.valid()) mGpu->destroy(mUniformBuffer);
        if (mSampler.valid()) mGpu->destroy(mSampler);
        if (mWhiteTexture.valid()) mGpu->destroy(mWhiteTexture);
        mGpu = nullptr;
    }

    void MeshRenderer::setLights(const ct::Vector<Light *> &lights)
    {
        mLights = lights;
    }

    void MeshRenderer::beginFrame()
    {
        mStaged.clear();
    }

    gpu::PipelineHandle MeshRenderer::pipelineFor(const PipelineKey &pk)
    {
        std::uint32_t key = pk.key();
        for (size_t i = 0; i < mPipelines.size(); ++i)
            if (mPipelines[i].key == key) return mPipelines[i].pipeline;

        gpu::PipelineDesc desc;
        desc.vertex.source = {kVertexShader, sizeof(kVertexShader) - 1};
        desc.fragment.source = {kFragmentShader, sizeof(kFragmentShader) - 1};
        desc.vertex.debugName = "mesh.vs";
        desc.fragment.debugName = "mesh.fs";
        desc.debugName = "mesh";
        desc.vertexBufferCount = 1;
        desc.vertexBuffers[0].stride = sizeof(Surface::Vertex);
        desc.vertexBuffers[0].attributeCount = 4;
        desc.vertexBuffers[0].attributes[0] = {gpu::VertexFormat::Float32x3, (std::uint32_t)offsetof(Surface::Vertex, coords), 0};
        desc.vertexBuffers[0].attributes[1] = {gpu::VertexFormat::Float32x3, (std::uint32_t)offsetof(Surface::Vertex, normal), 1};
        desc.vertexBuffers[0].attributes[2] = {gpu::VertexFormat::Unorm8x4, (std::uint32_t)offsetof(Surface::Vertex, color), 2};
        desc.vertexBuffers[0].attributes[3] = {gpu::VertexFormat::Float32x2, (std::uint32_t)offsetof(Surface::Vertex, texCoords), 3};
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

        CachedPipeline cached;
        cached.key = key;
        cached.pipeline = mGpu->createPipeline(desc);
        mPipelines.push_back(cached);
        return cached.pipeline;
    }

    void MeshRenderer::prepare(const Surface *surface, const Brush &brush, const Matrix4 &model)
    {
        (void)surface;
        Uniforms u;
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

        int count = (int)mLights.size();
        if (count > kMaxRenderLights) count = kMaxRenderLights;
        u.lightCount = count;
        for (int i = 0; i < count; ++i)
        {
            Light *l = mLights[i];
            const Vector &pos = l->getRenderPosition();
            const Vector &dir = l->getRenderDirection();
            u.lightPosType[i][0] = pos.x;
            u.lightPosType[i][1] = pos.y;
            u.lightPosType[i][2] = pos.z;
            u.lightPosType[i][3] = (float)(l->getType() - 1);
            u.lightColorRange[i][0] = l->getColor().x;
            u.lightColorRange[i][1] = l->getColor().y;
            u.lightColorRange[i][2] = l->getColor().z;
            u.lightColorRange[i][3] = l->getRange();
            float innerCos = std::cos(l->getInnerAngle() * blitz::PI / 180.0f);
            u.lightDir[i][0] = dir.x;
            u.lightDir[i][1] = dir.y;
            u.lightDir[i][2] = dir.z;
            u.lightDir[i][3] = innerCos;
        }

        mStaged.push_back(u);
    }

    void MeshRenderer::flushUniforms(gpu::Device &dev)
    {
        if (mStaged.empty()) return;
        if (!ensureUniformBuffer(dev, (std::uint32_t)mStaged.size())) return;

        for (size_t i = 0; i < mStaged.size(); ++i)
            dev.updateBuffer(mUniformBuffer, i * mUniformStride, {&mStaged[i], sizeof(Uniforms)});
    }

    void MeshRenderer::draw(gpu::Device &dev, int index, const Surface *surface, const Brush &brush)
    {
        if (surface->gpuIndexCount() <= 0) return;
        if (index < 0 || (size_t)index >= mStaged.size()) return;

        PipelineKey pk;
        pk.blend = brush.getBlend();
        pk.doubleSided = (brush.getFX() & FxDoubleSided) != 0;
        pk.hasTexture = brush.getTextureCount() > 0;

        gpu::PipelineHandle pipeline = pipelineFor(pk);
        if (!pipeline.valid()) return;

        gpu::TextureHandle tex = pk.hasTexture ? brush.getTexture(0).handle : mWhiteTexture;
        if (!tex.valid()) tex = mWhiteTexture;

        dev.setPipeline(pipeline);
        dev.bindUniformBuffer(0, mUniformBuffer, (std::uint64_t)index * mUniformStride, sizeof(Uniforms));
        dev.bindTexture(0, tex, mSampler);
        dev.bindVertexBuffer(0, surface->vertexBuffer(), 0);
        dev.bindIndexBuffer(surface->indexBuffer(), gpu::IndexFormat::Uint16, 0);
        dev.drawIndexed((std::uint32_t)surface->gpuIndexCount(), 1, 0, 0, 0);
    }
}
