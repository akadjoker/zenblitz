#include "engine/Batch.h"

#include "engine/Log.h"

#include "FontData.h"

#include <SDL2/SDL_timer.h>

#include <cmath>
#include <cstring>

namespace kx
{

  namespace
  {

    constexpr int kFontCols = 16;
    constexpr int kFontAtlasWidth = 128;
    constexpr int kFontAtlasHeight = 48;
    constexpr float kPi = 3.14159265359f;
    constexpr float kDeg2Rad = kPi / 180.0f;

    const char kVertexShaderDesktop[] =
        "#version 330 core\n"
        "layout(location = 0) in vec3 a_position;\n"
        "layout(location = 1) in vec2 a_texcoord;\n"
        "layout(location = 2) in vec4 a_color;\n"
        "layout(std140) uniform BatchUniforms { mat4 u_mvp; };\n"
        "out vec2 v_texcoord;\n"
        "out vec4 v_color;\n"
        "void main()\n"
        "{\n"
        "  gl_Position = u_mvp * vec4(a_position, 1.0);\n"
        "  v_texcoord = a_texcoord;\n"
        "  v_color = a_color;\n"
        "}\n";

    const char kFragmentShaderDesktop[] =
        "#version 330 core\n"
        "in vec2 v_texcoord;\n"
        "in vec4 v_color;\n"
        "uniform sampler2D u_texture;\n"
        "out vec4 o_color;\n"
        "void main()\n"
        "{\n"
        "  o_color = texture(u_texture, v_texcoord) * v_color;\n"
        "}\n";

    const char kVertexShaderES[] =
        "#version 300 es\n"
        "layout(location = 0) in vec3 a_position;\n"
        "layout(location = 1) in vec2 a_texcoord;\n"
        "layout(location = 2) in vec4 a_color;\n"
        "layout(std140) uniform BatchUniforms { mat4 u_mvp; };\n"
        "out vec2 v_texcoord;\n"
        "out vec4 v_color;\n"
        "void main()\n"
        "{\n"
        "  gl_Position = u_mvp * vec4(a_position, 1.0);\n"
        "  v_texcoord = a_texcoord;\n"
        "  v_color = a_color;\n"
        "}\n";

    const char kFragmentShaderES[] =
        "#version 300 es\n"
        "precision highp float;\n"
        "in vec2 v_texcoord;\n"
        "in vec4 v_color;\n"
        "uniform sampler2D u_texture;\n"
        "out vec4 o_color;\n"
        "void main()\n"
        "{\n"
        "  o_color = texture(u_texture, v_texcoord) * v_color;\n"
        "}\n";

    double timeMilliseconds()
    {
      return static_cast<double>(SDL_GetPerformanceCounter()) * 1000.0 /
             static_cast<double>(SDL_GetPerformanceFrequency());
    }

    float maxf(float a, float b) { return a > b ? a : b; }
    float minf(float a, float b) { return a < b ? a : b; }

    void applyBlend(gpu::ColorTargetState &target, BatchRenderer::BlendMode mode)
    {
      target.blendEnabled = true;
      target.alphaBlend = {gpu::BlendFactor::One, gpu::BlendFactor::OneMinusSourceAlpha, gpu::BlendOperation::Add};
      switch (mode)
      {
      case BatchRenderer::BlendMode::Alpha:
        target.colorBlend = {gpu::BlendFactor::SourceAlpha, gpu::BlendFactor::OneMinusSourceAlpha, gpu::BlendOperation::Add};
        break;
      case BatchRenderer::BlendMode::Additive:
        target.colorBlend = {gpu::BlendFactor::SourceAlpha, gpu::BlendFactor::One, gpu::BlendOperation::Add};
        break;
      case BatchRenderer::BlendMode::Multiplied:
        target.colorBlend = {gpu::BlendFactor::DestinationColor, gpu::BlendFactor::OneMinusSourceAlpha, gpu::BlendOperation::Add};
        break;
      case BatchRenderer::BlendMode::AddColors:
        target.colorBlend = {gpu::BlendFactor::One, gpu::BlendFactor::One, gpu::BlendOperation::Add};
        break;
      case BatchRenderer::BlendMode::SubtractColors:
        target.colorBlend = {gpu::BlendFactor::One, gpu::BlendFactor::One, gpu::BlendOperation::ReverseSubtract};
        break;
      }
    }

  } // namespace

  BatchRenderer::BatchRenderer()
  {
  }

  BatchRenderer::~BatchRenderer()
  {
    shutdown();
  }

  bool BatchRenderer::init(gpu::Device &device, ShaderDialect dialect)
  {
    return init(device, dialect, Config());
  }

  bool BatchRenderer::init(gpu::Device &device, ShaderDialect dialect, const Config &config)
  {
    shutdown();
    mConfig = config;
    mDialect = dialect;
    if (mConfig.maxVertices > 65535)
      mConfig.maxVertices = 65535;
    mVertices.reserve(mConfig.maxVertices);
    mIndices.reserve(mConfig.maxVertices * 2);
    mDrawCalls.reserve(mConfig.maxDrawCalls);
    mMatrixStack.reserve(mConfig.stackDepth);
    mGpu = &device;

    setupBuffers();
    setupTexture();
    setupFontTexture();
    updateProjection();
    resetStats();

    if (!mUniformBuffer.valid() || !mWhiteTexture.valid() || !mFontTexture.valid() || !mSampler.valid())
    {
      Log::error("BatchRenderer: failed to create gpu resources");
      shutdown();
      return false;
    }
    return true;
  }

  void BatchRenderer::shutdown()
  {
    if (!mGpu)
      return;

    for (std::size_t i = 0; i < mPipelines.size(); ++i)
      mGpu->destroy(mPipelines[i].pipeline);
    if (mVertexBuffer.valid())
      mGpu->destroy(mVertexBuffer);
    if (mIndexBuffer.valid())
      mGpu->destroy(mIndexBuffer);
    if (mUniformBuffer.valid())
      mGpu->destroy(mUniformBuffer);
    if (mWhiteTexture.valid())
      mGpu->destroy(mWhiteTexture);
    if (mFontTexture.valid())
      mGpu->destroy(mFontTexture);
    if (mSampler.valid())
      mGpu->destroy(mSampler);
    mPipelines.clear();

    mVertexBuffer = gpu::BufferHandle();
    mIndexBuffer = gpu::BufferHandle();
    mVertexCapacityBytes = 0;
    mIndexCapacityBytes = 0;
    mHasPendingUpload = false;
    mUniformBuffer = gpu::BufferHandle();
    mWhiteTexture = gpu::TextureHandle();
    mFontTexture = gpu::TextureHandle();
    mSampler = gpu::SamplerHandle();
    mGpu = nullptr;

    mVertices.clear();
    mIndices.clear();
    mDrawCalls.clear();
    mMatrixStack.clear();
  }

  bool BatchRenderer::resize(int width, int height)
  {
    if (width <= 0 || height <= 0)
      return false;
    mWindowWidth = width;
    mWindowHeight = height;
    updateProjection();
    return true;
  }

  void BatchRenderer::getWindowSize(int &width, int &height) const
  {
    width = mWindowWidth;
    height = mWindowHeight;
  }

  void BatchRenderer::pushMatrix()
  {
    if (mMatrixStack.size() < mConfig.stackDepth)
      mMatrixStack.push_back(mCurrentMatrix);
  }

  void BatchRenderer::popMatrix()
  {
    if (!mMatrixStack.empty())
    {
      mCurrentMatrix = mMatrixStack.back();
      mMatrixStack.pop_back();
    }
    else
      mCurrentMatrix = Math::Mat4::Identity();
  }

  void BatchRenderer::loadIdentity()
  {
    mCurrentMatrix = Math::Mat4::Identity();
  }

