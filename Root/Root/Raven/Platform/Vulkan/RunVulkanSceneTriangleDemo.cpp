#include "RunVulkanSceneTriangleDemo.h"

#include "VulkanSceneTriangleDemo.h"

#include "Raven/Assets/RHIShaderAsset.h"
#include "Raven/Core/Window.h"

#include <GLFW/glfw3.h>

#include <filesystem>
#include <iostream>

namespace Raven
{

int RunVulkanSceneTriangleDemo()
{
    // main.cppの既存Clear Demoと同様、通常Applicationとは別Windowを使用します。
    auto window = Window::Create(WindowProps(
        "Raven Vulkan Scene Triangle", 1280, 720, RHIBackend::Vulkan));
    if (window == nullptr || window->GetNativeWindow() == nullptr)
    {
        return 1;
    }

    const std::filesystem::path shaderDirectory =
        std::filesystem::path("Raven") / "Assets" / "Shaders";
    RHIShaderAssetSpecification vertexSpecification{};
    vertexSpecification.VulkanPath =
        (shaderDirectory / "Vulkan" / "SceneTriangle.vert.spv").generic_string();
    RHIShaderAssetSpecification fragmentSpecification{};
    fragmentSpecification.VulkanPath =
        (shaderDirectory / "Vulkan" / "SceneTriangle.frag.spv").generic_string();

    // Asset ManagerがBackend別Pathの選択と重複読込の防止を担当します。
    RHIShaderAssetManager shaderAssets;
    const Ref<RHIShaderAsset> vertexShader =
        shaderAssets.Load(vertexSpecification, RHIBackend::Vulkan);
    const Ref<RHIShaderAsset> fragmentShader =
        shaderAssets.Load(fragmentSpecification, RHIBackend::Vulkan);
    if (vertexShader == nullptr || fragmentShader == nullptr)
    {
        std::cerr << "Vulkan Scene Triangle shader asset load failed.\n";
        return 1;
    }

    VulkanSceneTriangleDemo demo;
    if (demo.Init(
        *window,
        vertexShader->GetBinary(),
        fragmentShader->GetBinary()) == false)
    {
        std::cerr << "Vulkan Scene Triangle initialization failed.\n";
        return 1;
    }

    GLFWwindow* nativeWindow = static_cast<GLFWwindow*>(window->GetNativeWindow());
    int exitCode = 0;
    while (glfwWindowShouldClose(nativeWindow) == GLFW_FALSE)
    {
        window->PollEvents();
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(nativeWindow, &width, &height);
        if (width <= 0 || height <= 0)
        {
            // 最小化中は0サイズのSwapChainを生成しません。
            glfwWaitEvents();
            continue;
        }
        const VkExtent2D extent = demo.GetExtent();
        if (extent.width != static_cast<uint32_t>(width) ||
            extent.height != static_cast<uint32_t>(height))
        {
            if (demo.Resize(static_cast<uint32_t>(width),
                static_cast<uint32_t>(height)) == false)
            {
                exitCode = 1;
                break;
            }
        }

        const RHIFrameResult result = demo.DrawFrame();
        if (result == RHIFrameResult::ResizeRequired)
        {
            // OUT_OF_DATE時は次のIterationでFramebuffer Sizeを再確認します。
            // サイズが変わらなくてもSwapChain再生成が必要な場合があります。
            if (demo.Resize(static_cast<uint32_t>(width),
                static_cast<uint32_t>(height)) == false)
            {
                exitCode = 1;
                break;
            }
        }
        else if (result != RHIFrameResult::Success)
        {
            exitCode = 1;
            break;
        }
    }
    demo.Shutdown();
    return exitCode;
}
} // namespace Raven
