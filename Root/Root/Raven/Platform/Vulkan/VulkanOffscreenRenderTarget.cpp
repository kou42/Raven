#include "VulkanOffscreenRenderTarget.h"

#include "VulkanDevice.h"
#include "VulkanSceneRHITexture.h"

#include <array>
#include <cstring>

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
    const RHIOffscreenRenderTargetSpecification& specification,
    VkRenderPass pickingRenderPass, uint32_t frameSlotCount)
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

    if (specification.EnableEntityPicking == true &&
        InitEntityPicking(device, pickingRenderPass, specification, frameSlotCount) == false)
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
        m_ColorPassEnded == true || m_PickingPassActive == true ||
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
        (m_RenderPassActive == false && m_ColorPassEnded == false) ||
        m_PickingPassActive == true ||
        m_ColorLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        return false;
    }

    if (m_RenderPassActive == true)
    {
        vkCmdEndRenderPass(commandBuffer);
    }
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
    m_ColorPassEnded = false;
    return true;
}

void VulkanOffscreenRenderTarget::Shutdown()
{
    if (m_Device != VK_NULL_HANDLE)
    {
        // ContextはFence/WaitIdle後、借用RenderPassとDepthより先にPickingを破棄します。
        for (const PickingReadbackSlot& slot : m_PickingReadbacks)
        {
            if (slot.Buffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(m_Device, slot.Buffer, nullptr);
            }
            if (slot.Memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(m_Device, slot.Memory, nullptr);
            }
        }
        if (m_PickingFramebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(m_Device, m_PickingFramebuffer, nullptr);
        }
        if (m_PickingView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(m_Device, m_PickingView, nullptr);
        }
        if (m_PickingImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(m_Device, m_PickingImage, nullptr);
        }
        if (m_PickingMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(m_Device, m_PickingMemory, nullptr);
        }
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
    m_PickingReadbacks.clear();
    m_PickingImage = VK_NULL_HANDLE;
    m_PickingMemory = VK_NULL_HANDLE;
    m_PickingView = VK_NULL_HANDLE;
    m_PickingFramebuffer = VK_NULL_HANDLE;
    m_PickingRenderPass = VK_NULL_HANDLE;
    m_PickingPassActive = false;
    m_ColorPassEnded = false;
    m_ActivePickingRequest = {};
    m_PickingState.Reset();
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

bool VulkanOffscreenRenderTarget::InitEntityPicking(const VulkanDevice& device,
    VkRenderPass renderPass, const RHIOffscreenRenderTargetSpecification& specification,
    uint32_t frameSlotCount)
{
    if (renderPass == VK_NULL_HANDLE || frameSlotCount == 0u)
    {
        return false;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(device.GetPhysicalDeviceHandle(), VK_FORMAT_R32_SINT, &properties);
    if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) == 0u)
    {
        return false;
    }
    m_PickingRenderPass = renderPass;
    VkImageCreateInfo image{};
    image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = VK_FORMAT_R32_SINT;
    image.extent = { specification.Width, specification.Height, 1u };
    image.mipLevels = 1u;
    image.arrayLayers = 1u;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(m_Device, &image, nullptr, &m_PickingImage) != VK_SUCCESS)
    {
        return false;
    }
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(m_Device, m_PickingImage, &requirements);
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = FindMemoryType(device, requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocation.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(m_Device, &allocation, nullptr, &m_PickingMemory) != VK_SUCCESS ||
        vkBindImageMemory(m_Device, m_PickingImage, m_PickingMemory, 0u) != VK_SUCCESS)
    {
        return false;
    }
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = m_PickingImage;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = VK_FORMAT_R32_SINT;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.levelCount = 1u;
    view.subresourceRange.layerCount = 1u;
    if (vkCreateImageView(m_Device, &view, nullptr, &m_PickingView) != VK_SUCCESS)
    {
        return false;
    }
    const std::array<VkImageView, 2> attachments = { m_PickingView, m_DepthView };
    VkFramebufferCreateInfo framebuffer{};
    framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer.renderPass = renderPass;
    framebuffer.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebuffer.pAttachments = attachments.data();
    framebuffer.width = specification.Width;
    framebuffer.height = specification.Height;
    framebuffer.layers = 1u;
    if (vkCreateFramebuffer(m_Device, &framebuffer, nullptr, &m_PickingFramebuffer) != VK_SUCCESS)
    {
        return false;
    }
    m_PickingReadbacks.resize(frameSlotCount);
    for (PickingReadbackSlot& slot : m_PickingReadbacks)
    {
        VkBufferCreateInfo buffer{};
        buffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer.size = sizeof(std::int32_t);
        buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_Device, &buffer, nullptr, &slot.Buffer) != VK_SUCCESS)
        {
            return false;
        }
        vkGetBufferMemoryRequirements(m_Device, slot.Buffer, &requirements);
        allocation.allocationSize = requirements.size;
        // Coherentを必須にして、Fence後の小さなReadbackでinvalidate alignmentを扱わずに済ませます。
        allocation.memoryTypeIndex = FindMemoryType(device, requirements,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (allocation.memoryTypeIndex == UINT32_MAX ||
            vkAllocateMemory(m_Device, &allocation, nullptr, &slot.Memory) != VK_SUCCESS ||
            vkBindBufferMemory(m_Device, slot.Buffer, slot.Memory, 0u) != VK_SUCCESS)
        {
            return false;
        }
    }
    return true;
}

