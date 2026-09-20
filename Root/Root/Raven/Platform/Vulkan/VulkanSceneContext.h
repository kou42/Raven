#pragma once

#include "Raven/Renderer/RHI/RHISceneFrameLifecycle.h"

#include "VulkanCommandBuffer.h"
#include "VulkanFrameRenderer.h"
#include "VulkanFrameSync.h"
#include "VulkanInstance.h"
#include "VulkanSceneRenderTarget.h"
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
    bool Resize(uint32_t width, uint32_t height);
    void Shutdown();

    void SetClearColor(const float color[4]);
    VkCommandBuffer GetActiveCommandBuffer() const;
    VkRenderPass GetRenderPass() const { return m_RenderTarget.GetRenderPass(); }
    VkExtent2D GetExtent() const { return m_SwapChain.GetExtent(); }

private:
    VulkanInstance m_Instance;
    VulkanSurface m_Surface;
    VulkanSwapChain m_SwapChain;
    VulkanFrameSync m_FrameSync;
    VulkanFrameRenderer m_FrameRenderer;
    VulkanSceneRenderTarget m_RenderTarget;
    std::vector<std::unique_ptr<VulkanCommandBuffer>> m_CommandBuffers;
    VkClearColorValue m_ClearColor{};
    uint32_t m_ActiveFrame = 0;
    bool m_VSync = true;
    bool m_FrameActive = false;
    bool m_FrameSubmitted = false;
};
} // namespace Raven
