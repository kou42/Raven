#pragma once

#include "Raven/Renderer/RHI/RHISceneFrameLifecycle.h"

#include "VulkanCommandBuffer.h"
#include "VulkanFrameRenderer.h"
#include "VulkanFrameSync.h"
#include "VulkanGraphicsPipeline.h"
#include "VulkanInstance.h"
#include "VulkanSceneRenderTarget.h"
#include "VulkanSceneBuffer.h"
#include "VulkanSurface.h"
#include "VulkanSwapChain.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace Raven
{
// Scene専用のVulkan Frame Contextです。Clear DemoとはGPU Resourceを共有しません。
// ApplicationへのFactory接続はScene用RHICommandList完成後に行います。
class VulkanSceneContext final : public RHISceneFrameLifecycle
{
public:
    ~VulkanSceneContext() override;

    bool Init(Window& window);
    RHIFrameResult BeginFrame() override;
    RHIFrameResult EndFrame() override;
    RHIFrameResult Present() override;
    bool Resize(uint32_t width, uint32_t height) override;
    void Shutdown();

    // RenderPass内のDynamic Viewport/Scissorを記録します。Pipeline側でDynamic Stateが必要です。
    bool SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
    void SetClearColor(const float color[4]);
    VkCommandBuffer GetActiveCommandBuffer() const;

    // Scene RenderPassと同じDevice/Color FormatでPipelineを生成します。
    // Resize時はnative Pipelineを無効化するため、呼び出し元は再生成してください。
    Ref<RHIGraphicsPipeline> CreateGraphicsPipeline(
        const RHIGraphicsPipelineSpecification& specification);
    // BeginFrame～EndFrameのRenderPass内だけで描画命令を記録します。
    // PipelineはこのContextで生成されたものに限定します。
    bool BindGraphicsPipeline(const Ref<RHIGraphicsPipeline>& pipeline);
    bool DrawIndexed(const VulkanSceneBuffer& vertexBuffer,
        const VulkanSceneBuffer& indexBuffer, uint32_t indexCount = 0,
        bool usePipelineVertexStride = false);
    // Scene Buffer生成と検証用。DeviceはContextが所有し、Shutdown後は使用不可です。
    const VulkanDevice& GetDevice() const { return m_Instance.GetDevice(); }
    VkFormat GetColorFormat() const { return m_SwapChain.GetImageFormat(); }
    VkRenderPass GetRenderPass() const { return m_RenderTarget.GetRenderPass(); }
    VkExtent2D GetExtent() const { return m_SwapChain.GetExtent(); }

private:
    VulkanInstance m_Instance;
    VulkanSurface m_Surface;
    VulkanSwapChain m_SwapChain;
    VulkanFrameSync m_FrameSync;
    VulkanFrameRenderer m_FrameRenderer;
    VulkanSceneRenderTarget m_RenderTarget;
    // Submit後もGPUが参照するため、Fence完了までPipelineを強参照で保持します。
    // Resize/ShutdownではWaitIdle後にnative handleを破棄します。
    std::vector<Ref<VulkanGraphicsPipeline>> m_GraphicsPipelines;
    Ref<VulkanGraphicsPipeline> m_BoundGraphicsPipeline;
    std::vector<std::unique_ptr<VulkanCommandBuffer>> m_CommandBuffers;
    VkClearColorValue m_ClearColor{};
    uint32_t m_ActiveFrame = 0;
    bool m_VSync = true;
    bool m_FrameActive = false;
    bool m_FrameSubmitted = false;
};
} // namespace Raven