  void BatchRenderer::translate(float x, float y, float z)
  {
    mCurrentMatrix = mCurrentMatrix * Math::Mat4::Translation(Math::Vec3(x, y, z));
  }

  void BatchRenderer::rotate(float angleDeg, float axisX, float axisY, float axisZ)
  {
    const Math::Vec3 axis(axisX, axisY, axisZ);
    if (axis.LengthSquared() <= 0.000001f)
      return;
    mCurrentMatrix =
        mCurrentMatrix * Math::Quaternion::FromAxisAngle(axis.Normalized(), angleDeg * kDeg2Rad).ToMat4();
  }

  void BatchRenderer::scale(float x, float y, float z)
  {
    mCurrentMatrix = mCurrentMatrix * Math::Mat4::Scale(Math::Vec3(x, y, z));
  }

  void BatchRenderer::setColor(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
  {
    mCurrentColor = packColor(r, g, b, a);
  }

  void BatchRenderer::setColor(float r, float g, float b, float a)
  {
    mCurrentColor = packColor(static_cast<unsigned char>(r * 255.0f + 0.5f), static_cast<unsigned char>(g * 255.0f + 0.5f),
                              static_cast<unsigned char>(b * 255.0f + 0.5f), static_cast<unsigned char>(a * 255.0f + 0.5f));
  }

  void BatchRenderer::setTexture(gpu::TextureHandle texture)
  {
    if (mCurrentTexture != texture)
    {
      mCurrentTexture = texture;
      if (mConfig.enableProfiling)
        mStats.textureSwitches++;
    }
  }

  void BatchRenderer::setBlendMode(BlendMode mode)
  {
    mCurrentBlendMode = mode;
  }

  void BatchRenderer::setTexcoord(float u, float v)
  {
    mCurrentTexcoord[0] = u;
    mCurrentTexcoord[1] = v;
  }

  void BatchRenderer::setDefault3DState()
  {
    mDepthTestEnabled = true;
    mDepthWriteEnabled = true;
    mBlendEnabled = false;
    mCullFaceEnabled = false;
  }

  void BatchRenderer::setClipRect(float x, float y, float width, float height)
  {
    mClipRect = FloatRect{x, y, width, height};
    mClipEnabled = true;
  }

  void BatchRenderer::setClipRect(const FloatRect &rect)
  {
    mClipRect = rect;
    mClipEnabled = true;
  }

  void BatchRenderer::clearClipRect()
  {
    mClipEnabled = false;
  }

  int BatchRenderer::clipPolygonToRect(const ClipVertex *in, int inCount, ClipVertex *out) const
  {
    const float minX = mClipRect.x;
    const float minY = mClipRect.y;
    const float maxX = mClipRect.x + mClipRect.width;
    const float maxY = mClipRect.y + mClipRect.height;

    ClipVertex tmp[16];
    const ClipVertex *src = in;
    int srcCount = inCount;

    for (int plane = 0; plane < 4; ++plane)
    {
      ClipVertex *dst = (plane % 2 == 0) ? tmp : out;
      int dstCount = 0;

      for (int i = 0; i < srcCount; ++i)
      {
        const ClipVertex &cur = src[i];
        const ClipVertex &prev = src[(i + srcCount - 1) % srcCount];

        bool curIn = false;
        bool prevIn = false;
        switch (plane)
        {
        case 0:
          curIn = cur.x >= minX;
          prevIn = prev.x >= minX;
          break;
        case 1:
          curIn = cur.x <= maxX;
          prevIn = prev.x <= maxX;
          break;
        case 2:
          curIn = cur.y >= minY;
          prevIn = prev.y >= minY;
          break;
        case 3:
          curIn = cur.y <= maxY;
          prevIn = prev.y <= maxY;
          break;
        }

        if (curIn != prevIn)
        {
          float t = 0.0f;
          switch (plane)
          {
          case 0:
            t = (minX - prev.x) / (cur.x - prev.x);
            break;
          case 1:
            t = (maxX - prev.x) / (cur.x - prev.x);
            break;
          case 2:
            t = (minY - prev.y) / (cur.y - prev.y);
            break;
          case 3:
            t = (maxY - prev.y) / (cur.y - prev.y);
            break;
          }
          ClipVertex isect;
          isect.x = prev.x + t * (cur.x - prev.x);
          isect.y = prev.y + t * (cur.y - prev.y);
          isect.u = prev.u + t * (cur.u - prev.u);
          isect.v = prev.v + t * (cur.v - prev.v);
          if (dstCount < 16)
            dst[dstCount++] = isect;
        }
        if (curIn && dstCount < 16)
          dst[dstCount++] = cur;
      }

      src = dst;
      srcCount = dstCount;
      if (srcCount == 0)
        return 0;
    }

    if (src == tmp)
      for (int i = 0; i < srcCount; ++i)
        out[i] = tmp[i];
    return srcCount;
  }

  bool BatchRenderer::clipSegmentToRect(float &x0, float &y0, float &x1, float &y1) const
  {
    const float minX = mClipRect.x;
    const float minY = mClipRect.y;
    const float maxX = mClipRect.x + mClipRect.width;
    const float maxY = mClipRect.y + mClipRect.height;

    const float dx = x1 - x0;
    const float dy = y1 - y0;
    float tMin = 0.0f;
    float tMax = 1.0f;

    const float p[4] = {-dx, dx, -dy, dy};
    const float q[4] = {x0 - minX, maxX - x0, y0 - minY, maxY - y0};

    for (int i = 0; i < 4; ++i)
    {
      if (p[i] == 0.0f)
      {
        if (q[i] < 0.0f)
          return false;
      }
      else
      {
        const float t = q[i] / p[i];
        if (p[i] < 0.0f)
          tMin = maxf(tMin, t);
        else
          tMax = minf(tMax, t);
      }
    }

    if (tMin > tMax)
      return false;

    const float nx0 = x0 + tMin * dx;
    const float ny0 = y0 + tMin * dy;
    const float nx1 = x0 + tMax * dx;
    const float ny1 = y0 + tMax * dy;
    x0 = nx0;
    y0 = ny0;
    x1 = nx1;
    y1 = ny1;
    return true;
  }

  void BatchRenderer::submitClippedQuad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3)
  {
    const ClipVertex in[4] = {{x0, y0, 0.0f, 0.0f}, {x1, y1, 1.0f, 0.0f}, {x2, y2, 1.0f, 1.0f}, {x3, y3, 0.0f, 1.0f}};
    ClipVertex out[16];
    const int n = clipPolygonToRect(in, 4, out);
    if (n < 3)
      return;
    for (int i = 1; i + 1 < n; ++i)
    {
      submitVertex(out[0].x, out[0].y, 0.0f, out[0].u, out[0].v);
      submitVertex(out[i].x, out[i].y, 0.0f, out[i].u, out[i].v);
      submitVertex(out[i + 1].x, out[i + 1].y, 0.0f, out[i + 1].u, out[i + 1].v);
    }
  }

  void BatchRenderer::submitClippedLine(float x0, float y0, float x1, float y1)
  {
    if (!clipSegmentToRect(x0, y0, x1, y1))
      return;
    submitVertex(x0, y0, 0.0f);
    submitVertex(x1, y1, 0.0f);
  }

  void BatchRenderer::emitLine(float x0, float y0, float x1, float y1)
  {
    if (mClipEnabled)
    {
      float z0 = 0.0f;
      float z1 = 0.0f;
      applyTransform(x0, y0, z0);
      applyTransform(x1, y1, z1);
      submitClippedLine(x0, y0, x1, y1);
    }
    else
    {
      vertex2(x0, y0);
      vertex2(x1, y1);
    }
  }

