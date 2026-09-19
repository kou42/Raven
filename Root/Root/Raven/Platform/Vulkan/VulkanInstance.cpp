#include "VulkanInstance.h"
#include "Raven/Platform/RHIDebugConfig.h"

#include <iostream>
#include <cstring>
#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace Raven
{
#if defined(_DEBUG)
namespace
{
VKAPI_ATTR VkBool32 VKAPI_CALL OnVulkanDebugMessage(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* userData)
{
    (void)userData;
    const char* level = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0
        ? "ERROR" : ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0 ? "WARNING" : "INFO");
    const char* category = (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) != 0
        ? "VALIDATION" : ((type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) != 0
            ? "PERFORMANCE" : "GENERAL");
    std::cerr << "[Vulkan Validation][" << level << "][" << category << "] "
              << (callbackData != nullptr && callbackData->pMessage != nullptr ? callbackData->pMessage : "")
              << '\n';
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT MakeDebugMessengerInfo()
{
    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    if (RHIDebugConfig::FromEnvironment().VerboseMessages == true)
    {
        info.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
    }
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = OnVulkanDebugMessage;
    return info;
}
} // namespace
#endif


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

    if (CreateInstance() == false)
    {
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

    // SwapChain実装ではLogical Device生成時点でVK_KHR_swapchainが必要です。
    // Present Queueの最終判定はSurface生成後に行います。
    if (m_Device.Init(*graphicsDevice) == false)
    {
        std::cout << "Failed to initialize Vulkan logical device.\n";
        Shutdown();
        return false;
    }

    if (IsValid() == false)
    {
        std::cout << "Vulkan base device initialization ended in an invalid state.\n";
        Shutdown();
        return false;
    }

    std::cout << "Vulkan base device initialization completed successfully.\n";
    return true;
}

bool VulkanInstance::CreateInstance()
{
    if (glfwVulkanSupported() == GLFW_FALSE)
    {
        std::cout << "GLFW reports that Vulkan is not supported by the current environment.\n";
        return false;
    }

    uint32_t requiredExtensionCount = 0;
    const char** requiredExtensions =
        glfwGetRequiredInstanceExtensions(&requiredExtensionCount);
    if (requiredExtensions == nullptr || requiredExtensionCount == 0)
    {
        std::cout << "Failed to query GLFW Vulkan instance extensions.\n";
        return false;
    }

    // GLFWが返すExtensionにはVK_KHR_surfaceとWindows用Surface Extensionが含まれます。
    // Platform固有Extension名をVulkan層へ直接ハードコードせず、Window libraryに必要条件を問い合わせます。
    std::vector<const char*> enabledExtensions(
        requiredExtensions,
        requiredExtensions + requiredExtensionCount);

#if defined(_DEBUG)
    // SDKが未導入の環境でも通常の描画確認を継続できるよう、Layerは存在を確認してから有効化します。
    const RHIDebugConfig debugConfig = RHIDebugConfig::FromEnvironment();
    bool validationAvailable = false;
    uint32_t layerCount = 0;
    if (vkEnumerateInstanceLayerProperties(&layerCount, nullptr) == VK_SUCCESS)
    {
        std::vector<VkLayerProperties> layers(layerCount);
        if (vkEnumerateInstanceLayerProperties(&layerCount, layers.data()) == VK_SUCCESS)
        {
            for (const auto& layer : layers)
            {
                if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0)
                {
                    validationAvailable = true;
                    break;
                }
            }
        }
    }

    bool debugUtilsAvailable = false;
    uint32_t extensionCount = 0;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr) == VK_SUCCESS)
    {
        std::vector<VkExtensionProperties> extensions(extensionCount);
        if (vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data()) == VK_SUCCESS)
        {
            for (const auto& extension : extensions)
            {
                if (std::strcmp(extension.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0)
                {
                    debugUtilsAvailable = true;
                    break;
                }
            }
        }
    }
    if (debugUtilsAvailable == true && debugConfig.EnableValidation == true)
    {
        enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    else
    {
        std::cerr << "[Vulkan Validation] VK_EXT_debug_utils is unavailable.\n";
    }
    if (validationAvailable == false && debugConfig.EnableValidation == true)
    {
        std::cerr << "[Vulkan Validation] VK_LAYER_KHRONOS_validation is unavailable.\n";
    }
    const char* validationLayer = "VK_LAYER_KHRONOS_validation";
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = MakeDebugMessengerInfo();
#endif

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
    createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
    createInfo.ppEnabledExtensionNames = enabledExtensions.data();
    createInfo.enabledLayerCount = 0;
    createInfo.ppEnabledLayerNames = nullptr;
#if defined(_DEBUG)
    if (validationAvailable == true && debugConfig.EnableValidation == true)
    {
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = &validationLayer;
        // Instance生成・破棄時のメッセージも捕捉します。
        if (debugUtilsAvailable == true)
        {
            createInfo.pNext = &debugCreateInfo;
        }
    }
#endif

    const VkResult result = vkCreateInstance(&createInfo, nullptr, &m_Instance);
    if (result != VK_SUCCESS)
    {
        m_Instance = VK_NULL_HANDLE;
        std::cout << "Failed to create Vulkan VkInstance. VkResult = "
                  << static_cast<int>(result) << '\n';
        return false;
    }

#if defined(_DEBUG)
    if (validationAvailable == true && debugUtilsAvailable == true && debugConfig.EnableValidation == true)
    {
        const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_Instance, "vkCreateDebugUtilsMessengerEXT"));
        if (createMessenger != nullptr)
        {
            const VkResult debugResult = createMessenger(m_Instance, &debugCreateInfo, nullptr, &m_DebugMessenger);
            if (debugResult != VK_SUCCESS)
            {
                m_DebugMessenger = VK_NULL_HANDLE;
                std::cerr << "[Vulkan Validation] Failed to create debug messenger: "
                          << static_cast<int>(debugResult) << '\n';
            }
        }
    }
#endif
    std::cout << "Vulkan VkInstance created successfully.\n";
    std::cout << "  Enabled Instance Extensions : " << enabledExtensions.size() << '\n';
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

#if defined(_DEBUG)
    if (m_DebugMessenger != VK_NULL_HANDLE)
    {
        const auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyMessenger != nullptr)
        {
            destroyMessenger(m_Instance, m_DebugMessenger, nullptr);
        }
        m_DebugMessenger = VK_NULL_HANDLE;
    }
#endif
    // VkInstanceが所有するVulkanオブジェクトは、今後この呼び出しより先に破棄します。
    vkDestroyInstance(m_Instance, nullptr);
    m_Instance = VK_NULL_HANDLE;
}

} // namespace Raven
