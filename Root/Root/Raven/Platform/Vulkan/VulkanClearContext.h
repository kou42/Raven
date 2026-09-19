#pragma once

#include "VulkanCommandBuffer.h"
#include "VulkanFrameRenderer.h"
#include "VulkanFrameSync.h"
#include "VulkanInstance.h"
#include "VulkanSurface.h"
#include "VulkanSwapChain.h"

#include <cstdint>

namespace Raven
{
class Window;

// 既存OpenGL Applicationを変更せず、No-API Windowに接続できる独立した描画検証Contextです。
class VulkanClearContext
{
public:
    bool Init(Window& window);
    VulkanFrameResult DrawClearFrame(const VkClearColorValue& clearColor);
    bool Resize(uint32_t width, uint32_t height);
    void Shutdown();
    ~VulkanClearContext();

private:
    VulkanInstance m_Instance;
    VulkanSurface m_Surface;
    VulkanSwapChain m_SwapChain;
    VulkanCommandBuffer m_CommandBuffer;
    VulkanFrameSync m_FrameSync;
    VulkanFrameRenderer m_FrameRenderer;
    bool m_VSync = true;
};
} // namespace Raven
