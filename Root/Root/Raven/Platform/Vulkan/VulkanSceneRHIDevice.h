#pragma once

#include "Raven/Renderer/RHI/RHIDevice.h"
#include "VulkanSceneContext.h"

namespace Raven
{

// Scene Contextが所有するVkDevice/RenderPassを借用するRHIDevice Adapterです。
// Contextより長く保持しないこと。Resize後は新しいRenderPassでPipelineを再生成します。
// 既存のVulkanSceneBufferはRHIBufferのResize/SetData契約をまだ満たさないため、
// Buffer/Texture生成を成功扱いせず、未対応を明示します。
class VulkanSceneRHIDevice final : public RHIDevice
{
public:
    explicit VulkanSceneRHIDevice(VulkanSceneContext& context)
        : m_Context(context)
    {
    }

    RHIBackend GetBackend() const override
    {
        return RHIBackend::Vulkan;
    }

    Ref<RHIBuffer> CreateBuffer(
        const RHIBufferSpecification& specification,
        const void* initialData = nullptr) override
    {
        (void)specification;
        (void)initialData;
        return nullptr;
    }

    Ref<RHIGraphicsPipeline> CreateGraphicsPipeline(
        const RHIGraphicsPipelineSpecification& specification) override
    {
        // RenderPass互換性、Device所有権、Resize時のPipeline無効化は
        // 既存Contextに一元化し、別のVkDeviceを誤って作らないようにします。
        return m_Context.CreateGraphicsPipeline(specification);
    }

    Ref<RHITexture> CreateTexture(
        const RHITextureSpecification& specification,
        const void* initialData = nullptr,
        std::size_t initialDataSize = 0) override
    {
        (void)specification;
        (void)initialData;
        (void)initialDataSize;
        return nullptr;
    }

private:
    VulkanSceneContext& m_Context;
};

} // namespace Raven
