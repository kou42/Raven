#pragma once

#include "Raven/Renderer/RHI/RHIDevice.h"
#include "VulkanSceneContext.h"
#include "VulkanSceneRHIBuffer.h"

namespace Raven
{

// Scene Contextが所有するVkDevice/RenderPassを借用するRHIDevice Adapterです。
// Contextより長く保持しないこと。Resize後は新しいRenderPassでPipelineを再生成します。
// Vertex/Index Bufferのみ共通RHIBuffer経由で生成します。Textureは未対応です。
// Buffer更新はContextでGPU同期し、Shutdown時は外部Refのnative Bufferも無効化します。
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
        // Contextが所有するDeviceで作成し、別DeviceのResourceを混在させません。
        auto buffer = CreateRef<VulkanSceneRHIBuffer>();
        if (buffer->Init(m_Context, specification, initialData) == false)
        {
            return nullptr;
        }
        m_Context.RegisterBuffer(buffer);
        return buffer;
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
