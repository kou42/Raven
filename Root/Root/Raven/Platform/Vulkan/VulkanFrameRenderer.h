#pragma once

#include <vulkan/vulkan.h>

namespace Raven
{

class VulkanCommandBuffer;
class VulkanDevice;
class VulkanFrameSync;
class VulkanSwapChain;

// SwapChain Image取得からClear/Submit/Presentまでの1 Frameを段階別に実行します。
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
    VulkanFrameResult BeginFrame(const VulkanDevice& device, VulkanSwapChain& swapChain,
        VulkanCommandBuffer& commandBuffer, VulkanFrameSync& frameSync);
    // SceneのColor Attachment用Layout遷移。RenderPassの開始・終了は後続の描画層が担当します。
    VulkanFrameResult BeginSceneColorTarget(VulkanSwapChain& swapChain,
        VulkanCommandBuffer& commandBuffer);
    VulkanFrameResult EndSceneColorTarget(VulkanSwapChain& swapChain,
        VulkanCommandBuffer& commandBuffer);
    VulkanFrameResult ClearFrame(VulkanSwapChain& swapChain,
        VulkanCommandBuffer& commandBuffer, const VkClearColorValue& clearColor);
    VulkanFrameResult EndFrame(const VulkanDevice& device, VulkanSwapChain& swapChain,
        VulkanCommandBuffer& commandBuffer, VulkanFrameSync& frameSync);
    VulkanFrameResult Present(const VulkanDevice& device, VulkanSwapChain& swapChain,
        VulkanFrameSync& frameSync);

    VulkanFrameResult DrawClearFrame(
        const VulkanDevice& device,
        VulkanSwapChain& swapChain,
        VulkanCommandBuffer& commandBuffer,
        VulkanFrameSync& frameSync,
        const VkClearColorValue& clearColor);

private:
    // Acquire成功後のImageとFrame状態を保持し、Submit成功後のみPresentへ進めます。
    uint32_t m_ImageIndex = 0;
    bool m_AcquiredSuboptimal = false;
    bool m_FrameActive = false;
    bool m_Submitted = false;
    bool m_ColorTargetActive = false;
    bool m_ColorTargetFinished = false;
    bool m_TransferClearFinished = false;
};

} // namespace Raven
