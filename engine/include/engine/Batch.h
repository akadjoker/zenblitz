#pragma once

#include "engine/ShaderDialect.h"

#include "gpu/GPU.h"

#include "engine/Geom.h"
#include "engine/Matrix4.h"

#include <ct/vector.hpp>

#include <cstddef>
#include <cstdint>

namespace engine
{
  using blitz::Vector;

  struct FloatRect
  {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
  };

  constexpr int kFontFirstChar = 32;
  constexpr int kFontGlyphCount = 224;

  struct FontGlyph
  {
    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
    float xoff = 0.0f, yoff = 0.0f;
    float width = 0.0f, height = 0.0f;
    float advance = 0.0f;
  };

  class BatchRenderer
  {
  public:
    struct Config
    {
      std::size_t maxVertices = 32768;
      std::size_t maxDrawCalls = 256;
      std::size_t stackDepth = 32;
      bool enableProfiling = true;
    };

    struct Stats
    {
      std::size_t drawCalls = 0;
      std::size_t verticesDrawn = 0;
      std::size_t indicesDrawn = 0;
      std::size_t textureSwitches = 0;
      double batchTime = 0.0;
      double renderTime = 0.0;
      double totalTime = 0.0;
      std::size_t batchesFlushed = 0;
      std::size_t frameCount = 0;
    };

    enum class BlendMode
    {
      Alpha,
      Additive,
      Multiplied,
      AddColors,
      SubtractColors
    };

    static constexpr int ModeLines = 0x0001;
    static constexpr int ModeTriangles = 0x0004;
    static constexpr int ModeQuads = 0x0007;

    BatchRenderer();
    ~BatchRenderer();

    BatchRenderer(const BatchRenderer &) = delete;
    BatchRenderer &operator=(const BatchRenderer &) = delete;

    bool init(gpu::Device &device, ShaderDialect dialect = ShaderDialect::GLSL330);
    bool init(gpu::Device &device, ShaderDialect dialect, const Config &config);
    void shutdown();

    bool resize(int width, int height);
    void getWindowSize(int &width, int &height) const;

    void pushMatrix();
    void popMatrix();
    void loadIdentity();
    void translate(float x, float y, float z = 0.0f);
    void rotate(float angleDeg, float axisX, float axisY, float axisZ);
    void scale(float x, float y, float z = 1.0f);

    void setColor(unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255);
    void setColor(float r, float g, float b, float a = 1.0f);
    void setTexture(gpu::TextureHandle texture);
    void setBlendMode(BlendMode mode);
    void setTexcoord(float u, float v);
    void setDepthTest(bool enabled) { mDepthTestEnabled = enabled; }
    void setDepthWrite(bool enabled) { mDepthWriteEnabled = enabled; }
    void setBlend(bool enabled) { mBlendEnabled = enabled; }
    void setCullFace(bool enabled) { mCullFaceEnabled = enabled; }
    void setDefault3DState();

    void setClipRect(float x, float y, float width, float height);
    void setClipRect(const FloatRect &rect);
    void clearClipRect();
    bool isClipEnabled() const { return mClipEnabled; }
    const FloatRect &getClipRect() const { return mClipRect; }

    void begin(int mode);
    void end();
    void vertex2(float x, float y);
    void vertex3(float x, float y, float z);

    void drawLine(float x0, float y0, float x1, float y1);
    void drawThickLine(float x0, float y0, float x1, float y1, float thickness);
    void drawTriangle(float x1, float y1, float x2, float y2, float x3, float y3);
    void drawRect(float x, float y, float width, float height, bool fill = true);
    void drawQuad(float x, float y, float width, float height);
    void drawCircle(float cx, float cy, float radius, int segments = 32);
    void drawEllipse(float cx, float cy, float rx, float ry, bool fill = true, int segments = 32);
    void drawArc(float cx, float cy, float radius, float startDeg, float endDeg, int segments = 32);
    void drawRing(float cx, float cy, float rInner, float rOuter, bool fill = true, int segments = 32);
    void drawPolygon(float cx, float cy, int sides, float radius, float rotDeg = 0.0f, bool fill = true);
    void drawPolyline(const float *xyPairs, int pointCount);

    void drawLine3D(float x0, float y0, float z0, float x1, float y1, float z1);
    void drawTriangle3D(const Vector &a, const Vector &b, const Vector &c);
    void drawTriangle3D(const Vector &a, const Vec2f &uvA, const Vector &b, const Vec2f &uvB,
                        const Vector &c, const Vec2f &uvC);
    void drawTriangle3D(const Vector &a, const Vec2f &uvA, std::uint32_t colorA, const Vector &b,
                        const Vec2f &uvB, std::uint32_t colorB, const Vector &c, const Vec2f &uvC,
                        std::uint32_t colorC);

    void drawWireBox(float minX, float minY, float minZ, float maxX, float maxY, float maxZ);
    void drawWireSphere(float cx, float cy, float cz, float radius, int segments = 24);
    void drawWireCylinder(float cx, float cy, float cz, float radius, float height, int segments = 24);
    void drawWireCapsule(float cx, float cy, float cz, float radius, float height, int segments = 24);

