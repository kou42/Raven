#include "VulkanFrameRenderer.h"

#include "VulkanCommandBuffer.h"
#include "VulkanDevice.h"
#include "VulkanFrameSync.h"
#include "VulkanSwapChain.h"

#include <iostream>
#include <limits>

namespace Raven
{

bool VulkanFrameRenderer::DrawClearFrame(
    const VulkanDevice& device,
    VulkanSwapChain& swapChain,
    VulkanCommandBuffer& commandBuffer,
    VulkanFrameSync& frameSync,
    const VkClearColorValue& clearColor)
{
    if (device.IsValid() == false ||
        swapChain.IsValid() == false ||
        commandBuffer.IsValid() == false ||
        frameSync.IsValid() == false)
    {
        return false;
    }

    if (frameSync.WaitForFrame() == false)
    {
        return false;
    }

    uint32_t imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(
        device.GetHandle(),
        swapChain.GetHandle(),
        std::numeric_limits<uint64_t>::max(),
        frameSync.GetImageAvailableSemaphore(),
        VK_NULL_HANDLE,
        &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        // Resize時のSwapChain再生成は次のWindow Resize対応で行います。
        return false;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        return false;
    }

    if (frameSync.ResetFence() == false ||
        commandBuffer.Reset() == false ||
        commandBuffer.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) == false)
    {
        return false;
    }

    const VkImage image = swapChain.GetImages()[imageIndex];

    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.srcAccessMask = 0;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = image;
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.baseMipLevel = 0;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.baseArrayLayer = 0;
    toTransfer.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer.GetHandle(),
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &toTransfer);

    VkImageSubresourceRange clearRange{};
    clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    clearRange.baseMipLevel = 0;
    clearRange.levelCount = 1;
    clearRange.baseArrayLayer = 0;
    clearRange.layerCount = 1;

    vkCmdClearColorImage(
        commandBuffer.GetHandle(),
        image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &clearColor,
        1,
        &clearRange);

    VkImageMemoryBarrier toPresent{};
    toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toPresent.dstAccessMask = 0;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.image = image;
    toPresent.subresourceRange = clearRange;

    vkCmdPipelineBarrier(
        commandBuffer.GetHandle(),
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &toPresent);

    if (commandBuffer.End() == false)
    {
        return false;
    }

    const VkSemaphore waitSemaphore = frameSync.GetImageAvailableSemaphore();
    const VkSemaphore signalSemaphore = frameSync.GetRenderFinishedSemaphore();
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

    result = vkQueueSubmit(
        device.GetGraphicsQueue(),
        1,
        &submitInfo,
        frameSync.GetInFlightFence());
    if (result != VK_SUCCESS)
    {
        return false;
    }

    const VkSwapchainKHR swapChainHandle = swapChain.GetHandle();
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &signalSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapChainHandle;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(device.GetGraphicsQueue(), &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        return false;
    }

    return result == VK_SUCCESS;
}

} // namespace Raven
