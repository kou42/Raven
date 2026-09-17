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

    std::vector<VkPhysicalDevice> handles;

    // 列挙中にDevice数が変化するとVK_INCOMPLETEが返る可能性があります。
    // その場合は最新の件数を再取得して配列を作り直し、欠けた一覧を後続処理へ渡さないようにします。
    do
    {
        handles.assign(deviceCount, VK_NULL_HANDLE);
        result = vkEnumeratePhysicalDevices(instance, &deviceCount, handles.data());

        if (result == VK_INCOMPLETE)
        {
            uint32_t updatedDeviceCount = 0;
            const VkResult countResult = vkEnumeratePhysicalDevices(instance, &updatedDeviceCount, nullptr);
            if (countResult != VK_SUCCESS || updatedDeviceCount == 0)
            {
                std::cout << "Failed to refresh Vulkan physical device count. VkResult = "
                          << static_cast<int>(countResult) << '\n';
                return false;
            }

            deviceCount = updatedDeviceCount;
        }
    } while (result == VK_INCOMPLETE);

    if (result != VK_SUCCESS)
    {
        std::cout << "Failed to enumerate Vulkan physical devices. VkResult = "
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
        m_Devices.push_back(info);

        const uint32_t apiMajor = VK_VERSION_MAJOR(info.Properties.apiVersion);
        const uint32_t apiMinor = VK_VERSION_MINOR(info.Properties.apiVersion);
        const uint32_t apiPatch = VK_VERSION_PATCH(info.Properties.apiVersion);

        std::cout << "Vulkan Physical Device [" << (m_Devices.size() - 1) << "]\n";
        std::cout << "  Name : " << info.Properties.deviceName << '\n';
        std::cout << "  Vendor ID : " << info.Properties.vendorID << '\n';
        std::cout << "  Device ID : " << info.Properties.deviceID << '\n';
        std::cout << "  API Version : " << apiMajor << '.' << apiMinor << '.' << apiPatch << '\n';
    }

    return m_Devices.empty() == false;
}

void VulkanPhysicalDevice::Clear()
{
    m_Devices.clear();
}

} // namespace Raven
