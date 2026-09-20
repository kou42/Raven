#pragma once

#include "Raven/Renderer/RHI/RHIDevice.h"
#include "VulkanSceneContext.h"
#include "VulkanSceneRHIBuffer.h"
#include "VulkanSceneRHITexture.h"

namespace Raven
{

// Scene Contextが所有するVkDevice/RenderPassを借用するRHIDevice Adapterです。
// Contextより長く保持しないこと。Resize後は新しいRenderPassでPipelineを再生成します。
// Vertex/Index BufferとRGBA8 Sampled Textureを共通RHI経由で生成します。
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

    bool GetGraphicsPipelineTarget(
        RHIGraphicsPipelineTarget& target) const override
    {
        target = {};
        switch (m_Context.GetColorFormat())
        {
        case VK_FORMAT_R8G8B8A8_UNORM:
            target.ColorFormat = RHIColorFormat::RGBA8Unorm;
            break;
        case VK_FORMAT_B8G8R8A8_UNORM:
            target.ColorFormat = RHIColorFormat::BGRA8Unorm;
            break;
        case VK_FORMAT_R8G8B8A8_SRGB:
            target.ColorFormat = RHIColorFormat::RGBA8Srgb;
            break;
        case VK_FORMAT_B8G8R8A8_SRGB:
            target.ColorFormat = RHIColorFormat::BGRA8Srgb;
            break;
        default:
            return false;
        }

        target.DepthFormat = RHIDepthFormat::D32Float;
        target.SampleCount = 1;
        return true;
    }

    bool PrepareSceneTextures(
        const std::vector<Ref<RHITexture>>& textures,
        const Ref<RHIGraphicsPipeline>& pipeline) override
    {
        return m_Context.RebuildTextureDescriptors(textures, pipeline);
    }

    Ref<RHITexture> CreateTexture(
        const RHITextureSpecification& specification,
        const void* initialData = nullptr,
        std::size_t initialDataSize = 0) override
    {
        if (m_Context.GetDevice().IsValid() == false ||
            m_Context.GetActiveCommandBuffer() != VK_NULL_HANDLE)
        {
            return nullptr;
        }
        auto texture = CreateRef<VulkanSceneRHITexture>();
        if (texture->Init(m_Context.GetDevice(), specification,
            initialData, initialDataSize) == false)
        {
            return nullptr;
        }
        m_Context.RegisterTexture(texture);
        return texture;
    }

private:
    VulkanSceneContext& m_Context;
};

} // namespace Raven