bool VulkanOffscreenRenderTarget::SupportsEntityPicking() const
{
    return IsValid() == true && m_Specification.EnableEntityPicking == true &&
        m_PickingFramebuffer != VK_NULL_HANDLE && m_PickingReadbacks.empty() == false;
}

bool VulkanOffscreenRenderTarget::RequestEntityPicking(uint32_t x, uint32_t y,
    std::uint64_t& outSequence)
{
    if (SupportsEntityPicking() == false)
    {
        outSequence = 0u;
        return false;
    }
    return m_PickingState.Request(m_Specification.Width, m_Specification.Height, x, y, outSequence);
}

bool VulkanOffscreenRenderTarget::ConsumeEntityPickingResult(
    std::uint64_t& outSequence, std::int32_t& outEntityID)
{
    return m_PickingState.ConsumeResult(outSequence, outEntityID);
}

bool VulkanOffscreenRenderTarget::TakePendingEntityPickingRequest(RHIEntityPickingRequest& outRequest)
{
    return SupportsEntityPicking() == true && m_PickingState.TakePendingRequest(outRequest);
}

bool VulkanOffscreenRenderTarget::BeginEntityPickingPass(VkCommandBuffer commands,
    const RHIEntityPickingRequest& request)
{
    if (SupportsEntityPicking() == false || commands == VK_NULL_HANDLE ||
        m_RenderPassActive == false || m_ColorPassEnded == true || m_PickingPassActive == true ||
        request.Sequence == 0u || request.X >= m_Specification.Width || request.Y >= m_Specification.Height)
    {
        return false;
    }
    vkCmdEndRenderPass(commands);
    m_RenderPassActive = false;
    m_ColorPassEnded = true;
    std::array<VkClearValue, 2> clears{};
    clears[0].color.int32[0] = -1;
    VkRenderPassBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = m_PickingRenderPass;
    begin.framebuffer = m_PickingFramebuffer;
    begin.renderArea.extent = { m_Specification.Width, m_Specification.Height };
    begin.clearValueCount = static_cast<uint32_t>(clears.size());
    begin.pClearValues = clears.data();
    vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
    m_ActivePickingRequest = request;
    m_PickingPassActive = true;
    return true;
}

bool VulkanOffscreenRenderTarget::EndEntityPickingPass(VkCommandBuffer commands,
    uint32_t frameSlot, const RHIEntityPickingRequest& request)
{
    if (commands == VK_NULL_HANDLE || m_PickingPassActive == false ||
        frameSlot >= m_PickingReadbacks.size() ||
        request.Sequence != m_ActivePickingRequest.Sequence ||
        request.X != m_ActivePickingRequest.X || request.Y != m_ActivePickingRequest.Y)
    {
        return false;
    }
    PickingReadbackSlot& slot = m_PickingReadbacks[frameSlot];
    // 同じslotの結果をCPUへ回収する前に上書きしません。
    if (slot.Sequence != 0u)
    {
        return false;
    }
    vkCmdEndRenderPass(commands);
    m_PickingPassActive = false;
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1u;
    copy.imageOffset = { static_cast<int32_t>(request.X), static_cast<int32_t>(request.Y), 0 };
    copy.imageExtent = { 1u, 1u, 1u };
    // 座標は共通Viewport変換で確定済みのTopLeft pixel。ここでYを再反転しません。
    vkCmdCopyImageToBuffer(commands, m_PickingImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        slot.Buffer, 1u, &copy);
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = slot.Buffer;
    barrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0u, 0u, nullptr, 1u, &barrier, 0u, nullptr);
    slot.Sequence = request.Sequence;
    return true;
}

bool VulkanOffscreenRenderTarget::ResolveEntityPickingReadback(uint32_t frameSlot)
{
    if (frameSlot >= m_PickingReadbacks.size())
    {
        return m_Specification.EnableEntityPicking == false;
    }
    PickingReadbackSlot& slot = m_PickingReadbacks[frameSlot];
    if (slot.Sequence == 0u)
    {
        return true;
    }
    void* mapped = nullptr;
    if (vkMapMemory(m_Device, slot.Memory, 0u, sizeof(std::int32_t), 0u, &mapped) != VK_SUCCESS)
    {
        return false;
    }
    std::int32_t entityID = -1;
    std::memcpy(&entityID, mapped, sizeof(entityID));
    vkUnmapMemory(m_Device, slot.Memory);
    // 古いClickならMailboxが破棄します。GPU処理成功と最新選択への採用は別の判定です。
    m_PickingState.PublishResult(slot.Sequence, entityID);
    slot.Sequence = 0u;
    return true;
}

VkDevice VulkanOffscreenRenderTarget::GetOwnerDevice() const
{
    return m_Device;
}

} // namespace Raven
