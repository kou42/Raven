#pragma once

#include "Raven/Renderer/RHI/RHIClearContext.h"
#include "Raven/Renderer/RHI/RHIFrameLifecycle.h"

#include "VulkanCommandBuffer.h"
#include "VulkanFrameRenderer.h"
#include "VulkanFrameSync.h"
#include "VulkanInstance.h"
#include "VulkanSurface.h"
#include "VulkanSwapChain.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace Raven
{
class Window;

// 既存OpenGL Applicationを変更せず、No-API Windowに接続できる独立した描画検証Contextです。
class VulkanClearContext : public RHIClearContext, public RHIFrameLifecycle
{
public:
    bool Init(Window& window) override;
    RHIFrameResult DrawClearFrame(const float clearColor[4]) override;
    RHIFrameResult BeginFrame() override;
    RHIFrameResult ClearFrame(const float clearColor[4]) override;
    RHIFrameResult EndFrame() override;
    RHIFrameResult Present() override;
    bool Resize(uint32_t width, uint32_t height) override;
    void Shutdown() override;
    ~VulkanClearContext();

private:
    VulkanInstance m_Instance;
    VulkanSurface m_Surface;
    VulkanSwapChain m_SwapChain;
    // 各Frame Slot専用のCommandPool/Buffer。GPU実行中のBufferをResetしないため分離します。
    std::vector<std::unique_ptr<VulkanCommandBuffer>> m_CommandBuffers;
    VulkanFrameSync m_FrameSync;
    VulkanFrameRenderer m_FrameRenderer;
    bool m_VSync = true;
    uint32_t m_ActiveFrame = 0;
    bool m_FrameActive = false;
};
} // namespace Raven
