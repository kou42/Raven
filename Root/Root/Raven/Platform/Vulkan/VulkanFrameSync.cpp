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

bool VulkanFrameSync::Init(const VulkanDevice& device, uint32_t imageCount, uint32_t frameCount)
{
    Shutdown();
    if (device.IsValid() == false || imageCount == 0 || frameCount == 0)
    {
        std::cout << "Cannot create Vulkan FrameSync: invalid device/image count.\n";
        return false;
    }

    m_Device = device.GetHandle();
    VkSemaphoreCreateInfo semaphoreCreateInfo{};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    // Frame SlotごとにAcquire Semaphoreを分離し、前FrameのGPU処理と重ねます。
    for (uint32_t index = 0; index < frameCount; ++index)
    {
        VkSemaphore semaphore = VK_NULL_HANDLE;
        if (vkCreateSemaphore(m_Device, &semaphoreCreateInfo, nullptr, &semaphore) != VK_SUCCESS)
        {
            Shutdown();
            return false;
        }
        m_ImageAvailableSemaphores.push_back(semaphore);
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
    for (uint32_t index = 0; index < frameCount; ++index)
    {
        VkFence fence = VK_NULL_HANDLE;
        if (vkCreateFence(m_Device, &fenceCreateInfo, nullptr, &fence) != VK_SUCCESS)
        {
            Shutdown();
            return false;
        }
        m_InFlightFences.push_back(fence);
    }
    m_CurrentFrame = 0;
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
    const VkFence fence = GetInFlightFence();
    return vkWaitForFences(m_Device, 1, &fence, VK_TRUE,
        std::numeric_limits<uint64_t>::max()) == VK_SUCCESS;
}

bool VulkanFrameSync::ResetFence() const
{
    if (IsValid() == false)
    {
        return false;
    }
    const VkFence fence = GetInFlightFence();
    return vkResetFences(m_Device, 1, &fence) == VK_SUCCESS;
}

void VulkanFrameSync::AdvanceFrame()
{
    if (m_InFlightFences.empty() == false)
    {
        m_CurrentFrame = (m_CurrentFrame + 1) % static_cast<uint32_t>(m_InFlightFences.size());
    }
}

void VulkanFrameSync::Shutdown()
{
    // GPU利用中に破棄しないこと。Context側がDeviceWaitIdleを保証します。
    if (m_Device != VK_NULL_HANDLE)
    {
        for (VkFence fence : m_InFlightFences)
        {
            if (fence != VK_NULL_HANDLE)
            {
                vkDestroyFence(m_Device, fence, nullptr);
            }
        }
        for (VkSemaphore semaphore : m_RenderFinishedSemaphores)
        {
            if (semaphore != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(m_Device, semaphore, nullptr);
            }
        }
        for (VkSemaphore semaphore : m_ImageAvailableSemaphores)
        {
            if (semaphore != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(m_Device, semaphore, nullptr);
            }
        }
    }
    m_InFlightFences.clear();
    m_CurrentFrame = 0;
    m_RenderFinishedSemaphores.clear();
    m_ImageAvailableSemaphores.clear();
    m_Device = VK_NULL_HANDLE;
}
} // namespace Raven
