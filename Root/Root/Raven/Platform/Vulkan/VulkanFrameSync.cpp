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

bool VulkanFrameSync::Init(const VulkanDevice& device, uint32_t imageCount)
{
    Shutdown();
    if (device.IsValid() == false || imageCount == 0)
    {
        std::cout << "Cannot create Vulkan FrameSync: invalid device/image count.\n";
        return false;
    }

    m_Device = device.GetHandle();
    VkSemaphoreCreateInfo semaphoreCreateInfo{};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(m_Device, &semaphoreCreateInfo, nullptr, &m_ImageAvailableSemaphore) != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }

    // 再AcquireされたImageについては以前のPresentがそのImageを解放済みなので、
    // Imageに紐付けたSemaphoreを安全に再利用できます。
    for (uint32_t index = 0; index < imageCount; ++index)
    {
        VkSemaphore semaphore = VK_NULL_HANDLE;
        if (vkCreateSemaphore(m_Device, &semaphoreCreateInfo, nullptr, &semaphore) != VK_SUCCESS)
        {
            Shutdown();
            return false;
        }
        m_RenderFinishedSemaphores.push_back(semaphore);
    }

    VkFenceCreateInfo fenceCreateInfo{};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(m_Device, &fenceCreateInfo, nullptr, &m_InFlightFence) != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }
    std::cout << "Vulkan Frame Semaphore/Fence created successfully.\n";
    return true;
}

VkSemaphore VulkanFrameSync::GetRenderFinishedSemaphore(uint32_t imageIndex) const
{
    if (imageIndex >= m_RenderFinishedSemaphores.size())
    {
        return VK_NULL_HANDLE;
    }
    return m_RenderFinishedSemaphores[imageIndex];
}

bool VulkanFrameSync::WaitForFrame() const
{
    if (IsValid() == false)
    {
        return false;
    }
    return vkWaitForFences(m_Device, 1, &m_InFlightFence, VK_TRUE,
        std::numeric_limits<uint64_t>::max()) == VK_SUCCESS;
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
    // GPU利用中に破棄しないこと。Context側がDeviceWaitIdleを保証します。
    if (m_Device != VK_NULL_HANDLE)
    {
        if (m_InFlightFence != VK_NULL_HANDLE)
        {
            vkDestroyFence(m_Device, m_InFlightFence, nullptr);
        }
        for (VkSemaphore semaphore : m_RenderFinishedSemaphores)
        {
            if (semaphore != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(m_Device, semaphore, nullptr);
            }
        }
        if (m_ImageAvailableSemaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(m_Device, m_ImageAvailableSemaphore, nullptr);
        }
    }
    m_InFlightFence = VK_NULL_HANDLE;
    m_RenderFinishedSemaphores.clear();
    m_ImageAvailableSemaphore = VK_NULL_HANDLE;
    m_Device = VK_NULL_HANDLE;
}
} // namespace Raven
