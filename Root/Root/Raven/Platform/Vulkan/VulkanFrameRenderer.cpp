#include "VulkanFrameRenderer.h"

#include "VulkanCommandBuffer.h"
#include "VulkanDevice.h"
#include "VulkanFrameSync.h"
#include "VulkanSwapChain.h"

#include <limits>
#include <iostream>

namespace Raven
{

VulkanFrameResult VulkanFrameRenderer::BeginFrame(
    const VulkanDevice& device, VulkanSwapChain& swapChain,
    VulkanCommandBuffer& commandBuffer, VulkanFrameSync& frameSync)
{
    if (m_FrameActive == true || m_Submitted == true)
    {
        return VulkanFrameResult::FatalError;
    }
    if (device.IsValid() == false || swapChain.IsValid() == false ||
        commandBuffer.IsValid() == false || frameSync.IsValid() == false) { return VulkanFrameResult::FatalError; }

    if (frameSync.WaitForFrame() == false) { return VulkanFrameResult::FatalError; }

    uint32_t imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(
        device.GetHandle(), swapChain.GetHandle(),
        std::numeric_limits<uint64_t>::max(),
        frameSync.GetImageAvailableSemaphore(), VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        std::cout << "[Vulkan Resize] Acquire: VK_ERROR_OUT_OF_DATE_KHR\n";
        return VulkanFrameResult::ResizeRequired;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) { return VulkanFrameResult::FatalError; }
    const bool acquiredSuboptimal = result == VK_SUBOPTIMAL_KHR;
    if (acquiredSuboptimal == true)
    {
        std::cout << "[Vulkan Resize] Acquire: VK_SUBOPTIMAL_KHR\n";
    }

    if (imageIndex >= swapChain.GetImages().size() ||
        frameSync.GetRenderFinishedSemaphore(imageIndex) == VK_NULL_HANDLE)
    {
        return VulkanFrameResult::FatalError;
    }

    m_ImageIndex = imageIndex;
    m_AcquiredSuboptimal = acquiredSuboptimal;
    if (commandBuffer.Reset() == false ||
        commandBuffer.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) == false) { return VulkanFrameResult::FatalError; }


    m_FrameActive = true;
    return VulkanFrameResult::Success;
}

VulkanFrameResult VulkanFrameRenderer::ClearFrame(
    VulkanSwapChain& swapChain, VulkanCommandBuffer& commandBuffer,
    const VkClearColorValue& clearColor)
{
    if (m_FrameActive == false || m_Submitted == true)
    {
        return VulkanFrameResult::FatalError;
    }
    const VkImage image = swapChain.GetImages()[m_ImageIndex];
    const VkImageLayout oldLayout = swapChain.GetImageLayout(m_ImageIndex);

    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.srcAccessMask = 0;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout = oldLayout;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = image;
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.layerCount = 1;

    const VkPipelineStageFlags sourceStage =
        oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
        ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
        : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    vkCmdPipelineBarrier(commandBuffer.GetHandle(), sourceStage, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toTransfer);

    VkImageSubresourceRange clearRange{};
    clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    clearRange.levelCount = 1;
    clearRange.layerCount = 1;
    vkCmdClearColorImage(commandBuffer.GetHandle(), image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &clearRange);

    VkImageMemoryBarrier toPresent{};
    toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.image = image;
    toPresent.subresourceRange = clearRange;
    vkCmdPipelineBarrier(commandBuffer.GetHandle(), VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &toPresent);


    return VulkanFrameResult::Success;
}

VulkanFrameResult VulkanFrameRenderer::EndFrame(
    const VulkanDevice& device, VulkanSwapChain& swapChain,
    VulkanCommandBuffer& commandBuffer, VulkanFrameSync& frameSync)
{
    if (m_FrameActive == false || m_Submitted == true)
    {
        return VulkanFrameResult::FatalError;
    }
    if (commandBuffer.End() == false) { return VulkanFrameResult::FatalError; }

    const VkSemaphore waitSemaphore = frameSync.GetImageAvailableSemaphore();
    const VkSemaphore signalSemaphore = frameSync.GetRenderFinishedSemaphore(m_ImageIndex);
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    const VkCommandBuffer commandBufferHandle = commandBuffer.GetHandle();

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &waitSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBufferHandle;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &signalSemaphore;

    // Command記録失敗でFenceを未Signalのまま残さないよう、Submit直前にResetします。
    if (frameSync.ResetFence() == false)
    {
        return VulkanFrameResult::FatalError;
    }
    VkResult result = vkQueueSubmit(device.GetGraphicsQueue(), 1, &submitInfo, frameSync.GetInFlightFence());
    if (result != VK_SUCCESS)
    {
        // Reset済みFenceは未Signalのため、次Frameを試行せずFatalErrorを返します。
        return VulkanFrameResult::FatalError;
    }

    // Submit成功後は、このImageに記録した最終Layoutを次FrameのBarrierへ引き継ぎます。
    swapChain.SetImageLayout(m_ImageIndex, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);


    // FenceをResetした後のSubmit失敗は再試行できないため、FatalErrorを返します。
    m_Submitted = true;
    return VulkanFrameResult::Success;
}

VulkanFrameResult VulkanFrameRenderer::Present(
    const VulkanDevice& device, VulkanSwapChain& swapChain, VulkanFrameSync& frameSync)
{
    if (m_FrameActive == false || m_Submitted == false)
    {
        return VulkanFrameResult::FatalError;
    }
    const VkSwapchainKHR swapChainHandle = swapChain.GetHandle();
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    const VkSemaphore signalSemaphore = frameSync.GetRenderFinishedSemaphore(m_ImageIndex);
    presentInfo.pWaitSemaphores = &signalSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapChainHandle;
    presentInfo.pImageIndices = &m_ImageIndex;

    const VkResult result = vkQueuePresentKHR(device.GetGraphicsQueue(), &presentInfo);
    // Submit済みのFrame SlotはPresent結果にかかわらず次のSlotへ進めます。
    // OUT_OF_DATE時はContextがWaitIdleして全Frame Resourceを再生成します。
    frameSync.AdvanceFrame();
    m_FrameActive = false;
    m_Submitted = false;
    if (result == VK_SUCCESS)
    {
        return m_AcquiredSuboptimal == true
            ? VulkanFrameResult::ResizeRequired : VulkanFrameResult::Success;
    }
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        std::cout << "[Vulkan Resize] Present: "
                  << (result == VK_ERROR_OUT_OF_DATE_KHR
                      ? "VK_ERROR_OUT_OF_DATE_KHR" : "VK_SUBOPTIMAL_KHR") << '\n';
        return VulkanFrameResult::ResizeRequired;
    }
    return VulkanFrameResult::FatalError;
}

VulkanFrameResult VulkanFrameRenderer::DrawClearFrame(
    const VulkanDevice& device, VulkanSwapChain& swapChain,
    VulkanCommandBuffer& commandBuffer, VulkanFrameSync& frameSync,
    const VkClearColorValue& clearColor)
{
    VulkanFrameResult result = BeginFrame(device, swapChain, commandBuffer, frameSync);
    if (result != VulkanFrameResult::Success)
    {
        return result;
    }
    result = ClearFrame(swapChain, commandBuffer, clearColor);
    if (result != VulkanFrameResult::Success)
    {
        return result;
    }
    result = EndFrame(device, swapChain, commandBuffer, frameSync);
    if (result != VulkanFrameResult::Success)
    {
        return result;
    }
    return Present(device, swapChain, frameSync);
}

} // namespace Raven
