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
      if (mScreenTexture.valid())
        mGpu->destroy(mScreenTexture);
      if (mScreenDepthTexture.valid())
        mGpu->destroy(mScreenDepthTexture);
      mScreenTexture = gpu::TextureHandle();
      mScreenDepthTexture = gpu::TextureHandle();
      mScreenTextureWidth = mScreenTextureHeight = 0;
      mScreenSizeSet = false;
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

  void Graphics::setScreenSize(std::uint32_t width, std::uint32_t height)
  {
    mScreenTextureWidth = width;
    mScreenTextureHeight = height;
    mScreenSizeSet = true;
    if (mScreenTexture.valid())
    {
      mGpu->destroy(mScreenTexture);
      mScreenTexture = gpu::TextureHandle();
    }
    if (mScreenDepthTexture.valid())
    {
      mGpu->destroy(mScreenDepthTexture);
      mScreenDepthTexture = gpu::TextureHandle();
    }
  }

  bool Graphics::ensureScreenTexture()
  {
    if (!mScreenSizeSet)
      setScreenSize(mWidth, mHeight);
    if (mScreenTexture.valid() && mScreenDepthTexture.valid())
      return true;

    gpu::TextureDesc desc;
    desc.width = mScreenTextureWidth;
    desc.height = mScreenTextureHeight;
    desc.format = gpu::Format::RGBA8;
    desc.usage = gpu::TextureUsageRenderTarget | gpu::TextureUsageSampled | gpu::TextureUsageCopySource;
    desc.debugName = "screen";
    mScreenTexture = mGpu->createTexture(desc);

    gpu::TextureDesc depthDesc;
    depthDesc.width = mScreenTextureWidth;
    depthDesc.height = mScreenTextureHeight;
    depthDesc.format = gpu::Format::Depth24Stencil8;
    depthDesc.usage = gpu::TextureUsageRenderTarget;
    depthDesc.debugName = "screen.depth";
    mScreenDepthTexture = mGpu->createTexture(depthDesc);

    return mScreenTexture.valid() && mScreenDepthTexture.valid();
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
    if (!ensureScreenTexture())
      return false;

    gpu::RenderPassDesc pass;
    pass.colorCount = 1;
    pass.colors[0].target.texture = mScreenTexture;
    pass.colors[0].surface = false;
    pass.colors[0].loadOp = gpu::LoadOp::Clear;
    pass.colors[0].storeOp = gpu::StoreOp::Store;
    pass.colors[0].clearColor[0] = r;
    pass.colors[0].clearColor[1] = g;
    pass.colors[0].clearColor[2] = b;
    pass.colors[0].clearColor[3] = a;
    pass.hasDepthStencil = mSurfaceDepth;
    pass.depthStencil.target.texture = mScreenDepthTexture;
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
    logErrors("Graphics::endFrame");
  }

  bool Graphics::reopenScreenPass()
  {
    if (!mGpu || mInFrame)
      return false;
    if (!mScreenTexture.valid())
      return false;

    gpu::RenderPassDesc pass;
    pass.colorCount = 1;
    pass.colors[0].target.texture = mScreenTexture;
    pass.colors[0].surface = false;
    pass.colors[0].loadOp = gpu::LoadOp::Load;
    pass.colors[0].storeOp = gpu::StoreOp::Store;
    pass.hasDepthStencil = mSurfaceDepth;
    pass.depthStencil.target.texture = mScreenDepthTexture;
    pass.depthStencil.depthLoadOp = gpu::LoadOp::Load;
    pass.depthStencil.stencilLoadOp = gpu::LoadOp::Load;

    if (!mGpu->beginRenderPass(pass))
    {
      logErrors("Graphics::reopenScreenPass");
      return false;
    }
    mInFrame = true;
    return true;
  }

  bool Graphics::beginSurfaceBlit()
  {
    if (!mGpu) return false;
    gpu::RenderPassDesc pass;
    pass.colorCount = 1;
    pass.colors[0].surface = true;
    pass.colors[0].loadOp = gpu::LoadOp::Clear;
    pass.colors[0].storeOp = gpu::StoreOp::Store;
    pass.hasDepthStencil = false;
    return mGpu->beginRenderPass(pass);
  }

  void Graphics::endSurfaceBlit()
  {
    if (!mGpu) return;
    {
      KX_PROFILE_SCOPE("Graphics/EndSurfacePass");
      mGpu->endRenderPass();
    }
    {
      KX_PROFILE_SCOPE("Graphics/Present");
      mGpu->present();
    }
    logErrors("Graphics::endSurfaceBlit");
  }

} // namespace kx