  void BatchRenderer::emitTriangle(float x0, float y0, float x1, float y1, float x2, float y2)
  {
    emitTexturedTriangle(x0, y0, 0.0f, 0.0f, x1, y1, 0.0f, 0.0f, x2, y2, 0.0f, 0.0f);
  }

  void BatchRenderer::emitTexturedTriangle(float x0, float y0, float u0, float v0, float x1, float y1, float u1,
                                           float v1, float x2, float y2, float u2, float v2)
  {
    if (mClipEnabled)
    {
      float z0 = 0.0f;
      float z1 = 0.0f;
      float z2 = 0.0f;
      applyTransform(x0, y0, z0);
      applyTransform(x1, y1, z1);
      applyTransform(x2, y2, z2);
      const ClipVertex in[3] = {{x0, y0, u0, v0}, {x1, y1, u1, v1}, {x2, y2, u2, v2}};
      ClipVertex out[16];
      const int n = clipPolygonToRect(in, 3, out);
      for (int i = 1; i + 1 < n; ++i)
      {
        submitVertex(out[0].x, out[0].y, 0.0f, out[0].u, out[0].v);
        submitVertex(out[i].x, out[i].y, 0.0f, out[i].u, out[i].v);
        submitVertex(out[i + 1].x, out[i + 1].y, 0.0f, out[i + 1].u, out[i + 1].v);
      }
    }
    else
    {
      setTexcoord(u0, v0);
      vertex2(x0, y0);
      setTexcoord(u1, v1);
      vertex2(x1, y1);
      setTexcoord(u2, v2);
      vertex2(x2, y2);
    }
  }

  void BatchRenderer::vertex2(float x, float y)
  {
    vertex3(x, y, 0.0f);
  }

  void BatchRenderer::vertex3(float x, float y, float z)
  {
    if (!mInBeginEnd)
      return;
    applyTransform(x, y, z);
    Vertex v;
    v.x = x;
    v.y = y;
    v.z = z;
    v.u = mCurrentTexcoord[0];
    v.v = mCurrentTexcoord[1];
    unpackColor(mCurrentColor, v.r, v.g, v.b, v.a);
    mVertices.push_back(v);
  }

  void BatchRenderer::begin(int mode)
  {
    if (mInBeginEnd)
      end();

    mInBeginEnd = true;
    mCurrentMode = mode;

    if (mDrawCalls.empty() || mDrawCalls.back().mode != mode || mDrawCalls.back().texture != mCurrentTexture ||
        mDrawCalls.back().blendMode != mCurrentBlendMode || mDrawCalls.back().depthTest != mDepthTestEnabled ||
        mDrawCalls.back().depthWrite != mDepthWriteEnabled || mDrawCalls.back().blend != mBlendEnabled ||
        mDrawCalls.back().cullFace != mCullFaceEnabled)
    {
      DrawCall call;
      call.mode = mode;
      call.vertexCount = 0;
      call.vertexAlignment = mVertices.size();
      call.texture = mCurrentTexture;
      call.blendMode = mCurrentBlendMode;
      call.depthTest = mDepthTestEnabled;
      call.depthWrite = mDepthWriteEnabled;
      call.blend = mBlendEnabled;
      call.cullFace = mCullFaceEnabled;
      mDrawCalls.push_back(call);
    }
  }

  void BatchRenderer::end()
  {
    if (!mInBeginEnd)
      return;
    mInBeginEnd = false;
    if (!mDrawCalls.empty())
      mDrawCalls.back().vertexCount = mVertices.size() - mDrawCalls.back().vertexAlignment;
  }

  void BatchRenderer::drawLine(float x0, float y0, float x1, float y1)
  {
    setTexture(mWhiteTexture);
    begin(ModeLines);
    emitLine(x0, y0, x1, y1);
    end();
  }

  void BatchRenderer::drawTriangle(float x1, float y1, float x2, float y2, float x3, float y3)
  {
    setTexture(mWhiteTexture);
    begin(ModeTriangles);
    emitTriangle(x1, y1, x2, y2, x3, y3);
    end();
  }

  void BatchRenderer::drawRect(float x, float y, float width, float height, bool fill)
  {
    setTexture(mWhiteTexture);
    if (fill)
    {
      begin(ModeTriangles);
      if (mClipEnabled)
      {
        float x0 = x, y0 = y, z0 = 0.0f;
        float x1 = x + width, y1 = y, z1 = 0.0f;
        float x2 = x + width, y2 = y + height, z2 = 0.0f;
        float x3 = x, y3 = y + height, z3 = 0.0f;
        applyTransform(x0, y0, z0);
        applyTransform(x1, y1, z1);
        applyTransform(x2, y2, z2);
        applyTransform(x3, y3, z3);
        submitClippedQuad(x0, y0, x1, y1, x2, y2, x3, y3);
      }
      else
      {
        vertex2(x, y);
        vertex2(x + width, y);
        vertex2(x, y + height);
        vertex2(x + width, y);
        vertex2(x + width, y + height);
        vertex2(x, y + height);
      }
      end();
    }
    else
    {
      begin(ModeLines);
      const float corners[4][2] = {{x, y}, {x + width, y}, {x + width, y + height}, {x, y + height}};
      for (int i = 0; i < 4; ++i)
        emitLine(corners[i][0], corners[i][1], corners[(i + 1) % 4][0], corners[(i + 1) % 4][1]);
      end();
    }
  }

  void BatchRenderer::drawQuad(float x, float y, float width, float height)
  {
    setTexture(mWhiteTexture);
    begin(ModeQuads);
    vertex2(x, y);
    vertex2(x + width, y);
    vertex2(x + width, y + height);
    vertex2(x, y + height);
    end();
  }

  void BatchRenderer::drawCircle(float cx, float cy, float radius, int segments)
  {
    if (segments < 3)
      segments = 3;
    setTexture(mWhiteTexture);
    begin(ModeLines);
    for (int i = 0; i < segments; ++i)
    {
      const float a0 = static_cast<float>(i) * 2.0f * kPi / static_cast<float>(segments);
      const float a1 = static_cast<float>(i + 1) * 2.0f * kPi / static_cast<float>(segments);
      emitLine(cx + std::cos(a0) * radius, cy + std::sin(a0) * radius, cx + std::cos(a1) * radius,
               cy + std::sin(a1) * radius);
    }
    end();
  }

  void BatchRenderer::drawPolyline(const float *xyPairs, int pointCount)
  {
    if (!xyPairs || pointCount < 2)
      return;
    setTexture(mWhiteTexture);
    begin(ModeLines);
    for (int i = 0; i < pointCount - 1; ++i)
      emitLine(xyPairs[i * 2], xyPairs[i * 2 + 1], xyPairs[(i + 1) * 2], xyPairs[(i + 1) * 2 + 1]);
    end();
  }

