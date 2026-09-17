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
        // Instanceだけが残った部分初期化状態を成功扱いにしないよう、
        // Logical Deviceまで有効な場合に限って二重初期化を成功とします。
        if (IsValid() == true)
        {
            return true;
        }

        Shutdown();
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
        // vkCreateInstance失敗時に未定義Handleを残さないよう、明示的に初期状態へ戻します。
        m_Instance = VK_NULL_HANDLE;
        std::cout << "Failed to create Vulkan VkInstance. VkResult = "
                  << static_cast<int>(result) << '\n';
        return false;
    }

    std::cout << "Vulkan VkInstance created successfully.\n";

    // VkInstance生成直後にGPU一覧まで取得しておくことで、次段階のQueue Family選択が
    // VulkanInstanceのLifetime内にある有効なVkPhysicalDeviceだけを参照できます。
    if (m_PhysicalDevices.Enumerate(m_Instance) == false)
    {
        std::cout << "Failed to initialize Vulkan physical device list.\n";
        Shutdown();
        return false;
    }

    const VulkanPhysicalDevice::DeviceInfo* graphicsDevice =
        m_PhysicalDevices.FindFirstGraphicsDevice();
    if (graphicsDevice == nullptr)
    {
        std::cout << "No Vulkan physical device with a Graphics Queue was found.\n";
        Shutdown();
        return false;
    }

    std::cout << "Selected Vulkan Physical Device : "
              << graphicsDevice->Properties.deviceName << '\n';

    // 現段階ではGraphics Queueを持つ最初のGPUから最小Logical Deviceを生成します。
    // Present対応とSwapChain Extension条件はSurface導入時にDevice選択へ追加します。
    if (m_Device.Init(*graphicsDevice) == false)
    {
        std::cout << "Failed to initialize Vulkan logical device.\n";
        Shutdown();
        return false;
    }

    std::cout << "Vulkan base device initialization completed successfully.\n";
    return true;
}

void VulkanInstance::Shutdown()
{
    // Vulkanの親子Lifetimeに従い、Logical DeviceをInstanceより先に破棄します。
    m_Device.Shutdown();

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
