#pragma once

#include "engine/ShaderDialect.h"

#include "gpu/GPU.h"
#include "gpu/GPUBackend.h"

#include <cstdint>

namespace engine
{

  class Device;

  class Graphics
  {
  public:
    Graphics();
    ~Graphics();

    Graphics(const Graphics &) = delete;
    Graphics &operator=(const Graphics &) = delete;

    bool create(Device &window);
    void destroy();

    bool valid() const { return mGpu != nullptr; }
    gpu::Device &device() { return *mGpu; }
    gpu::Backend backend() const { return mBackend; }
    ShaderDialect shaderDialect() const
    {
      return mBackend == gpu::Backend::OpenGLES ? ShaderDialect::GLSLES300 : ShaderDialect::GLSL330;
    }

    bool beginFrame(float r, float g, float b, float a = 1.0f);
    void endFrame();
    bool inFrame() const { return mInFrame; }
    bool reopenScreenPass();

    bool beginSurfaceBlit();
    void endSurfaceBlit();

    std::uint32_t width() const { return mWidth; }
    std::uint32_t height() const { return mHeight; }

    /* The screen texture's own resolution - fixed once set, independent of
       the window/surface size. Call before/at the next beginFrame(); it
       does not resize with the window. */
    void setScreenSize(std::uint32_t width, std::uint32_t height);
    std::uint32_t screenWidth() const { return mScreenTextureWidth; }
    std::uint32_t screenHeight() const { return mScreenTextureHeight; }

    gpu::TextureHandle screenTexture() const { return mScreenTexture; }

    bool logErrors(const char *where);

  private:
    void refreshSize();
    bool ensureScreenTexture();

    Device *mWindow = nullptr;
    gpu::Device *mGpu = nullptr;
    gpu::Backend mBackend = gpu::Backend::Null;
    std::uint32_t mWidth = 0;
    std::uint32_t mHeight = 0;
    bool mInFrame = false;
    bool mSurfaceDepth = true;
    gpu::TextureHandle mScreenTexture;
    gpu::TextureHandle mScreenDepthTexture;
    std::uint32_t mScreenTextureWidth = 0, mScreenTextureHeight = 0;
    bool mScreenSizeSet = false;
    // createTexture leaves the screen texture's contents undefined; a
    // program that calls RenderWorld before ever calling Cls (Blitz3D
    // always allowed this - RenderWorld clears on its own) must still get
    // a real clear on its first pass rather than whatever garbage the GPU
    // handed back. Cleared by the first beginFrame() or reopenScreenPass()
    // after the texture is (re)created.
    bool mScreenCleared = false;
  };

} // namespace engine