    void drawSolidBox(float minX, float minY, float minZ, float maxX, float maxY, float maxZ);
    void drawSolidSphere(float cx, float cy, float cz, float radius, int rings = 12, int segments = 24);
    void drawSolidCylinder(float cx, float cy, float cz, float radius, float height, int segments = 24);
    void drawSolidCapsule(float cx, float cy, float cz, float radius, float height, int rings = 8, int segments = 24);

    void drawAxis(float x, float y, float z, float size = 1.0f);
    void drawWireGrid(float y, int slices, float spacing, bool axes = true);

    void drawTexture(gpu::TextureHandle texture, float dstX, float dstY, float dstW, float dstH, float srcX = 0.0f,
                     float srcY = 0.0f, float srcW = 0.0f, float srcH = 0.0f, float pivotX = 0.0f,
                     float pivotY = 0.0f, float rotationDeg = 0.0f);

    void drawRenderBatch();
    void update();
    void flip();
    void draw();

    void drawText(float x, float y, float size, const char *text);
    float textWidth(float size, const char *text) const;
    gpu::TextureHandle fontTexture() const { return mFontTexture; }
    gpu::TextureHandle whiteTexture() const { return mWhiteTexture; }

    void setProjection(const Matrix4 &matrix);
    const Matrix4 &getProjection() const { return mProjection; }

    void resetStats();
    const Stats &getStats() const { return mStats; }

    static std::uint32_t packColor(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
    static void unpackColor(std::uint32_t packed, unsigned char &r, unsigned char &g, unsigned char &b, unsigned char &a);

  private:
#pragma pack(push, 1)
    struct Vertex
    {
      float x, y, z;
      float u, v;
      unsigned char r, g, b, a;
    };
#pragma pack(pop)

    struct DrawCall
    {
      int mode = ModeTriangles;
      std::size_t vertexCount = 0;
      std::size_t vertexAlignment = 0;
      gpu::TextureHandle texture;
      BlendMode blendMode = BlendMode::Alpha;
      bool depthTest = false;
      bool depthWrite = false;
      bool blend = true;
      bool cullFace = false;
    };

    struct CachedPipeline
    {
      std::uint32_t key = 0;
      gpu::PipelineHandle pipeline;
    };

    struct ClipVertex
    {
      float x, y, u, v;
    };

    void updateProjection();
    void uploadBatch();
    void applyDrawCalls();
    bool ensureGpuBuffers(std::uint64_t vertexBytes, std::uint64_t indexBytes);
    void setupBuffers();
    void setupTexture();
    void setupFontTexture();
    const FontGlyph *glyphFor(unsigned char code) const;
    gpu::PipelineHandle pipelineFor(const DrawCall &call);
    void submitVertex(float x, float y, float z);
    void submitVertex(float x, float y, float z, float u, float v);
    void applyTransform(float &x, float &y, float &z);
    int clipPolygonToRect(const ClipVertex *in, int inCount, ClipVertex *out) const;
    bool clipSegmentToRect(float &x0, float &y0, float &x1, float &y1) const;
    void submitClippedQuad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3);
    void submitClippedLine(float x0, float y0, float x1, float y1);
    void emitLine(float x0, float y0, float x1, float y1);
    void emitTriangle(float x0, float y0, float x1, float y1, float x2, float y2);
    void emitTexturedTriangle(float x0, float y0, float u0, float v0, float x1, float y1, float u1, float v1,
                              float x2, float y2, float u2, float v2);

    Config mConfig;
    Stats mStats;
    ShaderDialect mDialect = ShaderDialect::GLSL330;

    ct::Vector<Vertex> mVertices;
    ct::Vector<std::uint16_t> mIndices;
    ct::Vector<DrawCall> mDrawCalls;
    ct::Vector<Matrix4> mMatrixStack;
    Matrix4 mCurrentMatrix = Matrix4::identity();

    std::uint32_t mCurrentColor = 0xFFFFFFFFu;
    gpu::TextureHandle mCurrentTexture;
    float mCurrentTexcoord[2] = {0.0f, 0.0f};
    int mCurrentMode = ModeTriangles;
    bool mInBeginEnd = false;
    BlendMode mCurrentBlendMode = BlendMode::Alpha;

    int mWindowWidth = 800;
    int mWindowHeight = 600;
    Matrix4 mProjection = Matrix4::identity();
    bool mDepthTestEnabled = false;
    bool mDepthWriteEnabled = false;
    bool mBlendEnabled = true;
    bool mCullFaceEnabled = false;

    bool mClipEnabled = false;
    FloatRect mClipRect;

    gpu::Device *mGpu = nullptr;
    gpu::BufferHandle mVertexBuffer;
    gpu::BufferHandle mIndexBuffer;
    std::uint64_t mVertexCapacityBytes = 0;
    std::uint64_t mIndexCapacityBytes = 0;
    gpu::BufferHandle mUniformBuffer;
    gpu::TextureHandle mWhiteTexture;
    gpu::TextureHandle mFontTexture;
    FontGlyph mGlyphs[kFontGlyphCount];
    float mFontAscent = 0.0f;
    float mFontLineHeight = 0.0f;
    gpu::SamplerHandle mSampler;
    bool mHasPendingUpload = false;
    ct::Vector<CachedPipeline> mPipelines;

    double mFrameStartTime = 0.0;
  };

} // namespace engine
