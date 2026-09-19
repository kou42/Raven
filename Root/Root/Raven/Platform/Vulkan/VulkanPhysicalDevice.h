#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace Raven
{

// VkInstanceから利用可能なPhysical Device(GPU)を列挙し、
// 後続のQueue Family選択やLogical Device生成で利用する情報を保持します。
// この段階ではGPUの優先順位付けは行わず、Vulkanが返す全Deviceを保持します。
class VulkanPhysicalDevice
{
public:
    static constexpr uint32_t InvalidQueueFamilyIndex = UINT32_MAX;

    // Vulkan Driverから取得したGPU能力のSnapshotです。
    // VkPhysicalDevice自体のLifetimeはVkInstanceに従い、この構造体は破棄責務を持ちません。
    struct DeviceInfo
    {
        VkPhysicalDevice Handle = VK_NULL_HANDLE;
        VkPhysicalDeviceProperties Properties{};
        VkPhysicalDeviceFeatures Features{};
        VkPhysicalDeviceMemoryProperties MemoryProperties{};
        std::vector<VkQueueFamilyProperties> QueueFamilies;
        uint32_t GraphicsQueueFamilyIndex = InvalidQueueFamilyIndex;

        bool HasGraphicsQueue() const
        {
            return GraphicsQueueFamilyIndex != InvalidQueueFamilyIndex;
        }
    };

    // 全GPUの情報を保持し、少なくとも1つGraphics Queue対応GPUがあればtrueを返します。
    bool Enumerate(VkInstance instance);
    void Clear();

    const std::vector<DeviceInfo>& GetDevices() const { return m_Devices; }
    const DeviceInfo* FindFirstGraphicsDevice() const;
    bool HasDevices() const { return m_Devices.empty() == false; }

private:
    std::vector<DeviceInfo> m_Devices;
};

} // namespace Raven
