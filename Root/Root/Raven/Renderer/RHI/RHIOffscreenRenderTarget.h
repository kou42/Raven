#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Renderer/RHI/RHIResource.h"
#include "Raven/Renderer/RHI/RHITexture.h"

#include <cstdint>
#include <string>

namespace Raven
{

// Scene View / Game View等、SwapChainとは独立した描画先の共通設定です。
// Color Attachmentは描画完了後にSampled TextureとしてUIから参照できることを契約に含めます。
// Native RTV / VkFramebufferやResource State / Image LayoutはBackend実装内へ閉じ込めます。
struct RHIOffscreenRenderTargetSpecification
{
    std::uint32_t Width = 1u;
    std::uint32_t Height = 1u;
    RHITextureFormat ColorFormat = RHITextureFormat::RGBA8;
    RHITextureFormat DepthFormat = RHITextureFormat::Depth24Stencil8;
    std::string DebugName;
};

bool IsValidRHIOffscreenRenderTargetSpecification(
    const RHIOffscreenRenderTargetSpecification& specification);

// Explicit Backend共通のoff-screen描画先です。
//
// Ownership:
// - RHIDeviceが生成し、EditorまたはExplicit RuntimeがRefで所有します。
// - Color AttachmentもRefで公開するため、ResizeでAttachmentが交換されても、そのFrameの
//   UIDrawListが保持する旧TextureはGPU描画完了まで生存できます。
// - Begin/End時の状態遷移はRHISceneCommandListが担当し、Editor層はnative handleを扱いません。
class RHIOffscreenRenderTarget : public RHIResource
{
public:
    ~RHIOffscreenRenderTarget() override = default;

    virtual bool Resize(std::uint32_t width, std::uint32_t height) = 0;
    virtual const RHIOffscreenRenderTargetSpecification& GetSpecification() const = 0;
    virtual const Ref<RHITexture>& GetColorAttachment() const = 0;
    virtual bool IsValid() const = 0;

protected:
    RHIOffscreenRenderTarget() = default;
};

} // namespace Raven
