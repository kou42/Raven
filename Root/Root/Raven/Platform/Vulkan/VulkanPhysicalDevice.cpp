#include "VulkanPhysicalDevice.h"

#include <iostream>

namespace Raven
{

bool VulkanPhysicalDevice::Enumerate(VkInstance instance)
{
    Clear();

    if (instance == VK_NULL_HANDLE)
    {
        std::cout << "Cannot enumerate Vulkan physical devices because VkInstance is null.\n";
        return false;
    }

    uint32_t deviceCount = 0;
    VkResult result = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (result != VK_SUCCESS)
    {
        std::cout << "Failed to query Vulkan physical device count. VkResult = "
                  << static_cast<int>(result) << '\n';
        return false;
    }

    if (deviceCount == 0)
    {
        std::cout << "No Vulkan compatible physical device was found.\n";
        return false;
    }

    std::vector<VkPhysicalDevice> handles(deviceCount, VK_NULL_HANDLE);
    result = vkEnumeratePhysicalDevices(instance, &deviceCount, handles.data());
    if (result != VK_SUCCESS)
    {
        // VK_INCOMPLETEも不完全な一覧なので成功扱いにしません。
        // 次のQueue Family / Device選択へ欠けたGPU一覧を渡さないことを優先します。
        std::cout << "Failed to enumerate complete Vulkan physical device list. VkResult = "
                  << static_cast<int>(result) << '\n';
        return false;
    }

    handles.resize(deviceCount);
    m_Devices.reserve(deviceCount);

    for (VkPhysicalDevice handle : handles)
    {
        if (handle == VK_NULL_HANDLE)
        {
            continue;
        }

        DeviceInfo info{};
        info.Handle = handle;
        vkGetPhysicalDeviceProperties(handle, &info.Properties);
        vkGetPhysicalDeviceFeatures(handle, &info.Features);
        vkGetPhysicalDeviceMemoryProperties(handle, &info.MemoryProperties);

        // Logical Device作成では必要なQueue Family indexを指定するため、
        // GPU基本情報と同じタイミングでQueue能力もSnapshotとして保持します。
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(handle, &queueFamilyCount, nullptr);
        if (queueFamilyCount > 0)
        {
            info.QueueFamilies.resize(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(
                handle,
                &queueFamilyCount,
                info.QueueFamilies.data());
            info.QueueFamilies.resize(queueFamilyCount);
        }

        m_Devices.push_back(info);

        const uint32_t apiMajor = VK_VERSION_MAJOR(info.Properties.apiVersion);
        const uint32_t apiMinor = VK_VERSION_MINOR(info.Properties.apiVersion);
        const uint32_t apiPatch = VK_VERSION_PATCH(info.Properties.apiVersion);

        std::cout << "Vulkan Physical Device [" << (m_Devices.size() - 1) << "]\n";
        std::cout << "  Name : " << info.Properties.deviceName << '\n';
        std::cout << "  Vendor ID : " << info.Properties.vendorID << '\n';
        std::cout << "  Device ID : " << info.Properties.deviceID << '\n';
        std::cout << "  API Version : " << apiMajor << '.' << apiMinor << '.' << apiPatch << '\n';
        std::cout << "  Queue Families : " << info.QueueFamilies.size() << '\n';
    }

    return m_Devices.empty() == false;
}

void VulkanPhysicalDevice::Clear()
{
    m_Devices.clear();
}

} // namespace Raven
