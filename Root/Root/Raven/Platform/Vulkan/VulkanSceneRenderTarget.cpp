#include "VulkanSceneRenderTarget.h"

#include "VulkanSwapChain.h"

namespace Raven
{
VulkanSceneRenderTarget::~VulkanSceneRenderTarget()
{
    Shutdown();
}

bool VulkanSceneRenderTarget::Init(VkDevice device, const VulkanSwapChain& swapChain)
{
    Shutdown();
    if (device == VK_NULL_HANDLE || swapChain.IsValid() == false ||
        swapChain.GetImageFormat() == VK_FORMAT_UNDEFINED)
    {
        return false;
    }

    m_Device = device;
    m_Extent = swapChain.GetExtent();

    // Layout遷移はVulkanFrameRendererが行うため、RenderPass内ではColor Attachmentの
    // Layoutを変えません。初回FrameのUNDEFINEDも外側のBarrierで処理します。
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapChain.GetImageFormat();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorReference{};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    if (vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_RenderPass) != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }

    // Image数はSurfaceにより変わるため、SwapChainの実Image数から生成します。
    for (VkImage image : swapChain.GetImages())
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapChain.GetImageFormat();
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView imageView = VK_NULL_HANDLE;
        if (vkCreateImageView(m_Device, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
        {
            Shutdown();
            return false;
        }
        m_ImageViews.push_back(imageView);

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_RenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &m_ImageViews.back();
        framebufferInfo.width = m_Extent.width;
        framebufferInfo.height = m_Extent.height;
        framebufferInfo.layers = 1;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        if (vkCreateFramebuffer(m_Device, &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS)
        {
            Shutdown();
            return false;
        }
        m_Framebuffers.push_back(framebuffer);
    }
    return IsValid();
}

bool VulkanSceneRenderTarget::Begin(
    VkCommandBuffer commandBuffer, uint32_t imageIndex,
    const VkClearColorValue& clearColor) const
{
    if (IsValid() == false || commandBuffer == VK_NULL_HANDLE ||
        imageIndex >= m_Framebuffers.size())
    {
        return false;
    }

    VkClearValue clear{};
    clear.color = clearColor;
    VkRenderPassBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    beginInfo.renderPass = m_RenderPass;
    beginInfo.framebuffer = m_Framebuffers[imageIndex];
    beginInfo.renderArea.extent = m_Extent;
    beginInfo.clearValueCount = 1;
    beginInfo.pClearValues = &clear;
    vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
    return true;
}

void VulkanSceneRenderTarget::End(VkCommandBuffer commandBuffer) const
{
    if (commandBuffer != VK_NULL_HANDLE)
    {
        vkCmdEndRenderPass(commandBuffer);
    }
}

void VulkanSceneRenderTarget::Shutdown()
{
    if (m_Device != VK_NULL_HANDLE)
    {
        // 呼び出し元はSwapChain Resize前にGPU完了を保証します。
        for (VkFramebuffer framebuffer : m_Framebuffers)
        {
            vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
        }
        for (VkImageView imageView : m_ImageViews)
        {
            vkDestroyImageView(m_Device, imageView, nullptr);
        }
        if (m_RenderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
        }
    }
    m_Framebuffers.clear();
    m_ImageViews.clear();
    m_RenderPass = VK_NULL_HANDLE;
    m_Device = VK_NULL_HANDLE;
    m_Extent = {};
}
} // namespace Raven
