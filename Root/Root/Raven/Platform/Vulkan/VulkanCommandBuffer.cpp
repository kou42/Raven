#include "VulkanCommandBuffer.h"

#include "VulkanDevice.h"

#include <iostream>

namespace Raven
{

VulkanCommandBuffer::~VulkanCommandBuffer()
{
    Shutdown();
}

bool VulkanCommandBuffer::Init(const VulkanDevice& device)
{
    Shutdown();

    if (device.IsValid() == false)
    {
        std::cout << "Cannot create Vulkan CommandBuffer because Device is invalid.\n";
        return false;
    }

    VkCommandPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolCreateInfo.queueFamilyIndex = device.GetGraphicsQueueFamilyIndex();

    VkResult result = vkCreateCommandPool(
        device.GetHandle(),
        &poolCreateInfo,
        nullptr,
        &m_CommandPool);
    if (result != VK_SUCCESS)
    {
        m_CommandPool = VK_NULL_HANDLE;
        std::cout << "Failed to create Vulkan CommandPool. VkResult = "
                  << static_cast<int>(result) << '\n';
        return false;
    }

    m_Device = device.GetHandle();

    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = m_CommandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;

    result = vkAllocateCommandBuffers(
        m_Device,
        &allocateInfo,
        &m_CommandBuffer);
    if (result != VK_SUCCESS)
    {
        std::cout << "Failed to allocate Vulkan CommandBuffer. VkResult = "
                  << static_cast<int>(result) << '\n';
        Shutdown();
        return false;
    }

    std::cout << "Vulkan CommandPool and Primary CommandBuffer created successfully.\n";
    return true;
}

bool VulkanCommandBuffer::Reset()
{
    if (IsValid() == false)
    {
        return false;
    }

    // Pool作成時にRESET_COMMAND_BUFFER_BITを指定しているため、
    // Frame単位で同じPrimary CommandBufferを再利用できます。
    const VkResult result = vkResetCommandBuffer(m_CommandBuffer, 0);
    if (result != VK_SUCCESS)
    {
        std::cout << "Failed to reset Vulkan CommandBuffer. VkResult = "
                  << static_cast<int>(result) << '\n';
        return false;
    }

    return true;
}

bool VulkanCommandBuffer::Begin(VkCommandBufferUsageFlags usageFlags)
{
    if (IsValid() == false)
    {
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = usageFlags;

    const VkResult result = vkBeginCommandBuffer(m_CommandBuffer, &beginInfo);
    if (result != VK_SUCCESS)
    {
        std::cout << "Failed to begin Vulkan CommandBuffer. VkResult = "
                  << static_cast<int>(result) << '\n';
        return false;
    }

    return true;
}

bool VulkanCommandBuffer::End()
{
    if (IsValid() == false)
    {
        return false;
    }

    const VkResult result = vkEndCommandBuffer(m_CommandBuffer);
    if (result != VK_SUCCESS)
    {
        std::cout << "Failed to end Vulkan CommandBuffer. VkResult = "
                  << static_cast<int>(result) << '\n';
        return false;
    }

    return true;
}

void VulkanCommandBuffer::Shutdown()
{
    if (m_CommandPool != VK_NULL_HANDLE && m_Device != VK_NULL_HANDLE)
    {
        // CommandPool破棄時にPoolから確保したCommandBufferもまとめて解放されます。
        vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
    }

    m_CommandBuffer = VK_NULL_HANDLE;
    m_CommandPool = VK_NULL_HANDLE;
    m_Device = VK_NULL_HANDLE;
}

} // namespace Raven
