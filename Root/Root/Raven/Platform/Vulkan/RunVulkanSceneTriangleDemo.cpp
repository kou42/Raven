#include "RunVulkanSceneTriangleDemo.h"

#include "VulkanSceneTriangleDemo.h"

#include "Raven/Core/Window.h"

#include <GLFW/glfw3.h>

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace Raven
{
namespace
{
bool ReadSPIRV(const std::string& path, RHIShaderBinary& shader)
{
    std::ifstream file(path, std::ios::binary);
    if (file.is_open() == false)
    {
        std::cerr << "SPIR-V file not found: " << path << '\n';
        return false;
    }
    std::vector<char> bytes(
        (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (file.bad() == true || bytes.empty() == true ||
        bytes.size() % sizeof(uint32_t) != 0)
    {
        std::cerr << "Invalid SPIR-V file: " << path << '\n';
        return false;
    }
    shader.Format = RHIShaderBinaryFormat::SPIRV;
    shader.Code.assign(bytes.begin(), bytes.end());
    shader.EntryPoint = "main";
    return true;
}
} // namespace

int RunVulkanSceneTriangleDemo()
{
    // main.cppの既存Clear Demoと同様、通常Applicationとは別Windowを使用します。
    auto window = Window::Create(WindowProps(
        "Raven Vulkan Scene Triangle", 1280, 720, RHIBackend::Vulkan));
    if (window == nullptr || window->GetNativeWindow() == nullptr)
    {
        return 1;
    }

    RHIShaderBinary vertexShader;
    RHIShaderBinary fragmentShader;
    const std::string shaderDirectory = "Raven/Assets/Shaders/Vulkan/";
    if (ReadSPIRV(shaderDirectory + "SceneTriangle.vert.spv", vertexShader) == false ||
        ReadSPIRV(shaderDirectory + "SceneTriangle.frag.spv", fragmentShader) == false)
    {
        return 1;
    }

    VulkanSceneTriangleDemo demo;
    if (demo.Init(*window, vertexShader, fragmentShader) == false)
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
