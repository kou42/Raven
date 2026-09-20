#pragma once

#include "VulkanSceneContext.h"
#include "VulkanSceneRHIBuffer.h"

namespace Raven
{

// Legacy RHICommandListはOpenGLのVertexArray/Shader APIを前提とするため、
// Explicit APIのScene Drawをそこへ無理に流用しない専用CommandListです。
// Frameとnative DeviceはVulkanSceneContextが所有し、このクラスは借用します。
class VulkanSceneCommandList final
{
public:
    explicit VulkanSceneCommandList(VulkanSceneContext& context)
        : m_Context(context)
    {
    }

    VulkanSceneCommandList(const VulkanSceneCommandList&) = delete;
    VulkanSceneCommandList& operator=(const VulkanSceneCommandList&) = delete;

    bool SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        return m_Context.SetViewport(x, y, width, height);
    }

    bool BindPipeline(const Ref<RHIGraphicsPipeline>& pipeline)
    {
        return m_Context.BindGraphicsPipeline(pipeline);
    }

    // indexCount == 0 はIndexBuffer全体を描画します。
    // BeginFrame/EndFrameの外側ではContextがfalseを返し、GPU命令を記録しません。
    bool DrawIndexed(const VulkanSceneBuffer& vertexBuffer,
        const VulkanSceneBuffer& indexBuffer, uint32_t indexCount = 0)
    {
        return m_Context.DrawIndexed(vertexBuffer, indexBuffer, indexCount);
    }

    // 共通RHIDeviceから生成したBufferをSceneの描画へ接続します。
    // Strideは現在Bind中のPipelineのBinding 0から取得します。
    bool DrawIndexed(const Ref<RHIBuffer>& vertexBuffer,
        const Ref<RHIBuffer>& indexBuffer, uint32_t indexCount = 0)
    {
        if (vertexBuffer == nullptr || indexBuffer == nullptr ||
            vertexBuffer->GetSpecification().Usage != RHIBufferUsage::Vertex ||
            indexBuffer->GetSpecification().Usage != RHIBufferUsage::Index)
        {
            return false;
        }
        auto vertex = std::dynamic_pointer_cast<VulkanSceneRHIBuffer>(vertexBuffer);
        auto index = std::dynamic_pointer_cast<VulkanSceneRHIBuffer>(indexBuffer);
        if (vertex == nullptr || index == nullptr)
        {
            return false;
        }
        return m_Context.DrawIndexed(vertex->GetSceneBuffer(),
            index->GetSceneBuffer(), indexCount, true);
    }

private:
    VulkanSceneContext& m_Context;
};

} // namespace Raven
