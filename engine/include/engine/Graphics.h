#pragma once

#include "engine/ShaderDialect.h"

#include "gpu/GPU.h"
#include "gpu/GPUBackend.h"

#include <cstdint>

namespace kx
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

    std::uint32_t width() const { return mWidth; }
    std::uint32_t height() const { return mHeight; }

    bool logErrors(const char *where);

  private:
    void refreshSize();

    Device *mWindow = nullptr;
    gpu::Device *mGpu = nullptr;
    gpu::Backend mBackend = gpu::Backend::Null;
    std::uint32_t mWidth = 0;
    std::uint32_t mHeight = 0;
    bool mInFrame = false;
    bool mSurfaceDepth = true;
  };

} // namespace kx
