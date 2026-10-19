#include "VulkanOffscreenRenderTarget.h"

#include "VulkanDevice.h"
#include "VulkanSceneRHITexture.h"

#include <array>

namespace Raven
{
namespace
{

uint32_t FindMemoryType(const VulkanDevice& device,
    const VkMemoryRequirements& requirements, VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(
        device.GetPhysicalDeviceHandle(), &properties);
    for (uint32_t index = 0u; index < properties.memoryTypeCount; ++index)
    {
        if ((requirements.memoryTypeBits & (1u << index)) != 0u &&
            (properties.memoryTypes[index].propertyFlags & flags) == flags)
        {
            return index;
        }
    }
    return UINT32_MAX;
}

} // namespace

VulkanOffscreenRenderTarget::~VulkanOffscreenRenderTarget()
{
    Shutdown();
}

bool VulkanOffscreenRenderTarget::Init(const VulkanDevice& device,
    VkRenderPass renderPass, VkFormat colorFormat,
    const RHIOffscreenRenderTargetSpecification& specification)
{
    Shutdown();
    // 現行Scene Pipelineは常にD32 Depth付きRenderPass向けです。
    // DepthなしTargetを同じPipelineで扱うとRenderPass非互換になるため明示的に拒否します。
    if (device.IsValid() == false || renderPass == VK_NULL_HANDLE ||
        colorFormat == VK_FORMAT_UNDEFINED || specification.EnableDepth == false ||
        IsValidRHIOffscreenRenderTargetSpecification(specification) == false)
    {
        return false;
    }

    m_Device = device.GetHandle();
    m_RenderPass = renderPass;

    RHITextureSpecification colorSpecification{};
    colorSpecification.Width = specification.Width;
    colorSpecification.Height = specification.Height;
    colorSpecification.Format = specification.ColorFormat;
    colorSpecification.Usage = RHITextureUsage::RenderTarget;
    colorSpecification.GenerateMips = false;
    colorSpecification.DebugName = specification.DebugName + " Color";
    auto color = CreateRef<VulkanSceneRHITexture>();
    if (color->InitRenderTarget(
        device, colorSpecification, colorFormat) == false)
    {
        Shutdown();
        return false;
    }

    VkFormatProperties depthProperties{};
    vkGetPhysicalDeviceFormatProperties(device.GetPhysicalDeviceHandle(),
        VK_FORMAT_D32_SFLOAT, &depthProperties);
    if ((depthProperties.optimalTilingFeatures &
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0u)
    {
        Shutdown();
        return false;
    }

    VkImageCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depth.imageType = VK_IMAGE_TYPE_2D;
    depth.format = VK_FORMAT_D32_SFLOAT;
    depth.extent = {specification.Width, specification.Height, 1u};
    depth.mipLevels = 1u;
    depth.arrayLayers = 1u;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.tiling = VK_IMAGE_TILING_OPTIMAL;
    depth.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depth.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(m_Device, &depth, nullptr, &m_DepthImage) != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(m_Device, m_DepthImage, &requirements);
    const uint32_t memoryType = FindMemoryType(
        device, requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryType == UINT32_MAX)
    {
        Shutdown();
        return false;
    }
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(m_Device, &allocation, nullptr,
        &m_DepthMemory) != VK_SUCCESS ||
        vkBindImageMemory(m_Device, m_DepthImage,
            m_DepthMemory, 0u) != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }

    VkImageViewCreateInfo depthView{};
    depthView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depthView.image = m_DepthImage;
    depthView.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthView.format = depth.format;
    depthView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthView.subresourceRange.levelCount = 1u;
    depthView.subresourceRange.layerCount = 1u;
    if (vkCreateImageView(m_Device, &depthView, nullptr,
        &m_DepthView) != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }

    const std::array<VkImageView, 2> attachments = {
        color->GetNativeTexture().GetView(), m_DepthView
    };
    VkFramebufferCreateInfo framebuffer{};
    framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer.renderPass = m_RenderPass;
    framebuffer.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebuffer.pAttachments = attachments.data();
    framebuffer.width = specification.Width;
    framebuffer.height = specification.Height;
    framebuffer.layers = 1u;
    if (vkCreateFramebuffer(m_Device, &framebuffer, nullptr,
        &m_Framebuffer) != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }

    // 全native Resourceが揃ってからRefを公開し、部分初期化TargetをUIへ渡しません。
    m_ColorAttachment = std::move(color);
    m_Specification = specification;
    m_ColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

bool VulkanOffscreenRenderTarget::Begin(
    VkCommandBuffer commandBuffer, const float clearColor[4])
{
    const auto color =
        std::dynamic_pointer_cast<VulkanSceneRHITexture>(m_ColorAttachment);
    if (IsValid() == false || commandBuffer == VK_NULL_HANDLE ||
        clearColor == nullptr || color == nullptr || m_RenderPassActive == true ||
        (m_ColorLayout != VK_IMAGE_LAYOUT_UNDEFINED &&
            m_ColorLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
    {
        return false;
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = m_ColorLayout == VK_IMAGE_LAYOUT_UNDEFINED ?
        0u : VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = m_ColorLayout;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = color->GetNativeTexture().GetImage();
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1u;
    barrier.subresourceRange.layerCount = 1u;
    const VkPipelineStageFlags sourceStage =
        m_ColorLayout == VK_IMAGE_LAYOUT_UNDEFINED ?
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    vkCmdPipelineBarrier(commandBuffer, sourceStage,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
        0u, nullptr, 0u, nullptr, 1u, &barrier);

    std::array<VkClearValue, 2> clears{};
    for (uint32_t component = 0u; component < 4u; ++component)
    {
        clears[0].color.float32[component] = clearColor[component];
    }
    clears[1].depthStencil = {1.0f, 0u};
    VkRenderPassBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = m_RenderPass;
    begin.framebuffer = m_Framebuffer;
    begin.renderArea.extent = {
        m_Specification.Width, m_Specification.Height
    };
    begin.clearValueCount = static_cast<uint32_t>(clears.size());
    begin.pClearValues = clears.data();
    vkCmdBeginRenderPass(commandBuffer, &begin, VK_SUBPASS_CONTENTS_INLINE);
    m_ColorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    m_RenderPassActive = true;
    return true;
}

bool VulkanOffscreenRenderTarget::End(VkCommandBuffer commandBuffer)
{
    const auto color =
        std::dynamic_pointer_cast<VulkanSceneRHITexture>(m_ColorAttachment);
    if (commandBuffer == VK_NULL_HANDLE || color == nullptr ||
        m_RenderPassActive == false ||
        m_ColorLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        return false;
    }

    vkCmdEndRenderPass(commandBuffer);
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = color->GetNativeTexture().GetImage();
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1u;
    barrier.subresourceRange.layerCount = 1u;
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0u,
        0u, nullptr, 0u, nullptr, 1u, &barrier);
    m_ColorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    m_RenderPassActive = false;
    return true;
}

void VulkanOffscreenRenderTarget::Shutdown()
{
    if (m_Device != VK_NULL_HANDLE)
    {
        if (m_Framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(m_Device, m_Framebuffer, nullptr);
        }
        if (m_DepthView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(m_Device, m_DepthView, nullptr);
        }
        if (m_DepthImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(m_Device, m_DepthImage, nullptr);
        }
        if (m_DepthMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(m_Device, m_DepthMemory, nullptr);
        }
    }
    const auto color =
        std::dynamic_pointer_cast<VulkanSceneRHITexture>(m_ColorAttachment);
    if (color != nullptr)
    {
        // UIがColor AttachmentのRefを保持していても、Device/RenderPass破棄後に
        // stale native handleを使わないよう明示的に無効化します。
        color->Shutdown();
    }
    m_ColorAttachment.reset();
    m_Framebuffer = VK_NULL_HANDLE;
    m_DepthView = VK_NULL_HANDLE;
    m_DepthImage = VK_NULL_HANDLE;
    m_DepthMemory = VK_NULL_HANDLE;
    m_RenderPass = VK_NULL_HANDLE;
    m_Device = VK_NULL_HANDLE;
    m_ColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_Specification = {};
    m_RenderPassActive = false;
}

const RHIOffscreenRenderTargetSpecification&
VulkanOffscreenRenderTarget::GetSpecification() const
{
    return m_Specification;
}

const Ref<RHITexture>& VulkanOffscreenRenderTarget::GetColorAttachment() const
{
    return m_ColorAttachment;
}

bool VulkanOffscreenRenderTarget::IsValid() const
{
    const auto color =
        std::dynamic_pointer_cast<VulkanSceneRHITexture>(m_ColorAttachment);
    return m_Device != VK_NULL_HANDLE && m_RenderPass != VK_NULL_HANDLE &&
        m_Framebuffer != VK_NULL_HANDLE && m_DepthImage != VK_NULL_HANDLE &&
        m_DepthMemory != VK_NULL_HANDLE && m_DepthView != VK_NULL_HANDLE &&
        color != nullptr && color->GetNativeTexture().IsValid() == true;
}

VkDevice VulkanOffscreenRenderTarget::GetOwnerDevice() const
{
    return m_Device;
}

} // namespace Raven
