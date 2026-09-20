#pragma once

#include "VulkanSceneContext.h"

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

    // OpenGLのRenderCommand::Clearに対応するScene描画途中のClearです。
    // BeginFrameのLoadOp Clearとは異なり、呼び出した位置でGPU命令を記録します。
    bool ClearColor(const float color[4])
    {
        return m_Context.ClearColorAttachment(color);
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

private:
    VulkanSceneContext& m_Context;
};

} // namespace Raven
