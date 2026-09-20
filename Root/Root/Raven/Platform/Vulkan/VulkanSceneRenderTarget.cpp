#include "VulkanSceneRenderTarget.h"

#include "VulkanSwapChain.h"

#include <array>

namespace Raven
{
VulkanSceneRenderTarget::~VulkanSceneRenderTarget()
{
    Shutdown();
}

bool VulkanSceneRenderTarget::Init(VkDevice device, VkPhysicalDevice physicalDevice,
    const VulkanSwapChain& swapChain)
{
    Shutdown();
    if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE ||
        swapChain.IsValid() == false ||
        swapChain.GetImageFormat() == VK_FORMAT_UNDEFINED)
    {
        return false;
    }

    m_Device = device;
    m_Extent = swapChain.GetExtent();
    VkFormatProperties depthProperties{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_D32_SFLOAT,
        &depthProperties);
    if ((depthProperties.optimalTilingFeatures &
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0)
    {
        Shutdown();
        return false;
    }
    m_DepthFormat = VK_FORMAT_D32_SFLOAT;
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

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

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = m_DepthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depthReference{};
    depthReference.attachment = 1;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    const std::array<VkAttachmentDescription, 2> attachments = {
        colorAttachment, depthAttachment
    };
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
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

        // SwapChain ImageごとにDepthを分離し、Frame間の同時使用を避けます。
        VkImageCreateInfo depthInfo{};
        depthInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        depthInfo.imageType = VK_IMAGE_TYPE_2D;
        depthInfo.format = m_DepthFormat;
        depthInfo.extent = {m_Extent.width, m_Extent.height, 1};
        depthInfo.mipLevels = 1;
        depthInfo.arrayLayers = 1;
        depthInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        depthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depthInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        depthInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImage depthImage = VK_NULL_HANDLE;
        if (vkCreateImage(m_Device, &depthInfo, nullptr, &depthImage) != VK_SUCCESS)
        {
            Shutdown();
            return false;
        }
        m_DepthImages.push_back(depthImage);

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(m_Device, depthImage, &requirements);
        uint32_t memoryIndex = UINT32_MAX;
        for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index)
        {
            if ((requirements.memoryTypeBits & (1u << index)) != 0 &&
                (memoryProperties.memoryTypes[index].propertyFlags &
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
            {
                memoryIndex = index;
                break;
            }
        }
        if (memoryIndex == UINT32_MAX)
        {
            Shutdown();
            return false;
        }
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryIndex;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        if (vkAllocateMemory(m_Device, &allocation, nullptr, &memory) != VK_SUCCESS)
        {
            Shutdown();
            return false;
        }
        m_DepthMemories.push_back(memory);
        if (vkBindImageMemory(m_Device, depthImage, memory, 0) != VK_SUCCESS)
        {
            Shutdown();
            return false;
        }
        VkImageViewCreateInfo depthViewInfo{};
        depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        depthViewInfo.image = depthImage;
        depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        depthViewInfo.format = m_DepthFormat;
        depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depthViewInfo.subresourceRange.levelCount = 1;
        depthViewInfo.subresourceRange.layerCount = 1;
        VkImageView depthView = VK_NULL_HANDLE;
        if (vkCreateImageView(m_Device, &depthViewInfo, nullptr, &depthView) != VK_SUCCESS)
        {
            Shutdown();
            return false;
        }
        m_DepthViews.push_back(depthView);
        const std::array<VkImageView, 2> framebufferAttachments = {
            m_ImageViews.back(), m_DepthViews.back()
        };
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_RenderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(framebufferAttachments.size());
        framebufferInfo.pAttachments = framebufferAttachments.data();
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

    std::array<VkClearValue, 2> clears{};
    clears[0].color = clearColor;
    clears[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    beginInfo.renderPass = m_RenderPass;
    beginInfo.framebuffer = m_Framebuffers[imageIndex];
    beginInfo.renderArea.extent = m_Extent;
    beginInfo.clearValueCount = static_cast<uint32_t>(clears.size());
    beginInfo.pClearValues = clears.data();
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
        for (VkImageView depthView : m_DepthViews)
        {
            vkDestroyImageView(m_Device, depthView, nullptr);
        }
        for (VkImage depthImage : m_DepthImages)
        {
            vkDestroyImage(m_Device, depthImage, nullptr);
        }
        for (VkDeviceMemory memory : m_DepthMemories)
        {
            vkFreeMemory(m_Device, memory, nullptr);
        }
        if (m_RenderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
        }
    }
    m_DepthViews.clear();
    m_DepthImages.clear();
    m_DepthMemories.clear();
    m_DepthFormat = VK_FORMAT_UNDEFINED;
    m_Framebuffers.clear();
    m_ImageViews.clear();
    m_RenderPass = VK_NULL_HANDLE;
    m_Device = VK_NULL_HANDLE;
    m_Extent = {};
}
} // namespace Raven
