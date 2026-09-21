#pragma once

#include <cstddef>
#include <vector>

#include "Raven/Core/Base.h"
#include "Raven/Renderer/RHI/RHIBuffer.h"
#include "Raven/Renderer/RHI/RHIGraphicsPipeline.h"
#include "Raven/Renderer/RHI/RHITexture.h"
#include "Raven/Renderer/RHI/RHITypes.h"

namespace Raven
{

// ============================================================================
// RHIDevice
// ============================================================================
// GPU Resource生成の入口をGraphics APIから分離します。
// 将来Shader / Pipeline等もこのDeviceへ集約し、上位RendererがOpenGL実装を
// 直接生成しない構造へ段階的に移行します。
class RHIDevice
{
public:
    virtual ~RHIDevice() = default;

    virtual RHIBackend GetBackend() const = 0;

    virtual Ref<RHIBuffer> CreateBuffer(
        const RHIBufferSpecification& specification,
        const void* initialData = nullptr) = 0;

    // Explicit API用Graphics Pipeline生成口。既存OpenGLRHIDeviceはLegacy Pipelineを
    // 使用するため、対応Backendの実装が揃うまでは明示的に未対応を返します。
    // Vulkan/DX12ではScene Contextと同じnative Device/RenderTargetから生成してください。
    virtual Ref<RHIGraphicsPipeline> CreateGraphicsPipeline(
        const RHIGraphicsPipelineSpecification& specification)
    {
        (void)specification;
        return nullptr;
    }

    // 現在のScene Render Targetと互換なAttachment情報を返します。
    // Legacy BackendやScene Pipeline未対応Backendはfalseを返します。
    virtual bool GetGraphicsPipelineTarget(
        RHIGraphicsPipelineTarget& target) const
    {
        (void)target;
        return false;
    }

    // Scene描画で参照するTexture BindingをFrame開始前に準備します。
    // Pipeline layoutとの互換性はBackend側で検証します。
    virtual bool PrepareSceneTextures(
        const std::vector<Ref<RHITexture>>& textures,
        const Ref<RHIGraphicsPipeline>& pipeline)
    {
        (void)textures;
        (void)pipeline;
        return false;
    }

    virtual Ref<RHITexture> CreateTexture(
        const RHITextureSpecification& specification,
        const void* initialData = nullptr,
        std::size_t initialDataSize = 0) = 0;
};

} // namespace Raven
