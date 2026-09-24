#include "RunVulkanSceneRuntimeDemo.h"

#include "VulkanSceneRuntime.h"

#include "Raven/Core/Window.h"
#include "Raven/Core/Application.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Mesh/PrimitiveMeshFactory.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Renderer/RHI/RHISceneFrameLifecycle.h"
#include "Raven/Scene/SceneCamera.h"
#include "Raven/Scene/Scene.h"
#include "Raven/Scene/ExplicitCubeSceneDemo.h"

#include <GLFW/glfw3.h>

#include <filesystem>
#include <iostream>

namespace Raven
{

int RunVulkanSceneRuntimeDemo()
{
    auto window = Window::Create(WindowProps(
        "Raven Vulkan Scene Runtime", 1280, 720, RHIBackend::Vulkan));
    if (window == nullptr || window->GetNativeWindow() == nullptr)
    {
        return 1;
    }

    const std::filesystem::path shaderDirectory =
        std::filesystem::path("Raven") / "Assets" / "Shaders";
    RHIShaderAssetSpecification vertexShader{};
    vertexShader.VulkanPath =
        (shaderDirectory / "Vulkan" / "SceneTriangle.vert.spv").generic_string();
    RHIShaderAssetSpecification fragmentShader{};
    fragmentShader.VulkanPath =
        (shaderDirectory / "Vulkan" / "SceneTriangle.frag.spv").generic_string();

    PipelineSpecification pipelineSpecification{};
    pipelineSpecification.Topology = PrimitiveTopology::Triangles;
    pipelineSpecification.Cull = CullMode::None;
    pipelineSpecification.DepthTest = true;
    pipelineSpecification.DepthWrite = true;
    pipelineSpecification.Blend = false;
    pipelineSpecification.DebugName = "Vulkan Normal Mesh Scene";

    auto runtime = CreateScope<VulkanSceneRuntime>();
    if (runtime->Init(
        *window,
        pipelineSpecification,
        vertexShader,
        fragmentShader) == false)
    {
        std::cerr << "Vulkan Scene Runtime initialization failed.\n";
        // Initが部分初期化状態を解放する設計ですが、終了入口でも明示します。
        runtime->Shutdown();
        return 1;
    }

    // Backend非依存の検証Sceneを共有し、GPU Buffer準備だけRuntimeに任せます。
    ExplicitCubeSceneDemo demo;
    if (demo.Init("Vulkan", runtime->GetWidth(), runtime->GetHeight()) == false)
    {
        std::cerr << "Vulkan Entity Scene creation failed.\n";
        Renderer::Shutdown();
        runtime->Shutdown();
        return 1;
    }
    if (runtime->PrepareScene(demo.GetScene()) == false)
    {
        std::cerr << "Vulkan Entity Scene mesh preparation failed.\n";
        demo.Shutdown();
        Renderer::Shutdown();
        runtime->Shutdown();
        return 1;
    }

    // Scene固有Hookだけを渡し、Prepare/Acquire/Draw/Resize/終了順序は共通Runnerへ委譲します。
    Application::ExplicitSceneHooks hooks;
    hooks.OnScene = [&demo]() { demo.Render(); };
    hooks.OnResizeCamera = [&demo](uint32_t width, uint32_t height)
    {
        demo.ResizeCamera(width, height);
    };
    hooks.OnBeforeShutdown = [&demo]() { demo.Shutdown(); };
    const int exitCode = Application::RunOwnedExplicitScene(
        std::move(window), std::move(runtime), hooks);
    return exitCode;
}

} // namespace Raven
