#pragma once

#include "Raven/Renderer/RHI/RHISceneResourceFactory.h"
#include "VulkanSceneContext.h"

#include <limits>

namespace Raven
{

// ContextのDeviceとRenderTargetを借用してScene Resourceを生成します。
// Contextより長生きさせず、GPUのBuffer参照が完了してからBufferを破棄してください。
class VulkanSceneResourceFactory final : public RHISceneResourceFactory
{
public:
    explicit VulkanSceneResourceFactory(VulkanSceneContext& context)
        : m_Context(context)
    {
    }

    Scope<RHISceneBuffer> CreateVertexBuffer(
        const void* data, uint32_t byteSize, uint32_t stride) override
    {
        if (data == nullptr || byteSize == 0 || stride == 0)
        {
            return nullptr;
        }

        auto buffer = CreateScope<VulkanSceneBuffer>();
        if (buffer->InitVertex(m_Context.GetDevice(), data, byteSize, stride) == false)
        {
            return nullptr;
        }
        return buffer;
    }

    Scope<RHISceneBuffer> CreateIndexBuffer(
        const uint32_t* indices, uint32_t count) override
    {
        if (indices == nullptr || count == 0 ||
            count > std::numeric_limits<uint32_t>::max() / sizeof(uint32_t))
        {
            return nullptr;
        }

        auto buffer = CreateScope<VulkanSceneBuffer>();
        if (buffer->InitIndex(m_Context.GetDevice(), indices, count) == false)
        {
            return nullptr;
        }
        return buffer;
    }

    Ref<RHIGraphicsPipeline> CreateGraphicsPipeline(
        const RHIGraphicsPipelineSpecification& specification) override
    {
        RHIGraphicsPipelineSpecification resolved = specification;
        const RHIColorFormat targetFormat = GetSceneColorFormat();
        if (targetFormat == RHIColorFormat::None)
        {
            return nullptr;
        }

        // 指定されたFormatを黙って上書きしないことでRenderTargetとの不一致を検出します。
        if (resolved.ColorFormat == RHIColorFormat::None)
        {
            resolved.ColorFormat = targetFormat;
        }
        if (resolved.ColorFormat != targetFormat)
        {
            return nullptr;
        }
        return m_Context.CreateGraphicsPipeline(resolved);
    }

private:
    RHIColorFormat GetSceneColorFormat() const
    {
        switch (m_Context.GetColorFormat())
        {
        case VK_FORMAT_R8G8B8A8_UNORM:
            return RHIColorFormat::RGBA8Unorm;
        case VK_FORMAT_B8G8R8A8_UNORM:
            return RHIColorFormat::BGRA8Unorm;
        case VK_FORMAT_R8G8B8A8_SRGB:
            return RHIColorFormat::RGBA8Srgb;
        case VK_FORMAT_B8G8R8A8_SRGB:
            return RHIColorFormat::BGRA8Srgb;
        default:
            return RHIColorFormat::None;
        }
    }

    VulkanSceneContext& m_Context;
};

} // namespace Raven
