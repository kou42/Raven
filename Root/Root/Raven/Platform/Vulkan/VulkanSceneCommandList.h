#pragma once

#include "Raven/Renderer/RHI/RHISceneCommandList.h"
#include "VulkanSceneContext.h"

namespace Raven
{

// Legacy RHICommandListはOpenGLのVertexArray/Shader APIを前提とするため、
// Explicit APIのScene Drawをそこへ無理に流用しない専用CommandListです。
// Frameとnative DeviceはVulkanSceneContextが所有し、このクラスは借用します。
class VulkanSceneCommandList final : public RHISceneCommandList
{
public:
    explicit VulkanSceneCommandList(VulkanSceneContext& context)
        : m_Context(context)
    {
    }

    VulkanSceneCommandList(const VulkanSceneCommandList&) = delete;
    VulkanSceneCommandList& operator=(const VulkanSceneCommandList&) = delete;

    bool SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override
    {
        return m_Context.SetViewport(x, y, width, height);
    }

    bool BindPipeline(const Ref<RHIGraphicsPipeline>& pipeline) override
    {
        return m_Context.BindGraphicsPipeline(pipeline);
    }

    // indexCount == 0 はIndexBuffer全体を描画します。
    // BeginFrame/EndFrameの外側ではContextがfalseを返し、GPU命令を記録しません。
    bool DrawIndexed(const RHISceneBuffer& vertexBuffer,
        const RHISceneBuffer& indexBuffer, uint32_t indexCount = 0) override
    {
        // native Bufferを共通インターフェースへ露出させず、Backend境界で型を検証します。
        // 他BackendのResourceはContextへ渡す前に拒否します。
        const auto* vulkanVertex = dynamic_cast<const VulkanSceneBuffer*>(&vertexBuffer);
        const auto* vulkanIndex = dynamic_cast<const VulkanSceneBuffer*>(&indexBuffer);
        if (vulkanVertex == nullptr || vulkanIndex == nullptr)
        {
            return false;
        }

        return m_Context.DrawIndexed(*vulkanVertex, *vulkanIndex, indexCount);
    }

private:
    VulkanSceneContext& m_Context;
};

} // namespace Raven
