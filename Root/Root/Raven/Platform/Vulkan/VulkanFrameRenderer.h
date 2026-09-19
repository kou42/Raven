#pragma once

#include <vulkan/vulkan.h>

namespace Raven
{

class VulkanCommandBuffer;
class VulkanDevice;
class VulkanFrameSync;
class VulkanSwapChain;

// SwapChain Image取得からClear/Submit/Presentまでの1 Frameを実行します。
// Triangle前に同期・Image Layout遷移・Queue投入の基礎を独立して確認するための学習用経路です。
enum class VulkanFrameResult
{
    Success,
    ResizeRequired,
    FatalError
};

class VulkanFrameRenderer
{
public:
    VulkanFrameResult DrawClearFrame(
        const VulkanDevice& device,
        VulkanSwapChain& swapChain,
        VulkanCommandBuffer& commandBuffer,
        VulkanFrameSync& frameSync,
        const VkClearColorValue& clearColor);
};

} // namespace Raven
