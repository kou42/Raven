#include "VulkanFrameSync.h"

#include "VulkanDevice.h"

#include <iostream>
#include <limits>

namespace Raven
{

VulkanFrameSync::~VulkanFrameSync()
{
    Shutdown();
}

bool VulkanFrameSync::Init(const VulkanDevice& device)
{
    Shutdown();

    if (device.IsValid() == false)
    {
        std::cout << "Cannot create Vulkan FrameSync because Device is invalid.\n";
        return false;
    }

    m_Device = device.GetHandle();

    VkSemaphoreCreateInfo semaphoreCreateInfo{};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkResult result = vkCreateSemaphore(
        m_Device, &semaphoreCreateInfo, nullptr, &m_ImageAvailableSemaphore);
    if (result != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }

    result = vkCreateSemaphore(
        m_Device, &semaphoreCreateInfo, nullptr, &m_RenderFinishedSemaphore);
    if (result != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }

    VkFenceCreateInfo fenceCreateInfo{};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    result = vkCreateFence(m_Device, &fenceCreateInfo, nullptr, &m_InFlightFence);
    if (result != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }

    std::cout << "Vulkan Frame Semaphore/Fence created successfully.\n";
    return true;
}

bool VulkanFrameSync::WaitForFrame() const
{
    if (IsValid() == false)
    {
        return false;
    }

    const VkResult result = vkWaitForFences(
        m_Device,
        1,
        &m_InFlightFence,
        VK_TRUE,
        std::numeric_limits<uint64_t>::max());
    return result == VK_SUCCESS;
}

bool VulkanFrameSync::ResetFence() const
{
    if (IsValid() == false)
    {
        return false;
    }

    return vkResetFences(m_Device, 1, &m_InFlightFence) == VK_SUCCESS;
}

void VulkanFrameSync::Shutdown()
{
    if (m_Device != VK_NULL_HANDLE)
    {
        if (m_InFlightFence != VK_NULL_HANDLE)
        {
            vkDestroyFence(m_Device, m_InFlightFence, nullptr);
        }
        if (m_RenderFinishedSemaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(m_Device, m_RenderFinishedSemaphore, nullptr);
        }
        if (m_ImageAvailableSemaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(m_Device, m_ImageAvailableSemaphore, nullptr);
        }
    }

    m_InFlightFence = VK_NULL_HANDLE;
    m_RenderFinishedSemaphore = VK_NULL_HANDLE;
    m_ImageAvailableSemaphore = VK_NULL_HANDLE;
    m_Device = VK_NULL_HANDLE;
}

} // namespace Raven
