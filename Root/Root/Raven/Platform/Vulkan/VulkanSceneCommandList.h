#pragma once

#include "VulkanSceneContext.h"
#include "VulkanSceneRHIBuffer.h"
#include "Raven/Renderer/RHI/RHISceneFrameLifecycle.h"

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

    bool SetScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override
    {
        return m_Context.SetScissor(x, y, width, height);
    }

    // OpenGLのRenderCommand::Clearに対応するScene描画途中のClearです。
    // BeginFrameのLoadOp Clearとは異なり、呼び出した位置でGPU命令を記録します。
    bool ClearColor(const float color[4]) override
    {
        return m_Context.ClearColorAttachment(color);
    }

    bool BindPipeline(const Ref<RHIGraphicsPipeline>& pipeline) override
    {
        return m_Context.BindGraphicsPipeline(pipeline);
    }

    // Material Snapshotを共通RHIのTextureとTintとしてBindします。
    // Maskedは現行Shaderでalpha cutoff未実装のため拒否します。
    bool BindMaterial(const RHIMaterialProperties& material) override
    {
        if (material.SurfaceType == MaterialSurfaceType::Masked ||
            material.Texture == nullptr ||
            m_Context.BindTexture(material.Texture) == false)
        {
            return false;
        }
        return m_Context.SetMaterialTint(material.Tint);
    }

    bool SetMaterialTint(const std::array<float, 4>& tint)
    {
        return m_Context.SetMaterialTint(tint);
    }

    bool SetClipTransform(const std::array<float, 16>& model) override
    {
        return m_Context.SetClipTransform(model);
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
        const Ref<RHIBuffer>& indexBuffer, uint32_t indexCount = 0,
        uint32_t firstIndex = 0) override
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
        if (m_Context.DrawIndexed(vertex->GetSceneBuffer(),
            index->GetSceneBuffer(), indexCount, true) == false)
        {
            return false;
        }
        // 記録済みCommand BufferはSubmit後にもResourceを参照するため、
        // 呼び出し側のRefが解放されてもContextがGPU完了まで保持します。
        m_Context.RetainDrawBuffers(vertexBuffer, indexBuffer);
        return true;
    }

private:
    VulkanSceneContext& m_Context;
};

} // namespace Raven
