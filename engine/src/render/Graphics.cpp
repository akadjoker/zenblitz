#include "engine/Graphics.h"

#include "../core/Profiler.h"

#include "engine/Device.h"
#include "engine/Log.h"

#include "gpu/GPUBackend.h"

namespace kx
{

  Graphics::Graphics()
  {
  }

  Graphics::~Graphics()
  {
    destroy();
  }

  bool Graphics::create(Device &window)
  {
    destroy();

    mWindow = &window;
    refreshSize();

    gpu::DeviceDesc desc;
    desc.backend = window.getSurface().backend();
    desc.profile = gpu::RendererProfile::Portable;
    desc.surface = window.getSurface().surfaceDesc();

    gpu::GPUError error;
    mGpu = gpu::createDevice(desc, &error);
    if (!mGpu)
    {
      Log::error("Graphics: gpu::createDevice failed (%s)", error.message ? error.message : "no diagnostic");
      mWindow = nullptr;
      return false;
    }
    mBackend = desc.backend;

    logErrors("Graphics::create");
    return true;
  }

  void Graphics::destroy()
  {
    if (mGpu)
    {
      if (mInFrame)
        mGpu->endRenderPass();
      mInFrame = false;
      gpu::destroyDevice(mGpu);
      mGpu = nullptr;
    }
    mWindow = nullptr;
    mBackend = gpu::Backend::Null;
  }

  void Graphics::refreshSize()
  {
    mWindow->getSurface().drawableSize(mWidth, mHeight);
  }

  bool Graphics::logErrors(const char *where)
  {
    if (!mGpu)
      return false;
    bool found = false;
    gpu::GPUError error;
    while (mGpu->getError(error))
    {
      Log::error("%s: gpu error %u in operation %u (resource=%llu, offset=%llu, size=%llu): %s", where,
                 static_cast<unsigned>(error.code), static_cast<unsigned>(error.operation),
                 static_cast<unsigned long long>(error.resource), static_cast<unsigned long long>(error.value0),
                 static_cast<unsigned long long>(error.value1), error.message ? error.message : "no diagnostic");
      found = true;
    }
    return found;
  }

  bool Graphics::beginFrame(float r, float g, float b, float a)
  {
    if (!mGpu || mInFrame)
      return false;

    if (mWindow->consumeResized())
    {
      refreshSize();
      if (mWidth > 0 && mHeight > 0)
        mGpu->resizeSurface(mWidth, mHeight);
    }
    if (mGpu->surfaceState() != gpu::SurfaceState::Ready || mWidth == 0 || mHeight == 0)
      return false;

    gpu::RenderPassDesc pass;
    pass.colorCount = 1;
    pass.colors[0].surface = true;
    pass.colors[0].loadOp = gpu::LoadOp::Clear;
    pass.colors[0].storeOp = gpu::StoreOp::Store;
    pass.colors[0].clearColor[0] = r;
    pass.colors[0].clearColor[1] = g;
    pass.colors[0].clearColor[2] = b;
    pass.colors[0].clearColor[3] = a;
    pass.hasDepthStencil = mSurfaceDepth;
    pass.depthStencil.depthLoadOp = gpu::LoadOp::Clear;
    pass.depthStencil.stencilLoadOp = gpu::LoadOp::Clear;

    if (!mGpu->beginRenderPass(pass) && mSurfaceDepth)
    {
      mGpu->clearErrors();
      mSurfaceDepth = false;
      pass.hasDepthStencil = false;
      if (!mGpu->beginRenderPass(pass))
      {
        logErrors("Graphics::beginFrame");
        return false;
      }
    }
    mInFrame = true;
    return true;
  }

  void Graphics::endFrame()
  {
    if (!mGpu || !mInFrame)
      return;
    {
      KX_PROFILE_SCOPE("Graphics/EndPass");
      mGpu->endRenderPass();
    }
    mInFrame = false;
    {
      KX_PROFILE_SCOPE("Graphics/Present");
      mGpu->present();
    }
    logErrors("Graphics::endFrame");
  }

} // namespace kx