  void BatchRenderer::drawThickLine(float x0, float y0, float x1, float y1, float thickness)
  {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.0f)
      return;
    const float nx = -dy / length * thickness * 0.5f;
    const float ny = dx / length * thickness * 0.5f;
    setTexture(mWhiteTexture);
    begin(ModeTriangles);
    emitTriangle(x0 + nx, y0 + ny, x0 - nx, y0 - ny, x1 - nx, y1 - ny);
    emitTriangle(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x1 + nx, y1 + ny);
    end();
  }

  void BatchRenderer::drawEllipse(float cx, float cy, float rx, float ry, bool fill, int segments)
  {
    if (segments < 3)
      segments = 3;
    const float step = 2.0f * kPi / static_cast<float>(segments);
    setTexture(mWhiteTexture);
    if (fill)
    {
      begin(ModeTriangles);
      for (int i = 0; i < segments; ++i)
      {
        const float a0 = i * step;
        const float a1 = (i + 1) * step;
        emitTriangle(cx, cy, cx + rx * std::cos(a0), cy + ry * std::sin(a0), cx + rx * std::cos(a1), cy + ry * std::sin(a1));
      }
      end();
    }
    else
    {
      begin(ModeLines);
      for (int i = 0; i < segments; ++i)
      {
        const float a0 = i * step;
        const float a1 = (i + 1) * step;
        emitLine(cx + rx * std::cos(a0), cy + ry * std::sin(a0), cx + rx * std::cos(a1), cy + ry * std::sin(a1));
      }
      end();
    }
  }

  void BatchRenderer::drawArc(float cx, float cy, float radius, float startDeg, float endDeg, int segments)
  {
    if (segments < 1)
      segments = 1;
    const float a0 = startDeg * kDeg2Rad;
    const float step = (endDeg - startDeg) * kDeg2Rad / static_cast<float>(segments);
    setTexture(mWhiteTexture);
    begin(ModeLines);
    for (int i = 0; i < segments; ++i)
    {
      const float t0 = a0 + i * step;
      const float t1 = a0 + (i + 1) * step;
      emitLine(cx + radius * std::cos(t0), cy + radius * std::sin(t0), cx + radius * std::cos(t1), cy + radius * std::sin(t1));
    }
    end();
  }

  void BatchRenderer::drawRing(float cx, float cy, float rInner, float rOuter, bool fill, int segments)
  {
    if (segments < 3)
      segments = 3;
    const float step = 2.0f * kPi / static_cast<float>(segments);
    setTexture(mWhiteTexture);
    if (fill)
    {
      begin(ModeTriangles);
      for (int i = 0; i < segments; ++i)
      {
        const float a0 = i * step;
        const float a1 = (i + 1) * step;
        const float ix0 = cx + rInner * std::cos(a0), iy0 = cy + rInner * std::sin(a0);
        const float ox0 = cx + rOuter * std::cos(a0), oy0 = cy + rOuter * std::sin(a0);
        const float ix1 = cx + rInner * std::cos(a1), iy1 = cy + rInner * std::sin(a1);
        const float ox1 = cx + rOuter * std::cos(a1), oy1 = cy + rOuter * std::sin(a1);
        emitTriangle(ix0, iy0, ox0, oy0, ox1, oy1);
        emitTriangle(ix0, iy0, ox1, oy1, ix1, iy1);
      }
      end();
    }
    else
    {
      drawCircle(cx, cy, rInner, segments);
      drawCircle(cx, cy, rOuter, segments);
    }
  }

  void BatchRenderer::drawPolygon(float cx, float cy, int sides, float radius, float rotDeg, bool fill)
  {
    if (sides < 3)
      sides = 3;
    const float step = 2.0f * kPi / static_cast<float>(sides);
    const float rot = rotDeg * kDeg2Rad;
    setTexture(mWhiteTexture);
    if (fill)
    {
      begin(ModeTriangles);
      for (int i = 0; i < sides; ++i)
      {
        const float a0 = rot + i * step;
        const float a1 = rot + (i + 1) * step;
        emitTriangle(cx, cy, cx + radius * std::cos(a0), cy + radius * std::sin(a0), cx + radius * std::cos(a1),
                     cy + radius * std::sin(a1));
      }
      end();
    }
    else
    {
      begin(ModeLines);
      for (int i = 0; i < sides; ++i)
      {
        const float a0 = rot + i * step;
        const float a1 = rot + (i + 1) * step;
        emitLine(cx + radius * std::cos(a0), cy + radius * std::sin(a0), cx + radius * std::cos(a1), cy + radius * std::sin(a1));
      }
      end();
    }
  }

  void BatchRenderer::drawLine3D(float x0, float y0, float z0, float x1, float y1, float z1)
  {
    setTexture(mWhiteTexture);
    begin(ModeLines);
    vertex3(x0, y0, z0);
    vertex3(x1, y1, z1);
    end();
  }

  void BatchRenderer::drawTriangle3D(const Math::Vec3 &a, const Math::Vec3 &b, const Math::Vec3 &c)
  {
    begin(ModeTriangles);
    vertex3(a.x, a.y, a.z);
    vertex3(b.x, b.y, b.z);
    vertex3(c.x, c.y, c.z);
    end();
  }

  void BatchRenderer::drawTriangle3D(const Math::Vec3 &a, const Math::Vec2 &uvA, const Math::Vec3 &b, const Math::Vec2 &uvB,
                                     const Math::Vec3 &c, const Math::Vec2 &uvC)
  {
    begin(ModeTriangles);
    setTexcoord(uvA.x, uvA.y);
    vertex3(a.x, a.y, a.z);
    setTexcoord(uvB.x, uvB.y);
    vertex3(b.x, b.y, b.z);
    setTexcoord(uvC.x, uvC.y);
    vertex3(c.x, c.y, c.z);
    end();
  }

  void BatchRenderer::drawTriangle3D(const Math::Vec3 &a, const Math::Vec2 &uvA, std::uint32_t colorA, const Math::Vec3 &b,
                                     const Math::Vec2 &uvB, std::uint32_t colorB, const Math::Vec3 &c, const Math::Vec2 &uvC,
                                     std::uint32_t colorC)
  {
    begin(ModeTriangles);
    mCurrentColor = colorA;
    setTexcoord(uvA.x, uvA.y);
    vertex3(a.x, a.y, a.z);
    mCurrentColor = colorB;
    setTexcoord(uvB.x, uvB.y);
    vertex3(b.x, b.y, b.z);
    mCurrentColor = colorC;
    setTexcoord(uvC.x, uvC.y);
    vertex3(c.x, c.y, c.z);
    end();
  }

  void BatchRenderer::drawWireBox(float minX, float minY, float minZ, float maxX, float maxY, float maxZ)
  {
    setTexture(mWhiteTexture);
    begin(ModeLines);
    vertex3(minX, minY, minZ);
    vertex3(maxX, minY, minZ);
    vertex3(maxX, minY, minZ);
    vertex3(maxX, minY, maxZ);
    vertex3(maxX, minY, maxZ);
    vertex3(minX, minY, maxZ);
    vertex3(minX, minY, maxZ);
    vertex3(minX, minY, minZ);
    vertex3(minX, maxY, minZ);
    vertex3(maxX, maxY, minZ);
    vertex3(maxX, maxY, minZ);
    vertex3(maxX, maxY, maxZ);
    vertex3(maxX, maxY, maxZ);
    vertex3(minX, maxY, maxZ);
    vertex3(minX, maxY, maxZ);
    vertex3(minX, maxY, minZ);
    vertex3(minX, minY, minZ);
    vertex3(minX, maxY, minZ);
    vertex3(maxX, minY, minZ);
    vertex3(maxX, maxY, minZ);
    vertex3(maxX, minY, maxZ);
    vertex3(maxX, maxY, maxZ);
    vertex3(minX, minY, maxZ);
    vertex3(minX, maxY, maxZ);
    end();
  }

  void BatchRenderer::drawWireSphere(float cx, float cy, float cz, float radius, int segments)
  {
    if (segments < 3)
      segments = 3;
    const float step = 2.0f * kPi / static_cast<float>(segments);
    setTexture(mWhiteTexture);
    begin(ModeLines);
    for (int i = 0; i < segments; ++i)
    {
      const float a0 = i * step;
      const float a1 = (i + 1) * step;
      vertex3(cx + radius * std::cos(a0), cy + radius * std::sin(a0), cz);
      vertex3(cx + radius * std::cos(a1), cy + radius * std::sin(a1), cz);
      vertex3(cx + radius * std::cos(a0), cy, cz + radius * std::sin(a0));
      vertex3(cx + radius * std::cos(a1), cy, cz + radius * std::sin(a1));
      vertex3(cx, cy + radius * std::sin(a0), cz + radius * std::cos(a0));
      vertex3(cx, cy + radius * std::sin(a1), cz + radius * std::cos(a1));
    }
    end();
  }

  void BatchRenderer::drawWireCylinder(float cx, float cy, float cz, float radius, float height, int segments)
  {
    if (segments < 3)
      segments = 3;
    const float halfH = height * 0.5f;
    const float step = 2.0f * kPi / static_cast<float>(segments);
    setTexture(mWhiteTexture);
    begin(ModeLines);
    for (int i = 0; i < segments; ++i)
    {
      const float a0 = i * step;
      const float a1 = (i + 1) * step;
      const float x0 = radius * std::cos(a0), z0 = radius * std::sin(a0);
      const float x1 = radius * std::cos(a1), z1 = radius * std::sin(a1);
      vertex3(cx + x0, cy - halfH, cz + z0);
      vertex3(cx + x1, cy - halfH, cz + z1);
      vertex3(cx + x0, cy + halfH, cz + z0);
      vertex3(cx + x1, cy + halfH, cz + z1);
    }
    for (int i = 0; i < 4; ++i)
    {
      const float a = i * (kPi * 0.5f);
      vertex3(cx + radius * std::cos(a), cy - halfH, cz + radius * std::sin(a));
      vertex3(cx + radius * std::cos(a), cy + halfH, cz + radius * std::sin(a));
    }
    end();
  }

  void BatchRenderer::drawWireCapsule(float cx, float cy, float cz, float radius, float height, int segments)
  {
    if (segments < 4)
      segments = 4;
    const float halfH = height * 0.5f;
    const float step = 2.0f * kPi / static_cast<float>(segments);
    const int hsegs = segments / 2;
    const float hstep = kPi / static_cast<float>(hsegs);

    setTexture(mWhiteTexture);
    begin(ModeLines);
    for (int i = 0; i < segments; ++i)
    {
      const float a0 = i * step;
      const float a1 = (i + 1) * step;
      vertex3(cx + radius * std::cos(a0), cy - halfH, cz + radius * std::sin(a0));
      vertex3(cx + radius * std::cos(a1), cy - halfH, cz + radius * std::sin(a1));
      vertex3(cx + radius * std::cos(a0), cy + halfH, cz + radius * std::sin(a0));
      vertex3(cx + radius * std::cos(a1), cy + halfH, cz + radius * std::sin(a1));
    }
    for (int i = 0; i < 4; ++i)
    {
      const float a = i * (kPi * 0.5f);
      vertex3(cx + radius * std::cos(a), cy - halfH, cz + radius * std::sin(a));
      vertex3(cx + radius * std::cos(a), cy + halfH, cz + radius * std::sin(a));
    }
    for (int i = 0; i < hsegs; ++i)
    {
      const float t0 = i * hstep;
      const float t1 = (i + 1) * hstep;
      vertex3(cx + radius * std::cos(t0), cy + halfH + radius * std::sin(t0), cz);
      vertex3(cx + radius * std::cos(t1), cy + halfH + radius * std::sin(t1), cz);
      vertex3(cx, cy + halfH + radius * std::sin(t0), cz + radius * std::cos(t0));
      vertex3(cx, cy + halfH + radius * std::sin(t1), cz + radius * std::cos(t1));
    }
    for (int i = 0; i < hsegs; ++i)
    {
      const float t0 = i * hstep;
      const float t1 = (i + 1) * hstep;
      vertex3(cx + radius * std::cos(t0), cy - halfH - radius * std::sin(t0), cz);
      vertex3(cx + radius * std::cos(t1), cy - halfH - radius * std::sin(t1), cz);
      vertex3(cx, cy - halfH - radius * std::sin(t0), cz + radius * std::cos(t0));
      vertex3(cx, cy - halfH - radius * std::sin(t1), cz + radius * std::cos(t1));
    }
    end();
  }

  void BatchRenderer::drawSolidBox(float minX, float minY, float minZ, float maxX, float maxY, float maxZ)
  {
    setTexture(mWhiteTexture);
    begin(ModeTriangles);
    vertex3(minX, minY, minZ);
    vertex3(maxX, minY, maxZ);
    vertex3(maxX, minY, minZ);
    vertex3(minX, minY, minZ);
    vertex3(minX, minY, maxZ);
    vertex3(maxX, minY, maxZ);
    vertex3(minX, maxY, minZ);
    vertex3(maxX, maxY, minZ);
    vertex3(maxX, maxY, maxZ);
    vertex3(minX, maxY, minZ);
    vertex3(maxX, maxY, maxZ);
    vertex3(minX, maxY, maxZ);
    vertex3(minX, minY, minZ);
    vertex3(maxX, maxY, minZ);
    vertex3(minX, maxY, minZ);
    vertex3(minX, minY, minZ);
    vertex3(maxX, minY, minZ);
    vertex3(maxX, maxY, minZ);
    vertex3(minX, minY, maxZ);
    vertex3(minX, maxY, maxZ);
    vertex3(maxX, maxY, maxZ);
    vertex3(minX, minY, maxZ);
    vertex3(maxX, maxY, maxZ);
    vertex3(maxX, minY, maxZ);
    vertex3(minX, minY, minZ);
    vertex3(minX, maxY, maxZ);
    vertex3(minX, minY, maxZ);
    vertex3(minX, minY, minZ);
    vertex3(minX, maxY, minZ);
    vertex3(minX, maxY, maxZ);
    vertex3(maxX, minY, minZ);
    vertex3(maxX, minY, maxZ);
    vertex3(maxX, maxY, maxZ);
    vertex3(maxX, minY, minZ);
    vertex3(maxX, maxY, maxZ);
    vertex3(maxX, maxY, minZ);
    end();
  }

  void BatchRenderer::drawSolidSphere(float cx, float cy, float cz, float radius, int rings, int segments)
  {
    if (rings < 2)
      rings = 2;
    if (segments < 3)
      segments = 3;
    const float rstep = kPi / static_cast<float>(rings);
    const float sstep = 2.0f * kPi / static_cast<float>(segments);
    setTexture(mWhiteTexture);
    begin(ModeTriangles);
    for (int r = 0; r < rings; ++r)
    {
      const float phi0 = r * rstep;
      const float phi1 = (r + 1) * rstep;
      for (int s = 0; s < segments; ++s)
      {
        const float th0 = s * sstep;
        const float th1 = (s + 1) * sstep;
        const float x00 = radius * std::sin(phi0) * std::cos(th0), y00 = radius * std::cos(phi0), z00 = radius * std::sin(phi0) * std::sin(th0);
        const float x10 = radius * std::sin(phi1) * std::cos(th0), y10 = radius * std::cos(phi1), z10 = radius * std::sin(phi1) * std::sin(th0);
        const float x01 = radius * std::sin(phi0) * std::cos(th1), y01 = radius * std::cos(phi0), z01 = radius * std::sin(phi0) * std::sin(th1);
        const float x11 = radius * std::sin(phi1) * std::cos(th1), y11 = radius * std::cos(phi1), z11 = radius * std::sin(phi1) * std::sin(th1);
        vertex3(cx + x00, cy + y00, cz + z00);
        vertex3(cx + x10, cy + y10, cz + z10);
        vertex3(cx + x11, cy + y11, cz + z11);
        vertex3(cx + x00, cy + y00, cz + z00);
        vertex3(cx + x11, cy + y11, cz + z11);
        vertex3(cx + x01, cy + y01, cz + z01);
      }
    }
    end();
  }

  void BatchRenderer::drawSolidCylinder(float cx, float cy, float cz, float radius, float height, int segments)
  {
    if (segments < 3)
      segments = 3;
    const float halfH = height * 0.5f;
    const float step = 2.0f * kPi / static_cast<float>(segments);
    setTexture(mWhiteTexture);
    begin(ModeTriangles);
    for (int i = 0; i < segments; ++i)
    {
      const float a0 = i * step;
      const float a1 = (i + 1) * step;
      const float x0 = radius * std::cos(a0), z0 = radius * std::sin(a0);
      const float x1 = radius * std::cos(a1), z1 = radius * std::sin(a1);
      vertex3(cx + x0, cy - halfH, cz + z0);
      vertex3(cx + x1, cy - halfH, cz + z1);
      vertex3(cx + x1, cy + halfH, cz + z1);
      vertex3(cx + x0, cy - halfH, cz + z0);
      vertex3(cx + x1, cy + halfH, cz + z1);
      vertex3(cx + x0, cy + halfH, cz + z0);
      vertex3(cx, cy - halfH, cz);
      vertex3(cx + x1, cy - halfH, cz + z1);
      vertex3(cx + x0, cy - halfH, cz + z0);
      vertex3(cx, cy + halfH, cz);
      vertex3(cx + x0, cy + halfH, cz + z0);
      vertex3(cx + x1, cy + halfH, cz + z1);
    }
    end();
  }

  void BatchRenderer::drawSolidCapsule(float cx, float cy, float cz, float radius, float height, int rings, int segments)
  {
    if (rings < 1)
      rings = 1;
    if (segments < 3)
      segments = 3;
    const float halfH = height * 0.5f;
    const float sstep = 2.0f * kPi / static_cast<float>(segments);
    const float hstep = (kPi * 0.5f) / static_cast<float>(rings);

    setTexture(mWhiteTexture);
    begin(ModeTriangles);
    for (int i = 0; i < segments; ++i)
    {
      const float a0 = i * sstep;
      const float a1 = (i + 1) * sstep;
      const float x0 = radius * std::cos(a0), z0 = radius * std::sin(a0);
      const float x1 = radius * std::cos(a1), z1 = radius * std::sin(a1);
      vertex3(cx + x0, cy - halfH, cz + z0);
      vertex3(cx + x1, cy - halfH, cz + z1);
      vertex3(cx + x1, cy + halfH, cz + z1);
      vertex3(cx + x0, cy - halfH, cz + z0);
      vertex3(cx + x1, cy + halfH, cz + z1);
      vertex3(cx + x0, cy + halfH, cz + z0);
    }
    for (int half = 0; half < 2; ++half)
    {
      const float base = half == 0 ? 0.0f : kPi * 0.5f;
      const float offset = half == 0 ? halfH : -halfH;
      for (int r = 0; r < rings; ++r)
      {
        const float t0 = base + r * hstep;
        const float t1 = base + (r + 1) * hstep;
        for (int s = 0; s < segments; ++s)
        {
          const float a0 = s * sstep;
          const float a1 = (s + 1) * sstep;
          const float x00 = radius * std::sin(t0) * std::cos(a0), y00 = radius * std::cos(t0), z00 = radius * std::sin(t0) * std::sin(a0);
          const float x10 = radius * std::sin(t1) * std::cos(a0), y10 = radius * std::cos(t1), z10 = radius * std::sin(t1) * std::sin(a0);
          const float x01 = radius * std::sin(t0) * std::cos(a1), y01 = radius * std::cos(t0), z01 = radius * std::sin(t0) * std::sin(a1);
          const float x11 = radius * std::sin(t1) * std::cos(a1), y11 = radius * std::cos(t1), z11 = radius * std::sin(t1) * std::sin(a1);
          vertex3(cx + x00, cy + offset + y00, cz + z00);
          vertex3(cx + x10, cy + offset + y10, cz + z10);
          vertex3(cx + x11, cy + offset + y11, cz + z11);
          vertex3(cx + x00, cy + offset + y00, cz + z00);
          vertex3(cx + x11, cy + offset + y11, cz + z11);
          vertex3(cx + x01, cy + offset + y01, cz + z01);
        }
      }
    }
    end();
  }

  void BatchRenderer::drawAxis(float x, float y, float z, float size)
  {
    const std::uint32_t savedColor = mCurrentColor;
    setTexture(mWhiteTexture);
    setColor(static_cast<unsigned char>(255), static_cast<unsigned char>(0), static_cast<unsigned char>(0));
    begin(ModeLines);
    vertex3(x, y, z);
    vertex3(x + size, y, z);
    end();
    setColor(static_cast<unsigned char>(0), static_cast<unsigned char>(255), static_cast<unsigned char>(0));
    begin(ModeLines);
    vertex3(x, y, z);
    vertex3(x, y + size, z);
    end();
    setColor(static_cast<unsigned char>(0), static_cast<unsigned char>(0), static_cast<unsigned char>(255));
    begin(ModeLines);
    vertex3(x, y, z);
    vertex3(x, y, z + size);
    end();
    mCurrentColor = savedColor;
  }

  void BatchRenderer::drawWireGrid(float y, int slices, float spacing, bool axes)
  {
    slices = slices < 1 ? 1 : slices;
    const float half = static_cast<float>(slices) * spacing * 0.5f;
    setTexture(mWhiteTexture);
    begin(ModeLines);
    for (int i = 0; i <= slices; ++i)
    {
      const float offset = -half + static_cast<float>(i) * spacing;
      const bool center = axes && std::fabs(offset) < 0.0001f;
      if (center)
        setColor(0.8f, 0.2f, 0.2f, 1.0f);
      else
        setColor(0.45f, 0.45f, 0.45f, 1.0f);
      vertex3(offset, y, -half);
      vertex3(offset, y, half);
      if (center)
        setColor(0.2f, 0.8f, 0.2f, 1.0f);
      else
        setColor(0.45f, 0.45f, 0.45f, 1.0f);
      vertex3(-half, y, offset);
      vertex3(half, y, offset);
    }
    end();
    if (axes)
      setColor(1.0f, 1.0f, 1.0f, 1.0f);
  }

  void BatchRenderer::drawTexture(gpu::TextureHandle texture, float dstX, float dstY, float dstW, float dstH, float srcX,
                                  float srcY, float srcW, float srcH, float pivotX, float pivotY, float rotationDeg)
  {
    setTexture(texture);
    if (rotationDeg != 0.0f)
    {
      pushMatrix();
      translate(dstX + pivotX, dstY + pivotY);
      rotate(rotationDeg, 0.0f, 0.0f, 1.0f);
      translate(-pivotX, -pivotY);
      dstX = 0.0f;
      dstY = 0.0f;
    }

    begin(ModeTriangles);
    float u0 = srcX, v0 = srcY;
    float u1 = srcX + srcW, v1 = srcY + srcH;
    if (srcW == 0.0f || srcH == 0.0f)
    {
      u0 = 0.0f;
      v0 = 0.0f;
      u1 = 1.0f;
      v1 = 1.0f;
    }
    emitTexturedTriangle(dstX, dstY, u0, v0, dstX + dstW, dstY, u1, v0, dstX + dstW, dstY + dstH, u1, v1);
    emitTexturedTriangle(dstX, dstY, u0, v0, dstX + dstW, dstY + dstH, u1, v1, dstX, dstY + dstH, u0, v1);
    end();

    if (rotationDeg != 0.0f)
      popMatrix();
  }

  void BatchRenderer::drawRenderBatch()
  {
    if (mInBeginEnd)
      end();
    if (!mVertices.empty())
      uploadBatch();
  }

  void BatchRenderer::update()
  {
    mStats.drawCalls = 0;
    mStats.verticesDrawn = 0;
    mStats.indicesDrawn = 0;
    mStats.textureSwitches = 0;
    mStats.batchesFlushed = 0;
    if (mConfig.enableProfiling)
      mFrameStartTime = timeMilliseconds();
  }

  void BatchRenderer::flip()
  {
    const double start = timeMilliseconds();
    if (mInBeginEnd)
      end();
    mHasPendingUpload = !mVertices.empty() && !mDrawCalls.empty();
    if (mHasPendingUpload)
      uploadBatch();
    const double finish = timeMilliseconds();
    if (mConfig.enableProfiling)
    {
      mStats.renderTime = finish - start;
      mStats.totalTime = finish - mFrameStartTime;
      mStats.frameCount++;
      mStats.batchTime = mStats.totalTime - mStats.renderTime;
    }
  }

  void BatchRenderer::draw()
  {
    if (!mHasPendingUpload)
      return;
    mHasPendingUpload = false;
    applyDrawCalls();
    mVertices.clear();
    mIndices.clear();
    mDrawCalls.clear();
  }

  void BatchRenderer::resetStats()
  {
    mStats = Stats();
  }

  void BatchRenderer::setProjection(const Math::Mat4 &matrix)
  {
    mProjection = matrix;
  }

  std::uint32_t BatchRenderer::packColor(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
  {
    return static_cast<std::uint32_t>(r) | (static_cast<std::uint32_t>(g) << 8) | (static_cast<std::uint32_t>(b) << 16) |
           (static_cast<std::uint32_t>(a) << 24);
  }

  void BatchRenderer::unpackColor(std::uint32_t packed, unsigned char &r, unsigned char &g, unsigned char &b, unsigned char &a)
  {
    r = packed & 0xFF;
    g = (packed >> 8) & 0xFF;
    b = (packed >> 16) & 0xFF;
    a = (packed >> 24) & 0xFF;
  }

  void BatchRenderer::updateProjection()
  {
    mProjection = Math::Mat4::Ortho(0.0f, static_cast<float>(mWindowWidth), static_cast<float>(mWindowHeight), 0.0f, -1.0f, 1.0f);
  }

  bool BatchRenderer::ensureGpuBuffers(std::uint64_t vertexBytes, std::uint64_t indexBytes)
  {
    if (vertexBytes > mVertexCapacityBytes)
    {
      if (mVertexBuffer.valid())
        mGpu->destroy(mVertexBuffer);
      std::uint64_t capacity = mVertexCapacityBytes ? mVertexCapacityBytes : mConfig.maxVertices * sizeof(Vertex);
      while (capacity < vertexBytes)
        capacity *= 2;
      gpu::BufferDesc desc;
      desc.size = capacity;
      desc.usage = gpu::BufferUsageVertex;
      desc.debugName = "batch.vertices";
      mVertexBuffer = mGpu->createBuffer(desc);
      mVertexCapacityBytes = mVertexBuffer.valid() ? capacity : 0;
    }
    if (indexBytes > mIndexCapacityBytes)
    {
      if (mIndexBuffer.valid())
        mGpu->destroy(mIndexBuffer);
      std::uint64_t capacity =
          mIndexCapacityBytes ? mIndexCapacityBytes : mConfig.maxVertices * 3 * sizeof(std::uint16_t);
      while (capacity < indexBytes)
        capacity *= 2;
      gpu::BufferDesc desc;
      desc.size = capacity;
      desc.usage = gpu::BufferUsageIndex;
      desc.debugName = "batch.indices";
      mIndexBuffer = mGpu->createBuffer(desc);
      mIndexCapacityBytes = mIndexBuffer.valid() ? capacity : 0;
    }
    const bool vertexReady = mVertexBuffer.valid() || vertexBytes == 0;
    const bool indexReady = mIndexBuffer.valid() || indexBytes == 0;
    return vertexReady && indexReady;
  }

  void BatchRenderer::uploadBatch()
  {
    if (!mGpu || mVertices.empty() || mDrawCalls.empty())
      return;

    mIndices.clear();
    for (std::size_t i = 0; i < mDrawCalls.size(); ++i)
    {
      const DrawCall &call = mDrawCalls[i];
      if (call.mode != ModeQuads)
        continue;
      const std::size_t quadCount = call.vertexCount / 4;
      for (std::size_t q = 0; q < quadCount; ++q)
      {
        const std::size_t base = call.vertexAlignment + q * 4;
        mIndices.push_back(static_cast<std::uint16_t>(base + 0));
        mIndices.push_back(static_cast<std::uint16_t>(base + 1));
        mIndices.push_back(static_cast<std::uint16_t>(base + 2));
        mIndices.push_back(static_cast<std::uint16_t>(base + 0));
        mIndices.push_back(static_cast<std::uint16_t>(base + 2));
        mIndices.push_back(static_cast<std::uint16_t>(base + 3));
      }
    }

    if (!ensureGpuBuffers(mVertices.size() * sizeof(Vertex), mIndices.size() * sizeof(std::uint16_t)))
      return;

    // A frame with nothing batched still reaches here. GPU::updateBuffer
    // rejects zero-byte writes, so an empty batch would report an
    // out-of-bounds error every frame, as ImGuiRenderer already guards against.
    if (!mVertices.empty())
      mGpu->updateBuffer(mVertexBuffer, 0, {mVertices.data(), mVertices.size() * sizeof(Vertex)});
    if (!mIndices.empty())
      mGpu->updateBuffer(mIndexBuffer, 0, {mIndices.data(), mIndices.size() * sizeof(std::uint16_t)});
    mGpu->updateBuffer(mUniformBuffer, 0, {mProjection.Data(), sizeof(Math::Mat4)});

    if (mConfig.enableProfiling)
    {
      mStats.drawCalls += mDrawCalls.size();
      mStats.verticesDrawn += mVertices.size();
      mStats.indicesDrawn += mIndices.size();
      mStats.batchesFlushed++;
    }
  }

  gpu::PipelineHandle BatchRenderer::pipelineFor(const DrawCall &call)
  {
    const std::uint32_t key = static_cast<std::uint32_t>(call.mode) | (call.depthTest ? 1u << 8 : 0u) |
                              (call.depthWrite ? 1u << 9 : 0u) | (call.blend ? 1u << 10 : 0u) |
                              (call.cullFace ? 1u << 11 : 0u) | (static_cast<std::uint32_t>(call.blendMode) << 12);

    for (std::size_t i = 0; i < mPipelines.size(); ++i)
      if (mPipelines[i].key == key)
        return mPipelines[i].pipeline;

    gpu::PipelineDesc desc;
    if (mDialect == ShaderDialect::GLSLES300)
    {
      desc.vertex.source = {kVertexShaderES, sizeof(kVertexShaderES) - 1};
      desc.fragment.source = {kFragmentShaderES, sizeof(kFragmentShaderES) - 1};
    }
    else
    {
      desc.vertex.source = {kVertexShaderDesktop, sizeof(kVertexShaderDesktop) - 1};
      desc.fragment.source = {kFragmentShaderDesktop, sizeof(kFragmentShaderDesktop) - 1};
    }
    desc.vertex.debugName = "batch.vs";
    desc.fragment.debugName = "batch.fs";
    desc.debugName = "batch";
    desc.vertexBufferCount = 1;
    desc.vertexBuffers[0].stride = sizeof(Vertex);
    desc.vertexBuffers[0].attributeCount = 3;
    desc.vertexBuffers[0].attributes[0] = {gpu::VertexFormat::Float32x3, static_cast<std::uint32_t>(offsetof(Vertex, x)), 0};
    desc.vertexBuffers[0].attributes[1] = {gpu::VertexFormat::Float32x2, static_cast<std::uint32_t>(offsetof(Vertex, u)), 1};
    desc.vertexBuffers[0].attributes[2] = {gpu::VertexFormat::Unorm8x4, static_cast<std::uint32_t>(offsetof(Vertex, r)), 2};
    desc.topology = call.mode == ModeLines ? gpu::Topology::Lines : gpu::Topology::Triangles;
    desc.colorTargetCount = 1;
    desc.colorTargets[0].format = gpu::Format::RGBA8;
    if (call.blend)
      applyBlend(desc.colorTargets[0], call.blendMode);
    desc.depthStencil.depthTestEnabled = call.depthTest;
    desc.depthStencil.depthWriteEnabled = call.depthWrite;
    desc.raster.cullMode = call.cullFace ? gpu::CullMode::Back : gpu::CullMode::None;

    CachedPipeline cached;
    cached.key = key;
    cached.pipeline = mGpu->createPipeline(desc);
    mPipelines.push_back(cached);
    return cached.pipeline;
  }

  void BatchRenderer::applyDrawCalls()
  {
    std::size_t vertexOffset = 0;
    std::uint32_t indexOffset = 0;
    for (std::size_t i = 0; i < mDrawCalls.size(); ++i)
    {
      const DrawCall &call = mDrawCalls[i];
      const gpu::PipelineHandle pipeline = pipelineFor(call);
      if (!pipeline.valid() || call.vertexCount == 0)
      {
        vertexOffset += call.vertexCount;
        continue;
      }
      mGpu->setPipeline(pipeline);
      mGpu->bindUniformBuffer(0, mUniformBuffer, 0, sizeof(Math::Mat4));
      mGpu->bindTexture(0, call.texture.valid() ? call.texture : mWhiteTexture, mSampler);
      mGpu->bindVertexBuffer(0, mVertexBuffer, 0);

      if (call.mode == ModeQuads)
      {
        const std::uint32_t indexCount = static_cast<std::uint32_t>(call.vertexCount / 4) * 6;
        mGpu->bindIndexBuffer(mIndexBuffer, gpu::IndexFormat::Uint16, 0);
        mGpu->drawIndexed(indexCount, 1, indexOffset, 0, 0);
        indexOffset += indexCount;
      }
      else
        mGpu->draw(static_cast<std::uint32_t>(call.vertexCount), 1, static_cast<std::uint32_t>(vertexOffset), 0);

      vertexOffset += call.vertexCount;
    }
  }

  void BatchRenderer::setupBuffers()
  {
    gpu::BufferDesc uniformDesc;
    uniformDesc.size = sizeof(Math::Mat4);
    uniformDesc.usage = gpu::BufferUsageUniform;
    uniformDesc.debugName = "batch.uniforms";
    mUniformBuffer = mGpu->createBuffer(uniformDesc);

    gpu::SamplerDesc samplerDesc;
    samplerDesc.minFilter = gpu::Filter::Nearest;
    samplerDesc.magFilter = gpu::Filter::Nearest;
    samplerDesc.mipFilter = gpu::Filter::Nearest;
    samplerDesc.addressU = gpu::AddressMode::ClampToEdge;
    samplerDesc.addressV = gpu::AddressMode::ClampToEdge;
    samplerDesc.lodMax = 0.0f;
    samplerDesc.debugName = "batch.sampler";
    mSampler = mGpu->createSampler(samplerDesc);
  }

  void BatchRenderer::setupTexture()
  {
    const unsigned char whitePixel[4] = {255, 255, 255, 255};
    gpu::TextureDesc desc;
    desc.width = 1;
    desc.height = 1;
    desc.format = gpu::Format::RGBA8;
    desc.initialData = {whitePixel, sizeof(whitePixel)};
    desc.debugName = "batch.white";
    mWhiteTexture = mGpu->createTexture(desc);
  }

  void BatchRenderer::setupFontTexture()
  {
    ct::Vector<unsigned char> atlas(static_cast<std::size_t>(kFontAtlasWidth) * kFontAtlasHeight * 4, static_cast<unsigned char>(0));
    for (int g = 0; g < 96; ++g)
    {
      const int cellX = (g % kFontCols) * 8;
      const int cellY = (g / kFontCols) * 8;
      for (int row = 0; row < 8; ++row)
      {
        const unsigned char bits = kFont8x8[g][row];
        for (int col = 0; col < 8; ++col)
        {
          if (!((bits >> col) & 1))
            continue;
          unsigned char *p = &atlas[(static_cast<std::size_t>(cellY + row) * kFontAtlasWidth + cellX + col) * 4];
          p[0] = p[1] = p[2] = p[3] = 255;
        }
      }
    }

    gpu::TextureDesc desc;
    desc.width = kFontAtlasWidth;
    desc.height = kFontAtlasHeight;
    desc.format = gpu::Format::RGBA8;
    desc.initialData = {atlas.data(), atlas.size()};
    desc.debugName = "batch.font";
    mFontTexture = mGpu->createTexture(desc);
  }

  void BatchRenderer::submitVertex(float x, float y, float z)
  {
    submitVertex(x, y, z, mCurrentTexcoord[0], mCurrentTexcoord[1]);
  }

  void BatchRenderer::submitVertex(float x, float y, float z, float u, float v)
  {
    Vertex vert;
    vert.x = x;
    vert.y = y;
    vert.z = z;
    vert.u = u;
    vert.v = v;
    unpackColor(mCurrentColor, vert.r, vert.g, vert.b, vert.a);
    mVertices.push_back(vert);
  }

  void BatchRenderer::applyTransform(float &x, float &y, float &z)
  {
    const Math::Vec3 transformed = mCurrentMatrix.TransformPoint(Math::Vec3(x, y, z));
    x = transformed.x;
    y = transformed.y;
    z = transformed.z;
  }

  Math::Vec4 fontGlyphUVRect(unsigned char code)
  {
    if (code < 32 || code > 127 || code == ' ')
      return Math::Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    const float cw = 8.0f / static_cast<float>(kFontAtlasWidth);
    const float ch = 8.0f / static_cast<float>(kFontAtlasHeight);
    const int g = code - 32;
    return Math::Vec4(static_cast<float>(g % kFontCols) * cw, static_cast<float>(g / kFontCols) * ch, cw, ch);
  }

  void BatchRenderer::drawText(float x, float y, float size, const char *text)
  {
    if (!text || size <= 0.0f)
      return;

    setTexture(mFontTexture);
    begin(ModeTriangles);
    float penX = x;
    float penY = y;
    for (const char *c = text; *c; ++c)
    {
      if (*c == '\n')
      {
        penX = x;
        penY += size;
        continue;
      }
      unsigned char code = static_cast<unsigned char>(*c);
      if (code < 32 || code > 127)
        code = '?';
      const Math::Vec4 rect = fontGlyphUVRect(code);
      if (rect.z > 0.0f)
      {
        const float u0 = rect.x, v0 = rect.y, u1 = rect.x + rect.z, v1 = rect.y + rect.w;
        emitTexturedTriangle(penX, penY, u0, v0, penX + size, penY, u1, v0, penX, penY + size, u0, v1);
        emitTexturedTriangle(penX + size, penY, u1, v0, penX + size, penY + size, u1, v1, penX, penY + size, u0, v1);
      }
      penX += size;
    }
    end();
  }

  float BatchRenderer::textWidth(float size, const char *text) const
  {
    if (!text)
      return 0.0f;
    std::uint32_t longest = 0;
    std::uint32_t line = 0;
    for (const char *c = text; *c; ++c)
    {
      if (*c == '\n')
      {
        if (line > longest)
          longest = line;
        line = 0;
        continue;
      }
      ++line;
    }
    if (line > longest)
      longest = line;
    return static_cast<float>(longest) * size;
  }

} // namespace kx
