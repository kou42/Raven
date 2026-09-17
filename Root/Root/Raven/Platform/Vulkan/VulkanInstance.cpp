#include "VulkanInstance.h"

#include <iostream>

namespace Raven
{

VulkanInstance::~VulkanInstance()
{
    Shutdown();
}

bool VulkanInstance::Init()
{
    if (m_Instance != VK_NULL_HANDLE)
    {
        // 二重初期化で既存Instanceを失わないよう、生成済みならそのまま成功とします。
        return true;
    }

    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "Raven";
    applicationInfo.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    applicationInfo.pEngineName = "Raven";
    applicationInfo.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    applicationInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &applicationInfo;

    // この段階ではGPU列挙とDevice生成に必要な最小Instanceだけを作ります。
    // Window Surface用ExtensionやValidation Layerは、対応する学習ステップで追加します。
    createInfo.enabledExtensionCount = 0;
    createInfo.ppEnabledExtensionNames = nullptr;
    createInfo.enabledLayerCount = 0;
    createInfo.ppEnabledLayerNames = nullptr;

    const VkResult result = vkCreateInstance(&createInfo, nullptr, &m_Instance);
    if (result != VK_SUCCESS)
    {
        m_Instance = VK_NULL_HANDLE;
        std::cout << "Failed to create Vulkan VkInstance. VkResult = "
                  << static_cast<int>(result) << '\n';
        return false;
    }

    // VkInstance生成直後にGPU一覧まで取得しておくことで、次段階のQueue Family選択が
    // VulkanInstanceのLifetime内にある有効なVkPhysicalDeviceだけを参照できます。
    if (m_PhysicalDevices.Enumerate(m_Instance) == false)
    {
        std::cout << "Failed to initialize Vulkan physical device list.\n";
        Shutdown();
        return false;
    }

    std::cout << "Vulkan VkInstance created successfully.\n";
    return true;
}

void VulkanInstance::Shutdown()
{
    // VkPhysicalDevice自体の明示破棄は不要ですが、Instance破棄後に無効Handleを
    // 保持し続けないよう、先に列挙結果を破棄します。
    m_PhysicalDevices.Clear();

    if (m_Instance == VK_NULL_HANDLE)
    {
        return;
    }

    // VkInstanceが所有するVulkanオブジェクトは、今後この呼び出しより先に破棄します。
    vkDestroyInstance(m_Instance, nullptr);
    m_Instance = VK_NULL_HANDLE;
}

} // namespace Raven
